#pragma once
#include "ckix.hpp"

namespace pipeline::parse {
struct alignas(64) Out {
    std::array<std::array<u64,2>,NB> rec{};
    std::array<u64,NB> ob{}, lp{};
    ckix::Pend pd;
};
struct EvP {
    i64 ing_base, ws;
    u64 wlen, ukey, ekey, ucap, ecap;
    const u8* ptab;
    const u8* lines;
};
struct CkP {
    i64 cbase;
    u64 clen,ukey,ucap,ckey,ccap;
    const u8* ctab;
    std::optional<std::pair<u64,u64>> crange;
    const u8* lines;
};
struct EvMasks { u64 regular, special; bool bad_ingest; };
struct CkMasks { u64 regular, special; };

template<bool CheckBad> inline EvMasks ev_scalar(const EvBatch& eb, Out& out, std::size_t n,const EvP& p) {
    u64 regular=0;
    bool bad=false;
    for(std::size_t i=0;i<n;++i) {
        const u64 ing=u64(eb.ing[i])-u64(p.ing_base),rel=u64(eb.ts[i])-u64(p.ws);
        bad |= (CheckBad && eb.ing[i]==TS_BAD) || ing>=ING_MAX*1000;
        const u64 price=(eb.pp[i]>>32)&0xffffff;
        const bool valid=eb.uid[i]!=ID_EMPTY && rel<p.wlen && !(eb.pp[i]&PP_INVALID)
            && std::bit_cast<i64>(u64(eb.ing[i])-u64(eb.ts[i]))<=DAY_MS;
        const u64 w=keyrec::unscramble(eb.uid[i],p.ukey),user=w>>9, alias=w&511;
        const bool normal=eb.uid[i]<=keyrec::MASK && user<p.ucap && alias<NALIAS;
        const u64 event=keyrec::unscramble(eb.eid[i],p.ekey);
        const bool evok=eb.eid[i]<=keyrec::MASK && event<p.ecap;
        if(evok && (!valid || normal)) regular |= u64{1}<<i;
        u64 word0=valid ? (rel<<33)|(price<<9)|alias : UINT64_MAX;
        if(valid && price) word0 |= u64(p.ptab[std::min<std::size_t>(u32(eb.pp[i]),PBOUND)])<<4;
        // This multiply is exact on the supported ingest interval.
        const u64 ing_s=(u64(u32(ing))*274877907ull)>>38;
        out.rec[i]={word0,(event&(BSIZE-1))|(ing_s<<16)|((valid?user:0)<<37)};
        out.ob[i]=event>>BB;
        out.lp[i]=u64(std::uintptr_t(p.lines))+(out.ob[i]<<6);
    }
    return {regular,~regular&active_mask(n),bad};
}
template<bool Ranged,bool ModeA> inline CkMasks ck_scalar(const CkBatch& cb, Out& out, std::size_t n,const CkP& p) {
    u64 regular=0,keep=0;
    for(std::size_t i=0;i<n;++i){
        const u64 rel=u64(cb.ts[i])-u64(p.cbase),cid=cb.cid[i],uid=cb.uid[i],camp=cb.camp[i];
        const bool incsv=Ranged ? camp-p.crange->first<=p.crange->second : camp<CBOUND && p.ctab[camp]!=255;
        const bool valid=incsv && uid!=ID_EMPTY && rel<p.clen;
        if(valid) keep |= u64{1}<<i;
        const u64 w=keyrec::unscramble(uid,p.ukey),user=w>>9,alias=w&511;
        const u64 id=ModeA ? keyrec::unscramble(cid,p.ckey) : cid;
        if(valid && uid<=keyrec::MASK && user<p.ucap && alias<NALIAS && cid<=keyrec::MASK && (!ModeA || id<p.ccap))
            regular |= u64{1}<<i;
        out.rec[i]={id|((user&(UBSIZE-1))<<43),(rel<<32)|(camp<<4)|alias};
        out.ob[i]=user>>UB;
        out.lp[i]=u64(std::uintptr_t(p.lines))+(out.ob[i]<<6);
    }
    return {regular,keep & ~regular};
}

#if defined(__x86_64__)
template<bool CheckBad,bool Ifma>
PIPELINE_ROUTING_X64 inline EvMasks ev_packet(const EvBatch& eb,Out& out,std::size_t n,const EvP& p) {
    const auto user_key=splat8(p.ukey&keyrec::MASK), event_key=splat8(p.ekey&keyrec::MASK);
    const auto even=_mm512_setr_epi64(0,8,1,9,2,10,3,11),odd=_mm512_setr_epi64(4,12,5,13,6,14,7,15);
    const auto zero=_mm512_setzero_si512(), ones=splat8(UINT64_MAX);
    u64 badb=0, purchases=0, regular=0;
    for(std::size_t j=0;j<n;j+=8) {
        auto arrival=_mm512_load_si512(eb.ing.data()+j);
        auto ing=_mm512_sub_epi64(arrival,splat8(u64(p.ing_base)));
        auto bad=_mm512_cmpge_epu64_mask(ing,splat8(ING_MAX*1000));
        if constexpr(CheckBad) bad |= _mm512_cmpeq_epi64_mask(arrival,splat8(u64(TS_BAD)));
        auto ins=_mm512_srli_epi64(mulu32_8(ing,splat8(274877907)),38);
        auto ts=_mm512_load_si512(eb.ts.data()+j),rel=_mm512_sub_epi64(ts,splat8(u64(p.ws)));
        auto eid=_mm512_load_si512(eb.eid.data()+j),uid=_mm512_load_si512(eb.uid.data()+j);
        auto pp=_mm512_load_si512(eb.pp.data()+j),price=_mm512_and_si512(_mm512_srli_epi64(pp,32),splat8(0xffffff));
        auto valid=_mm512_cmpneq_epi64_mask(uid,splat8(ID_EMPTY));
        valid=_mm512_mask_cmplt_epu64_mask(valid,rel,splat8(p.wlen));
        valid=_mm512_mask_cmpgt_epi64_mask(valid,pp,ones);
        valid=_mm512_mask_cmple_epi64_mask(valid,_mm512_sub_epi64(arrival,ts),splat8(DAY_MS));
        auto w=decode8<Ifma>(uid,user_key);
        auto user=_mm512_srli_epi64(w,9), alias=_mm512_and_si512(w,splat8(511));
        auto normal=_mm512_cmplt_epu64_mask(uid,splat8(u64{1}<<40));
        normal=_mm512_mask_cmplt_epu64_mask(normal,user,splat8(p.ucap));
        normal=_mm512_mask_cmplt_epu64_mask(normal,alias,splat8(NALIAS));
        auto pm=_mm512_mask_cmpneq_epi64_mask(valid,price,zero);
        auto word0=_mm512_or_si512(_mm512_or_si512(_mm512_slli_epi64(rel,33),_mm512_slli_epi64(price,9)),alias);
        word0=_mm512_mask_blend_epi64(valid,ones,word0);
        auto userv=_mm512_maskz_mov_epi64(valid,user);
        auto v=decode8<Ifma>(eid,event_key);
        auto evok=_mm512_mask_cmplt_epu64_mask(_mm512_cmplt_epu64_mask(eid,splat8(u64{1}<<40)),v,splat8(p.ecap));
        auto word1=_mm512_or_si512(_mm512_and_si512(v,splat8(BSIZE-1)),
                     _mm512_or_si512(_mm512_slli_epi64(ins,16),_mm512_slli_epi64(userv,37)));
        auto ok=__mmask8(~(valid & ~normal) & evok);
        auto bucket=_mm512_srli_epi64(v,BB);
        _mm512_store_si512(out.rec.data()+j,_mm512_permutex2var_epi64(word0,even,word1));
        _mm512_store_si512(out.rec.data()+j+4,_mm512_permutex2var_epi64(word0,odd,word1));
        _mm512_store_si512(out.ob.data()+j,bucket);
        _mm512_store_si512(out.lp.data()+j,_mm512_add_epi64(_mm512_slli_epi64(bucket,6),splat8(std::uintptr_t(p.lines))));
        badb |= u64(bad)<<j;
        purchases |= u64(pm)<<j;
        regular |= u64(ok)<<j;
    }
    auto active=active_mask(n);
    purchases &= active;
    while(purchases) {
        const auto i=std::countr_zero(purchases);
        purchases &= purchases-1;
        const auto prod=std::min<std::size_t>(u32(eb.pp[i]),PBOUND);
        out.rec[i][0] |= u64(p.ptab[prod])<<4;
    }
    return {regular & active,~regular & active,(badb & active)!=0};
}

template<bool Ranged,bool ModeA>
PIPELINE_ROUTING_X64 inline CkMasks ck_packet(const CkBatch& cb,Out& out,std::size_t n,const CkP& p) {
    const auto user_key=splat4(p.ukey&keyrec::MASK),click_key=splat4(p.ckey&keyrec::MASK);
    const auto [lo,span]=p.crange.value_or(std::pair<u64,u64>{});
    const auto even=_mm256_setr_epi64x(0,4,1,5),odd=_mm256_setr_epi64x(2,6,3,7);
    u64 regular=0, kept=0;
    for(std::size_t j=0;j<n;j+=4) {
        auto rel=_mm256_sub_epi64(_mm256_load_si256(reinterpret_cast<const __m256i*>(cb.ts.data()+j)),splat4(u64(p.cbase)));
        auto cid=_mm256_load_si256(reinterpret_cast<const __m256i*>(cb.cid.data()+j));
        auto uid=_mm256_load_si256(reinterpret_cast<const __m256i*>(cb.uid.data()+j));
        auto camp=_mm256_load_si256(reinterpret_cast<const __m256i*>(cb.camp.data()+j));
        __mmask8 present;
        if constexpr(Ranged) {
            present=_mm256_cmple_epu64_mask(_mm256_sub_epi64(camp,splat4(lo)),splat4(span));
        } else {
            auto inbounds=_mm256_cmplt_epu64_mask(camp,splat4(CBOUND));
            auto ff=splat4(255);
            auto ch=_mm256_and_si256(_mm256_mmask_i64gather_epi64(ff,inbounds,camp,p.ctab,1),ff);
            present=_mm256_mask_cmpneq_epi64_mask(inbounds,ch,ff);
        }
        auto keep=_mm256_mask_cmplt_epu64_mask(_mm256_mask_cmpneq_epi64_mask(present,uid,splat4(ID_EMPTY)),rel,splat4(p.clen));
        auto w=decode4(uid,user_key),user=_mm256_srli_epi64(w,9),alias=_mm256_and_si256(w,splat4(511));
        auto ok=_mm256_mask_cmplt_epu64_mask(keep,uid,splat4(u64{1}<<40));
        ok=_mm256_mask_cmplt_epu64_mask(ok,user,splat4(p.ucap));
        ok=_mm256_mask_cmplt_epu64_mask(ok,alias,splat4(NALIAS));
        ok=_mm256_mask_cmplt_epu64_mask(ok,cid,splat4(u64{1}<<40));
        auto idw=cid;
        if constexpr(ModeA){
            idw=decode4(cid,click_key);
            ok=_mm256_mask_cmplt_epu64_mask(ok,idw,splat4(p.ccap));
        }
        auto key=_mm256_or_si256(_mm256_slli_epi64(rel,32),_mm256_slli_epi64(camp,4));
        auto o0=_mm256_or_si256(idw,_mm256_slli_epi64(_mm256_and_si256(user,splat4(UBSIZE-1)),43));
        auto o1=_mm256_or_si256(key,alias),bucket=_mm256_srli_epi64(user,UB);
        _mm256_store_si256(reinterpret_cast<__m256i*>(out.rec.data()+j),_mm256_permutex2var_epi64(o0,even,o1));
        _mm256_store_si256(reinterpret_cast<__m256i*>(out.rec.data()+j+2),_mm256_permutex2var_epi64(o0,odd,o1));
        _mm256_store_si256(reinterpret_cast<__m256i*>(out.ob.data()+j),bucket);
        _mm256_store_si256(reinterpret_cast<__m256i*>(out.lp.data()+j),_mm256_add_epi64(_mm256_slli_epi64(bucket,6),splat4(std::uintptr_t(p.lines))));
        regular |= u64(ok)<<j;
        kept |= u64(keep)<<j;
    }
    const auto active=active_mask(n);
    return {regular & active, kept & ~regular & active};
}
#endif

template<bool CheckBad> inline EvMasks ev_vec(const EvBatch& eb,Out& out,std::size_t n,const EvP& p) {
#if defined(__x86_64__)
    if(routing_x64()) {
        if(__builtin_cpu_supports("avx512ifma")) return ev_packet<CheckBad,true>(eb,out,n,p);
        return ev_packet<CheckBad,false>(eb,out,n,p);
    }
#endif
    return ev_scalar<CheckBad>(eb,out,n,p);
}
template<bool Ranged,bool ModeA>
inline CkMasks ck_vec(const CkBatch& cb,Out& out,std::size_t n,const CkP& p) {
#if defined(__x86_64__)
    if(routing_x64()) return ck_packet<Ranged,ModeA>(cb,out,n,p);
#endif
    return ck_scalar<Ranged,ModeA>(cb,out,n,p);
}
}
