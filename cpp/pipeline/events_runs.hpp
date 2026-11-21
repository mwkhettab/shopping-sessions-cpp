#pragma once
#include "core.hpp"
#include "sys.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <compare>
#include <cstdlib>
#include <numeric>
#include <string_view>
#include <tuple>
#include <vector>

namespace pipeline::events {
inline constexpr auto relaxed = std::memory_order_relaxed;
inline constexpr u32 M31 = 0x7fffffff, FLAG = 0x80000000, NONE = UINT32_MAX, DIG = FLAG;
inline constexpr std::size_t GUARD=16, RC_N=128, WL_CAP=4096, WL_RUN=1024, AL_CAP=4096;
inline std::atomic<std::size_t> DIG_MIN{4096};
inline u32 user_of(const Rec& r) { return u32((r.w1>>37)&(ODDU-1)); }
template<class T> void cpush(T& t,u64 pkey,u64 w0) { t.cx_ev[shard(pkey)].push_back({w0,pkey}); }
template<class G> auto cid_cmp(const G& g,u64 a,u64 b) {
    if (!((a|b)&RAW)) return enc36(a).first <=> enc36(b).first;
    std::vector<u8> sa,sb;
    g.id_str(a,'k',sa); g.id_str(b,'k',sb);
    return std::lexicographical_compare_three_way(sa.begin(),sa.end(),sb.begin(),sb.end());
}
template<class G> inline void claim_word(const G& g,std::size_t word,u64 bits) {
    if (bits && (g.claim[word].fetch_or(bits,relaxed)&bits)) sys::fallback("a user has two runs");
}
template<class G> inline void claim(const G& g,u32 u) { claim_word(g,u>>6,u64{1}<<(u&63)); }
inline u32 class_of(u16 tv,u32 bits) {
    const u32 mask=tv&MASK;
    return !(bits&~mask) ? mask : !(bits&(bits-1)) ? bits : 0;
}
template<class G,class T> void finish_simple(const G& g,T& t,u32 u,u32 klass,const core::St& st) {
    auto clicks=g.csr_range(u);
    if (!std::all_of(clicks.begin(),clicks.end(),[&](const auto& c){return (klass>>(c[0]&15))&1;})) {
        t.ck.clear();
        for (auto c:clicks) if ((klass>>(c[0]&15))&1) t.ck.push_back(c);
        clicks=t.ck;
    }
    t.core.finish(g,st,clicks,core::Name::Simple(u,klass));
}
template<class G,class T> void simple_run(const G& g,T& t,u32 u,std::span<Rec> recs,u16 tv) {
    auto add_complex=[&]{
        for (auto r:recs) t.cx_raw.push_back({r.w0,u});
        t.n_complex+=recs.size();
    };
    if (tv&COMPLEX) { add_complex(); return; }
    if (recs.empty()) return;
    claim(g,u);
    u32 bits=0;
    u64 prev=0;
    bool sorted=true;
    for (auto r:recs) {
        bits|=u32{1}<<(r.w0&15);
        const auto ts=r.w0>>33;
        sorted&=ts>=prev;
        prev=ts;
    }
    const u32 mask=tv&MASK;
    u32 klass=mask;
    if (bits&~mask) {
        if (!(bits&(bits-1))) klass=bits;
        else { t.mixed_users.push_back(u); add_complex(); return; }
    }
    if (!sorted) std::sort(recs.begin(),recs.end(),[](auto a,auto b){return a.w0<b.w0;});
    t.n_simple+=recs.size();
    auto st=t.core.begin(recs[0].w0);
    for (auto r:recs) t.core.feed(st,r.w0);
    finish_simple(g,t,u,klass,st);
}
template<class G,class T> void process_run(const G& g,T& t,u32 u,std::span<Rec> recs) {
    ++t.n_runs;
    const auto tv=g.utab[u];
    if ((tv&(COMPLEX|EXTRA))==EXTRA) {
        auto buf=std::vector<Rec>(recs.begin(),recs.end());
        for (auto w:g.extra.at(u)) buf.push_back({w,0});
        t.n_odd+=buf.size()-recs.size();
        simple_run(g,t,u,buf,tv);
    } else simple_run(g,t,u,recs,tv);
}

struct Pool {
    std::atomic<std::size_t> next{};
    std::size_t end{};
    template<class G> explicit Pool(const G& g) {
        const auto n=g.us_arena.used();
        const auto base=reinterpret_cast<std::size_t>(g.us_arena.alloc(0))-n;
        next=base;
        const char *env=std::getenv("XL_RECYCLE");
        end=base+(env && std::string_view(env)=="0" ? 0:n&~((std::size_t{2}<<20)-1));
    }
    u8* take(std::size_t len,std::size_t align) {
        auto a=next.load(relaxed);
        for (;;) {
            const auto s=(a+align-1)&~(align-1);
            if (s+len>end) return nullptr;
            if (next.compare_exchange_weak(a,s+len,relaxed)) return reinterpret_cast<u8*>(s);
        }
    }
    std::size_t left() const { const auto n=next.load(relaxed); return end>n?end-n:0; }
};
inline constexpr std::size_t r4k(std::size_t n) { return (n+4095)&~std::size_t{4095}; }
inline constexpr std::size_t r64(std::size_t n) { return (n+63)&~std::size_t{63}; }
struct Share { u8 *dense{},*hot{},*agg{},*aggp{},*slab{}; std::size_t slab_len{}; };
inline std::pair<std::size_t,std::size_t> fast_sizes(std::size_t nc,std::size_t nd) {
    constexpr auto n=BSIZE+GUARD+64;
    const auto dense=r64(16*(BSIZE+16))+r64(8*n)+8*r64(4*n);
    const auto hot=r64(2*(BSIZE/8+2))+2*r64(BSIZE/8+8)+r64(4*(BSIZE/16+2))
        +r64(2*(BSIZE/16+2))+r64(4*(BSIZE/16+2))+r64(2*(BSIZE/16+2))+r64(8*(CBOUND+2))
        +r64(64*(nc+2))+r64(4*(nc+2)*nd)+r64(256*RC_N)+2*r64(4*(WL_CAP+WL_RUN+64))+r64(4*(AL_CAP+64));
    return {dense,hot};
}
inline void slab_cols(core::Core& c,u8* slab,std::size_t len) {
    const auto r=(len-std::min(len,9*std::size_t{4096}))*5/131, p=r*2/5;
    if (!slab || r<4096) return;
    auto *at=slab;
    auto part=[&](std::size_t bytes){auto *q=at;at+=r4k(bytes);return q;};
    c.keys.set_slab(reinterpret_cast<u32*>(part(4*r)),r);
    c.day.set_slab(reinterpret_cast<i32*>(part(4*r)),r);
    c.ses.set_slab(reinterpret_cast<i32*>(part(4*r)),r);
    c.evs.set_slab(reinterpret_cast<i32*>(part(4*r)),r);
    c.rev.set_slab(reinterpret_cast<i32*>(part(4*r)),r);
    c.cat.set_slab(part(r),r);
    c.names.set_slab(part(9*p+64),9*p+64);
    c.names_off.set_slab(reinterpret_cast<u32*>(part(4*(p+1))),p+1);
    if (at>slab+len) sys::fallback("slab layout");
}
inline void slab_full(core::Core& c) {
    c.keys.slab_full();c.day.slab_full();c.ses.slab_full();c.evs.slab_full();
    c.rev.slab_full();c.cat.slab_full();c.names.slab_full();c.names_off.slab_full();
}
inline void slab_settle(core::Core& c) {
    auto tight=[](const auto& col){return col.cap()-std::min(col.len(),col.cap())<col.len()/64+4096;};
    if (c.day.in_slab() && tight(c.day)) {
        const auto n=c.day.len();
        c.keys.grow(n);c.day.grow(n);c.ses.grow(n);c.evs.grow(n);c.rev.grow(n);c.cat.grow(n);
    }
    if (c.names.in_slab() && tight(c.names)) c.names.grow(c.names.len());
    if (c.names_off.in_slab() && tight(c.names_off)) c.names_off.grow(c.names_off.len());
}
inline void scatter_chunk(const Rec* p,std::size_t n,Rec* slots,u64 gen,const i8* nextc) {
    auto put=[&](const Rec& r){
        auto& s=slots[r.w1&(BSIZE-1)];
        if (u16(s.w1)!=u16(r.w1+gen) || ((r.w1>>16)&ING_MAX)<((s.w1>>16)&ING_MAX)) {
#if defined(__x86_64__)
            const auto v=_mm_loadu_si128(reinterpret_cast<const __m128i*>(&r));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&s),_mm_add_epi64(v,_mm_set_epi64x(gen,0)));
#else
            s={r.w0,r.w1+gen};
#endif
        }
    };
    constexpr std::size_t link_off=4032;
    __builtin_prefetch(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(nextc)+link_off));
    const auto bulk=n&~std::size_t{3};
    for (std::size_t i=0;i!=bulk;i+=4) {
        __builtin_prefetch(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(nextc)+i*16));
        put(p[i]);put(p[i+1]);put(p[i+2]);put(p[i+3]);
    }
    for (std::size_t tail=0;tail<(n&3);++tail) put(p[bulk+tail]);
}
inline std::size_t kept_block(const u16* blkk,std::size_t a) {
    std::size_t lo=0,hi=BSIZE/8;
    while (hi-lo>1) {const auto m=(lo+hi)/2;if (blkk[m]<=a) lo=m;else hi=m;}
    return lo;
}
template<class F> inline void for_kept(const Rec* slots,const u16* blkk,const u8* keepm,std::size_t a,std::size_t n,F f) {
    if (!n) return;
    auto blk=kept_block(blkk,a);
    u32 m=keepm[blk];
    for (auto i=std::size_t(blkk[blk]);i<a;++i) m&=m-1;
    for (;;) {
        while (m) {f(slots[8*blk+std::countr_zero(m)].w0);m&=m-1;if (!--n) return;}
        if (++blk>=BSIZE/8) sys::fallback("kept records");
        m=keepm[blk];
    }
}
template<class T> [[gnu::noinline]] void odd_records(T& t,const Rec* slots,std::size_t blk,u8 m) {
    while (m) {
        const auto r=slots[8*blk+std::countr_zero(m)];
        m&=m-1;
        t.cx_raw.push_back({r.w0,ODDU|user_of(r)});++t.n_odd;
    }
}
struct DigRun {bool first;u32 user;std::size_t a,e,s0,s1;};
inline constexpr std::size_t EP_WORDS=1<<16, EP_BYTES=8*EP_WORDS+4096;
static_assert(BSIZE<=EP_WORDS);
struct Edge {
    std::vector<u64*> pieces;
    std::size_t used{EP_WORDS},spare{};
    Pool* pool{};
    static Edge* start(std::size_t tid,Pool* pool);
    [[gnu::noinline]] void piece(std::size_t n) {
        if (n>EP_WORDS) sys::fallback("edge run over a piece");
        if ((pieces.size()+1)*EP_WORDS>UINT32_MAX) sys::fallback("edge store over 4G records");
        if (spare) {--spare;pieces.push_back(pieces.back()+EP_WORDS);}
        else {
            auto p=pool ? pool->take(EP_BYTES,4096) : nullptr;
            if (!p) {p=sys::anon(2<<20);spare=((2<<20)-4096)/(8*EP_WORDS)-1;}
            pieces.push_back(reinterpret_cast<u64*>(p));
        }
        used=0;
    }
    std::pair<u64*,Seg> seg(std::size_t tid,u32 user,std::size_t n) {
        if (used+n>EP_WORDS || pieces.empty()) piece(n);
        Seg s{u32(tid),u32((pieces.size()-1)*EP_WORDS+used),u32(n),user};
        auto p=pieces.back()+used;
        used+=n;
        return {p,s};
    }
};
inline std::array<Edge,64> EDGE;
inline Edge* Edge::start(std::size_t tid,Pool* p) {
    if (tid>=EDGE.size()) sys::fallback("more than 64 threads");
    EDGE[tid].pool=p;return &EDGE[tid];
}
inline std::span<const u64> seg_words(const Seg& s,std::size_t n) {
    if (s.tid>=EDGE.size()) sys::fallback("edge segment");
    const auto& e=EDGE[s.tid];
    const auto pi=s.off/EP_WORDS, at=s.off%EP_WORDS;
    if (pi>=e.pieces.size() || at+n>EP_WORDS || (pi+1==e.pieces.size() && at+n>e.used)) sys::fallback("edge segment");
    return {e.pieces[pi]+at,n};
}
inline std::span<const u64> seg_w0(const Seg& s) {
    if (s.len&DIG) sys::fallback("edge segment");
    return seg_words(s,s.len);
}
template<class Fast> [[gnu::noinline]] Seg digest_put(std::size_t tid,Fast& fx,const DigRun& d,const u64* q,
                                                       const u32* sidx,const u32* sts,std::size_t np) {
    const auto ns=d.s1-d.s0;
    std::size_t pa=0;
    while (pa<np && fx.p_sid[pa]<d.s0) ++pa;
    auto pb=pa;
    while (pb<np && fx.p_sid[pb]<d.s1) ++pb;
    const auto words=3+2*ns+pb-pa;
    if (words>EP_WORDS || !ns) sys::fallback("digest size");
    auto [p,seg]=fx.edge->seg(tid,d.user,words);
    seg.len|=DIG;
    p[0]=u64(ns)|(u64(pb-pa)<<32);
    p[1]=u64(d.e-d.a)|((u64{1}<<((q[d.a]>>32)&15))<<32);
    p[2]=u32(q[d.e-1])|(q[d.a]<<32);
    std::size_t total=0;
    for (std::size_t i=0;i<ns;++i) {
        const auto s=d.s0+i;
        const auto ra=sidx[s]&M31,rb=sidx[s+1]&M31;
        if (rb<=ra || rb>d.e) sys::fallback("digest sessions");
        total+=rb-ra;p[3+2*i]=sts[s]|(u64(rb-ra)<<32);p[4+2*i]=u32(q[rb-1]);
    }
    if (total!=d.e-d.a || (sidx[d.s0]&M31)!=d.a) sys::fallback("digest sessions");
    for (auto j=pa;j<pb;++j) p[3+2*ns+j-pa]=(u64(fx.p_sid[j]-d.s0)<<32)|fx.p_w[j];
    return seg;
}
struct Digest {std::span<const u64> sess,purch;u64 count;u32 bits;u64 first,last;};
inline Digest digest_get(const Seg& s) {
    const auto w=seg_words(s,s.len&~DIG);
    if (w.size()<3) sys::fallback("edge segment");
    const auto ns=std::size_t(u32(w[0])),np=std::size_t(w[0]>>32);
    if (!ns || 3+2*ns+np!=w.size()) sys::fallback("edge segment");
    return {w.subspan(3,2*ns),w.subspan(3+2*ns),u32(w[1]),u32(w[1]>>32),w[2]>>32,u32(w[2])};
}
inline Seg edge_copy(Edge* ed,std::size_t tid,u32 u,std::span<const Rec> recs) {
    auto [p,seg]=ed->seg(tid,u,recs.size());
    for (auto r:recs) *p++=r.w0;
    return seg;
}
template<class G> void stage3a_old(const G& g,std::size_t tid) {
    auto& t=g.t(tid);
    t.slots.assign(BSIZE,{UINT64_MAX,UINT64_MAX});
    auto ed=Edge::start(tid,nullptr);
    auto slots=t.slots.data();
    using clock=std::chrono::steady_clock;
    for (;;) {
        const auto b=g.next.fetch_add(1,relaxed);
        if (b>=g.nb) break;
        const auto ta=clock::now();
        std::memset(slots,255,BSIZE*sizeof(Rec));
        bool any=false;
        for (std::size_t th=0;th<g.nt;++th) g.t(th).ev.for_each(b,[&](auto records){
            any=true;
            for (auto r:records) {
                auto& s=slots[r.w1&(BSIZE-1)];
                if (((r.w1>>16)&ING_MAX)<((s.w1>>16)&ING_MAX)) s=r;
            }
        });
        auto& info=g.edges[b];
        info.n=0;
        if (!any) continue;
        const auto tb=clock::now();
        std::size_t k=0;
        for (std::size_t s=0;s<BSIZE;++s) {
            const Rec r=slots[s];
            if ((r.w0>>33)==INVALID_TS) continue;
            if (r.w1>>63) {
                t.cx_raw.push_back({r.w0,ODDU|user_of(r)});++t.n_odd;
            } else slots[k++]=r;
        }
        const auto tc=clock::now();
        auto recs=std::span<Rec>{slots,k};
        std::size_t i=0;
        if (k) {
            const auto u=user_of(recs[0]);
            std::size_t j=1;
            while (j<k && user_of(recs[j])==u) ++j;
            info.first=edge_copy(ed,tid,u,recs.first(j));info.n=1;
            i=j;
        }
        while (i<k) {
            const auto u=user_of(recs[i]);const auto tv=g.utab[u];
            auto j=i;
            if (tv&(COMPLEX|EXTRA)) {
                while(j<k && user_of(recs[j])==u) ++j;
                if (j==k) {info.last=edge_copy(ed,tid,u,recs.subspan(i,j-i));info.n=2;}
                else process_run(g,t,u,recs.subspan(i,j-i));
                i=j;continue;
            }
            auto st=t.core.begin(recs[i].w0);
            u32 bits=0;u64 prev=0;bool sorted=true;
            for (;j<k && user_of(recs[j])==u;++j) {
                const auto w=recs[j].w0;
                bits|=u32{1}<<(w&15);sorted&=(w>>33)>=prev;prev=w>>33;
                t.core.feed(st,w);
            }
            if (j==k) {
                t.core.abort(st);info.last=edge_copy(ed,tid,u,recs.subspan(i,j-i));info.n=2;break;
            }
            const auto klass=class_of(tv,bits);
            if (!klass || !sorted) {
                t.core.abort(st);process_run(g,t,u,recs.subspan(i,j-i));
            } else {
                ++t.n_runs;t.n_simple+=j-i;claim(g,u);finish_simple(g,t,u,klass,st);
            }
            i=j;
        }
        const auto td=clock::now();
        t.prof[0]+=std::chrono::duration_cast<std::chrono::nanoseconds>(tb-ta).count();
        t.prof[1]+=std::chrono::duration_cast<std::chrono::nanoseconds>(tc-tb).count();
        t.prof[2]+=std::chrono::duration_cast<std::chrono::nanoseconds>(td-tc).count();
    }
}

