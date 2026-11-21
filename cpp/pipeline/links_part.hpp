#pragma once
#include "ckix.hpp"
#include "links_sort.hpp"
#include "links_uf.hpp"
#include "parse_part.hpp"
#include <tuple>

namespace pipeline::links {
using Range=std::pair<std::size_t,std::size_t>;
using Piece=std::pair<std::size_t,i64>;
using Cx=std::vector<std::tuple<u32,u8,u8>>;
inline u64 elapsed_ns(sys::Instant::time_point start){return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(sys::Instant::now()-start).count());}
struct Cand {u32 u,a;u64 key,tok;};
struct Fix {u32 user;u64 w0;std::size_t slot;u64 tok;};
inline constexpr std::size_t HEAVY_DIV=4,SORT_UNITS=64+UBSIZE/64-1;
struct Heavy {
    std::size_t ub;
    std::vector<std::vector<u32>> cnt;
    std::vector<ScratchVec<u64>> links;
    std::vector<Range> mm; std::vector<Piece> pieces;
    Cx cx;
    std::atomic<std::size_t> counted{},scat_next{},scat_fin{},piece_next{},piece_fin{},sort_next{},sort_fin{};
    std::atomic_bool mid_taken{},mid_done{},fin_taken{},fin_done{};
    explicit Heavy(std::size_t u):ub(u){}
};
struct Plan {
    std::vector<std::unique_ptr<Heavy>> heavy;
    std::vector<u32> normal;
    std::vector<std::size_t> base;
    std::vector<std::vector<Cand>> cand;
    std::vector<Range> range;
    std::mutex fixes_lock; std::vector<Fix> fixes;
    std::atomic<std::size_t> next{},fin{};
};
inline std::unique_ptr<Plan> plan;
inline std::atomic_bool READY{};
inline std::atomic<std::size_t> US_FIN{};
inline std::atomic<u64> CEND{};
template<class G> inline std::span<u16> utab_of(const G& g,std::size_t ub){return {g.utab+(ub<<UB),UBSIZE};}
template<class G> inline std::span<u32> off_of(const G& g,std::size_t ub){return {g.csr_off+(ub<<UB)+ub,UBSIZE+1};}
template<class G> inline void mark_complex(const G& g,u64 t){if(!(t&RAW))if(auto u=g.normal(t))g.utab[u->first]|=COMPLEX;}
inline std::size_t local_user(u64 w){return (w>>43)&(UBSIZE-1);}
template<class T> inline void link_rec(T& t,std::span<u16> tab,u64 w) {
    auto u=local_user(w);const u16 bits=u16((1u<<((w>>4)&15))|(1u<<(w&15))),m=tab[u]&MASK;
    if(!m || (m&bits)) tab[u]|=bits;else t.pend.emplace_back(u32(u),bits);
}
template<class T> inline bool links_done(T& t,std::span<u16> tab,std::size_t u0) {
    if(!t.pend.empty()) {
        bool changed;
        do {changed=false;for(const auto& [u,b]:t.pend){const u16 m=tab[u]&MASK;if((m&b) && (m|b)!=m) {tab[u]|=b;changed=true;}}} while(changed);
        for(const auto& [u,b]:t.pend)if(!(tab[u]&b))tab[u]|=COMPLEX;
    }
    bool any=false;for(std::size_t u=0;u<tab.size();++u)if(tab[u]&COMPLEX){t.cx_users.push_back(u32(u0+u));any=true;}
    return any;
}
template<class T,class Fn> inline void process_links(T& t,std::span<u16> tab,std::size_t u0,Fn&& visit,Cx& cx){
    t.pend.clear();visit([&](u64 w){link_rec(t,tab,w);});
    if(links_done(t,tab,u0))visit([&](u64 w){const auto u=local_user(w);if(tab[u]&COMPLEX)cx.emplace_back(u32(u0+u),u8((w>>4)&15),u8(w&15));});
}
inline void count_chunk(std::span<const Rec> recs,std::vector<u32>& cnt,ScratchVec<u64>& lk) {
    const auto n=lk.size();lk.resize(n+recs.size()+1);auto j=n;
    for(const auto r:recs){const auto link=r.w0>>63;++cnt[local_user(r.w0)|(link<<UB)];lk[j]=r.w0;j+=std::size_t(link);}
    lk.resize(j);
}
inline void scatter_chunk(std::span<const Rec> recs,std::vector<u32>& pos,Click* csr) {
    for(auto r:recs){const auto flag=r.w0>>63;auto& pp=pos[local_user(r.w0)|(flag<<UB)];const auto at=pp;pp+=1-u32(flag);csr[at]={r.w1,r.w0&((RAW<<1)-1)};}
}
template<class G> inline void prelude_part(const G& g) {
    auto& p=*plan;
    for(std::size_t i=0;i<g.nt;++i)for(auto [a,b]:g.t(i).gen_edges){mark_complex(g,a);mark_complex(g,b);}
    std::size_t at=0;p.base.reserve(g.nub+1);
    for(std::size_t ub=0;ub<g.nub;++ub){p.base.push_back(at);for(std::size_t th=0;th<g.nt;++th)at+=g.t(th).us.count(ub);}
    p.base.push_back(at);if(at>=UINT32_MAX)sys::fallback("too many clicks");if(at+16>g.csr_cap)sys::fallback("CSR capacity");
    for(std::size_t ub=0;ub<g.nub;++ub){
        if(p.base[ub+1]-p.base[ub]>std::max<std::size_t>(at/HEAVY_DIV,1<<17)){
            auto h=std::make_unique<Heavy>(ub);h->cnt.resize(g.nt);h->links.resize(g.nt);p.heavy.push_back(std::move(h));
        } else p.normal.push_back(u32(ub));
    }
}
template<class G,class T> inline void bucket(const G& g,T& t,Scratch& sc,std::size_t ub) {
    const auto start=sys::Instant::now();
    auto& p=*plan;auto& cnt=t.cnt;auto& lk=sc.lk;
    cnt.assign(2*UBSIZE,0);lk.clear();std::size_t seen=0;
    for(std::size_t th=0;th<g.nt;++th)g.t(th).us.for_each(ub,[&](auto rows){seen+=rows.size();count_chunk(rows,cnt,lk);});
    const auto total=p.base[ub+1]-p.base[ub];if(seen!=total)sys::fallback("click partition count");
    u64 d1=0;bool first=true;
    process_links(t,utab_of(g,ub),ub<<UB,[&](auto f){for(u64 w:lk)f(w);if(first){d1=elapsed_ns(start);first=false;}},t.cx_edges);
    auto d2=d1;
    if(total){
        auto off=off_of(g,ub);u32 at=u32(p.base[ub]);
        for(std::size_t ul=0;ul<UBSIZE;++ul){off[ul]=at;at+=cnt[ul];cnt[ul]=off[ul];}
        off[UBSIZE]=at;if(at-p.base[ub]+lk.size()!=total)sys::fallback("click partition count");
        std::fill(cnt.begin()+UBSIZE,cnt.end(),at);
        for(std::size_t th=0;th<g.nt;++th)g.t(th).us.for_each(ub,[&](auto rows){scatter_chunk(rows,cnt,g.csr);});
        d2=elapsed_ns(start);sort_users(g,off.data(),0,UBSIZE,sc);
    }
    const auto el=elapsed_ns(start);t.prof[5]+=d1;t.prof[6]+=d2;t.prof[3]+=el;t.prof[4]=std::max(t.prof[4],el);
}
template<class G> inline void heavy_count(const G& g,Heavy& h,std::size_t th) {
    auto& cnt=h.cnt[th];auto& lk=h.links[th];cnt.assign(2*UBSIZE,0);lk.clear();std::size_t seen=0;
    g.t(th).us.for_each(h.ub,[&](auto rows){seen+=rows.size();count_chunk(rows,cnt,lk);});
    if(seen!=g.t(th).us.count(h.ub))sys::fallback("click partition count");
}
template<class G,class T> inline void heavy_links(const G& g,T& t,Heavy& h) {
    auto ub=h.ub;process_links(t,utab_of(g,ub),ub<<UB,[&](auto f){for(const auto& group:h.links)for(auto w:group)f(w);},h.cx);
    auto off=off_of(g,ub);auto at=u32(plan->base[ub]);
    for(std::size_t u=0;u<UBSIZE;++u){off[u]=at;for(auto& c:h.cnt)at+=c[u];}off[UBSIZE]=at;
}
template<class G,class T> inline void heavy_scatter(const G& g,T& t,Heavy& h,std::size_t th) {
    auto& pos=t.cnt;pos.resize(2*UBSIZE);auto at=u32(plan->base[h.ub]);
    for(std::size_t u=0;u<UBSIZE;++u)for(std::size_t j=0;j<g.nt;++j){if(j==th)pos[u]=at;at+=h.cnt[j][u];}
    std::size_t spare=at,links=0;for(std::size_t j=0;j<g.nt;++j){const auto n=h.links[j].size();if(j<th)spare+=n;links+=n;}
    if(at+links!=plan->base[h.ub+1])sys::fallback("click partition count");
    std::fill(pos.begin()+UBSIZE,pos.end(),u32(spare));g.t(th).us.for_each(h.ub,[&](auto rows){scatter_chunk(rows,pos,g.csr);});
}
inline std::size_t next_unit(std::atomic<std::size_t>& next,std::size_t n){if(next.load()>=n)return n;return next.fetch_add(1);}
template<class G> inline bool sort_step(const G& g,Heavy& h,Scratch& sc) {
    const auto j=next_unit(h.sort_next,SORT_UNITS);if(j>=SORT_UNITS)return false;
    const auto a=j<64 ? j : (j-63)*64,b=j<64 ? j+1 : (j-62)*64;
    sort_users(g,off_of(g,h.ub).data(),a,b,sc);h.sort_fin.fetch_add(1);return true;
}
template<class G,class T> inline bool job_part(const G& g,T& t,Scratch& sc) {
    auto& p=*plan;
    for(auto& hp:p.heavy){auto& h=*hp;if(h.counted.load()<g.nt)continue;
        if(!h.mid_taken.load() && !h.mid_taken.exchange(true)){heavy_links(g,t,h);h.mid_done.store(true);return true;}
        auto j=next_unit(h.scat_next,g.nt);if(j<g.nt){heavy_scatter(g,t,h,j);h.scat_fin.fetch_add(1);return true;}
        if(h.scat_fin.load()==g.nt && h.mid_done.load() && sort_step(g,h,sc))return true;
    }
    auto nh=p.heavy.size()*g.nt,nq=nh+p.normal.size();
    if(p.next.load(std::memory_order_relaxed)>=nq)return false;
    auto i=p.next.fetch_add(1,std::memory_order_relaxed);
    if(i<nh){auto& h=*p.heavy[i/g.nt];heavy_count(g,h,i%g.nt);h.counted.fetch_add(1);return true;}
    if(i<nq){bucket(g,t,sc,p.normal[i-nh]);p.fin.fetch_add(1);return true;}
    return false;
}
}
