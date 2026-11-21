#pragma once
#include "parse_packets.hpp"
#include "parse_part.hpp"
#include "sys.hpp"
#include <mutex>

namespace pipeline::parse {
struct Tabs {
    std::vector<u8> owned_prod,owned_camp;
    std::span<const u8> prod,camp;
    std::optional<std::pair<u64,u64>> crange;
};
inline std::once_flag tabs_once;
inline Tabs global_tabs;
inline std::atomic_bool CORE_READY{},PARTS_READY{};
inline std::optional<std::pair<u64,u64>> camp_range(std::span<const u8> camp) {
    std::size_t lo=0,hi=camp.size();
    while(lo<hi && camp[lo]==255) ++lo;
    if(lo==hi) return {};
    while(camp[hi-1]==255) --hi;
    if(std::find(camp.begin()+lo,camp.begin()+hi,255)!=camp.begin()+hi) return {};
    return std::pair<u64,u64>{lo,hi-lo-1};
}
inline void init_tabs(std::span<const u8> prod,std::span<const u8> camp) {
    std::call_once(tabs_once,[&] {
        auto& t=global_tabs;
        t.owned_prod.resize(PBOUND+65,0); t.owned_camp.resize(CBOUND+64,255);
        std::copy_n(prod.begin(),PBOUND+1,t.owned_prod.begin());
        std::copy_n(camp.begin(),CBOUND,t.owned_camp.begin());
        t.prod=t.owned_prod; t.camp=t.owned_camp; t.crange=camp_range(t.camp.first(CBOUND));
    });
}
inline void init_tabs_shared(const u8 *prod,std::size_t np,const u8 *camp,std::size_t nc) {
    if(np<PBOUND+65 || nc<CBOUND+64) sys::fallback("lookup table padding");
    std::call_once(tabs_once,[&] { auto& t=global_tabs; t.prod={prod,np}; t.camp={camp,nc}; t.crange=camp_range(t.camp.first(CBOUND)); });
}
template<class G> inline const Tabs& tabs(const G& g) {init_tabs(g.prod,g.camp);return global_tabs;}

template<class G,class T> struct Ctx {
    G& g; T& t;
    EvBatch& eb; CkBatch& cb; Out& out; const Tabs& tb;
    u64 tid,base,fend;
    u64 token(u64 id) {return (id&ID_ODD) ? raw_token(base+odd_offset(id),odd_len(id)) : id;}
    [[gnu::cold,gnu::noinline]] u64 raw_token(u64 off,std::size_t len) {
        if(len>=255) while(off+len<fend) {
            const auto c=g.region[off+len]; if(c==',' || c=='"' || c=='\n') break; ++len;
        }
        const auto idx=t.raws.size();
        if(idx>=(u64{1}<<34)) sys::fallback("too many raw IDs");
        const auto at=t.rawbuf.size();
        t.rawbuf.insert(t.rawbuf.end(),g.region+off,g.region+off+len);
        t.raws.emplace_back(at,static_cast<u32>(len));
        return RAW|(tid<<34)|idx;
    }
    [[gnu::noinline]] void ev_one(const EvRow& r) {
        const u64 ing=u64(r.ingest)-u64(g.ing_base), rel=u64(r.ts)-u64(g.ws);
        if(r.ingest==TS_BAD || ing>=ING_MAX*1000) sys::fallback("ingest_ts bad or out of range");
        const bool valid=r.uid!=ID_EMPTY && rel<g.wlen && r.kind<=2 && r.product!=PROD_EMPTY && (r.kind!=2 || r.price)
            && std::bit_cast<i64>(u64(r.ingest)-u64(r.ts))<=DAY_MS;
        u64 w0=UINT64_MAX,user=0;
        if(valid) {
            const u64 cat=r.price ? g.prod[std::min<std::size_t>(r.product,PBOUND)] : 0;
            u64 alias=0;
            const auto w=keyrec::unscramble(r.uid,g.ukey);
            if(r.uid<=keyrec::MASK && (w>>9)<g.ucap && (w&511)<NALIAS) {user=w>>9;alias=w&511;}
            else {
                const u64 tok=token(r.uid),idx=t.odd_users.size();
                if(idx>=(u64{1}<<22)) sys::fallback("too many odd-user rows");
                t.odd_users.push_back(tok); user=ODDU|(tid<<22)|idx;
            }
            w0=(rel<<33)|(u64(r.price)<<9)|(cat<<4)|alias;
        }
        if(r.eid<=keyrec::MASK) {
            if(auto v=keyrec::unscramble(r.eid,g.ekey);v<g.ecap) {
                t.ev.push(v>>BB,w0,(v&(BSIZE-1))|((ing/1000)<<16)|(user<<37)); return;
            }
        }
        t.odd_events.push_back({token(r.eid),r.ingest,w0,user});
    }
    EvBatch* ev() {return &eb;}
    [[gnu::noinline]] void ev_flush() {
        const auto n=std::exchange(eb.n,0); if(!n) return;
        t.n_ev_rows+=n;
        const EvP p{g.ing_base,g.ws,g.wlen,g.ukey,g.ekey,g.ucap,g.ecap,tb.prod.data(),t.ev.lines_ptr()};
        const auto [regular,special,bad]=u64(TS_BAD)-u64(p.ing_base)>=ING_MAX*1000 ? ev_vec<false>(eb,out,n,p) : ev_vec<true>(eb,out,n,p);
        if(bad) sys::fallback("ingest_ts bad or out of range");
        t.ev.push_many(regular,n,out.ob.data(),out.lp.data(),out.rec.data());
        for(auto m=special;m;m&=m-1) ev_one(eb.row(std::countr_zero(m)));
    }
    [[gnu::noinline]] void ck_one(const ClRow& r) {
        const auto rel=u64(r.ts)-u64(g.cbase);
        if(r.uid==ID_EMPTY || rel>=g.clen || r.campaign>=CBOUND || g.camp[r.campaign]==255) return;
        const auto key=(rel<<32)|(u64(r.campaign)<<4),cid=token(r.cid);
        const auto w=keyrec::unscramble(r.uid,g.ukey),u=w>>9,alias=w&511;
        if(r.uid<=keyrec::MASK && u<g.ucap && alias<NALIAS) {
            if(g.ccap) {
                if(r.cid<=keyrec::MASK) if(auto c=keyrec::unscramble(r.cid,g.ckey);c<g.ccap) {
                    if(g.cpart) t.us.push_indexed(u>>UB,c|((u&(UBSIZE-1))<<43),key|alias);
                    else ckix::store_one(out.pd,g.csr,c,key|alias,u);
                    return;
                }
                t.odd_clicks.push_back({r.uid,key,cid}); return;
            }
            t.us.push(u>>UB,cid|((u&(UBSIZE-1))<<43),key|alias);return;
        }
        t.odd_clicks.push_back({token(r.uid),key,cid});
    }
    CkBatch* ck() {return &cb;}
    [[gnu::noinline]] void ck_flush() {
        const auto n=std::exchange(cb.n,0); if(!n) return;
        t.n_ck_rows+=n;
        u64 special;
        if(g.ccap && !g.cpart) {
            const ckix::CkxP p{g.cbase,g.clen,g.ukey,g.ucap,g.ckey,g.ccap,tb.camp.data(),tb.crange,g.csr};
            special=ckix::ck_vec_ix(cb,out.pd,n,p);
        } else {
            const CkP p{g.cbase,g.clen,g.ukey,g.ucap,g.ckey,g.ccap,tb.camp.data(),tb.crange,t.us.lines_ptr()};
            const auto masks=tb.crange ? (g.ccap ? ck_vec<true,true>(cb,out,n,p) : ck_vec<true,false>(cb,out,n,p))
                                       : (g.ccap ? ck_vec<false,true>(cb,out,n,p) : ck_vec<false,false>(cb,out,n,p));
            if(g.ccap)t.us.push_indexed_many(masks.regular,n,out.ob.data(),out.lp.data(),out.rec.data());
            else t.us.push_many(masks.regular,n,out.ob.data(),out.lp.data(),out.rec.data());
            special=masks.special;
        }
        for(auto m=special;m;m&=m-1) ck_one(cb.row(std::countr_zero(m)));
    }
};
}