template<class G> void stage3b(const G& g,std::size_t tid) {
    auto& t=g.t(tid);
    bool released=false;
    for (;;) {
        const auto i=g.next.fetch_add(1,relaxed);
        if (i>=g.joined.size()) break;
        if (i>=g.nt && !released) {g.ev_arena.release_part(tid,g.nt);released=true;}
        const auto& segs=g.joined[i];const auto u=segs[0].user;
        const auto tv=g.utab[u];
        if (!(tv&COMPLEX)) {
            std::span<const u64> extra;
            if (tv&EXTRA) extra=g.extra.at(u);
            const auto first=(segs[0].len&DIG) ? digest_get(segs[0]).first<<33 : seg_w0(segs[0])[0];
            auto st=t.core.begin(!extra.empty() && extra[0]<first ? extra[0]:first);
            u32 bits=0;u64 prev=0,n=0;bool sorted=true,has_dig=false;
            std::size_t xi=0;
            for (auto& s:segs) {
                if (s.len&DIG) {
                    auto d=digest_get(s);has_dig=true;bits|=d.bits;sorted&=d.first>=prev;
                    bits|=t.core.feed_digest(st,d.sess,d.purch,extra,xi);
                    prev=d.last;n+=d.count;continue;
                }
                auto ws=seg_w0(s);std::size_t a=0;
                auto feed=[&](std::span<const u64> w){
                    auto [b,uns]=t.core.feed_slice(st,w,prev);
                    bits|=b;sorted&=!uns;prev=w.back()>>33;
                };
                while (xi<extra.size()) {
                    const auto e=extra[xi];
                    const auto p=std::find_if(ws.begin()+a,ws.end(),[&](u64 w){return e<w;});
                    if (p==ws.end()) break;
                    const auto j=std::size_t(p-ws.begin());
                    if (j>a) feed(ws.subspan(a,j-a));
                    bits|=u32{1}<<(e&15);t.core.feed(st,e);++xi;a=j;
                }
                if (a<ws.size()) feed(ws.subspan(a));
                n+=s.len;
            }
            for (;xi<extra.size();++xi) {
                bits|=u32{1}<<(extra[xi]&15);t.core.feed(st,extra[xi]);
            }
            const auto klass=class_of(tv,bits);
            if (klass && sorted) {
                ++t.n_runs;t.n_simple+=n;t.n_odd+=extra.size();
                claim(g,u);finish_simple(g,t,u,klass,st);continue;
            }
            if (has_dig) sys::fallback("digest of a user that needs its records");
            t.core.abort(st);
        }
        auto buf=std::move(t.runbuf);buf.clear();
        for (auto& s:segs) for (auto w:seg_w0(s)) buf.push_back({w,u64(u)<<37});
        process_run(g,t,u,buf);t.runbuf=std::move(buf);
        if (!released) {g.ev_arena.release_part(tid,g.nt);released=true;}
    }
    if (!released) g.ev_arena.release_part(tid,g.nt);
}
template<class G> void stitch(G& g) {
    std::vector<std::vector<Seg>> joined;
    std::vector<Seg> cur;
    for (std::size_t b=0;b<g.nb;++b) {
        const auto& info=g.edges[b];
        if (!info.n) continue;
        if (!cur.empty() && cur[0].user!=info.first.user) {joined.push_back(std::move(cur));cur.clear();}
        cur.push_back(info.first);
        if (info.n==2) {joined.push_back(std::move(cur));cur.clear();cur.push_back(info.last);}
    }
    if (!cur.empty()) joined.push_back(std::move(cur));
    auto weight=[](const auto& segs){u64 n=0;for (auto s:segs)n+=s.len;return n;};
    std::stable_sort(joined.begin(),joined.end(),[&](const auto& a,const auto& b){return weight(a)>weight(b);});
    g.joined=std::move(joined);
}
}
