#pragma once
#include "links_part.hpp"

namespace pipeline::links {
template<class G> inline bool piece_ix(const G& g,std::size_t a,std::size_t b,i64 prev,u32 u0,u32* start) {
    u64 xa=(u64(a)*keyrec::A)&keyrec::MASK,key=g.ckey&keyrec::MASK;bool bad=false;
    auto csr=g.csr;if(b>a && !(csr[a][1]&ckix::VALID))return false;
    for(auto i=a;i<b;++i){auto& row=csr[i];const auto w=row[1];
        if(!(w&ckix::VALID))row=csr[i-1];
        else{const auto u=i64(u32(w));const auto ul=u32(u)-u0;bad|=(u<prev || ul>=UBSIZE);
            if(u!=prev)std::atomic_ref<u32>(start[ul&(UBSIZE-1)]).store(u32(i),std::memory_order_relaxed);prev=u;auto x=xa^key;row[1]=((x^(x>>17))*keyrec::B)&keyrec::MASK;}
        xa=(xa+keyrec::A)&keyrec::MASK;
    }
    return !bad;
}
inline void fill_off(std::span<u32> off,std::size_t lo,std::size_t hi) {
    off[UBSIZE]=u32(hi);for(auto i=UBSIZE;i--;)off[i]=std::min(off[i],off[i+1]);
    if(off[0]!=lo)sys::fallback("click index order");
}
template<class G> inline void build_pieces(const G& g,Heavy& h,std::size_t lo,std::size_t hi) {
    auto used=[&](auto i){return g.csr[i][1]&ckix::VALID;};auto& p=h.pieces;
    if(hi>lo){p.emplace_back(lo,i64(h.ub<<UB)-1);
        for(std::size_t k=1;k<g.nt;++k){auto s=lo+(hi-lo)*k/g.nt;while(s<hi && !used(s))++s;
            if(s<hi && s>p.back().first){auto q=s-1;while(!used(q))--q;p.emplace_back(s,i64(u32(g.csr[q][1])));}}
    }
    p.emplace_back(hi,0);auto o=off_of(g,h.ub);std::fill_n(o.begin(),UBSIZE,UINT32_MAX);
}
template<class G> inline std::size_t lower_ix(const G& g,std::size_t lo,std::size_t hi,u64 user) {
    auto l=lo,h=hi;auto used=[&](auto i){return g.csr[i][1]&ckix::VALID;};
    while(l<h){const auto mid=l+(h-l)/2;auto m=mid;while(m<h && !used(m))++m;
        if(m<h && u32(g.csr[m][1])<user)l=m+1;else h=mid;}
    while(l<hi && !used(l))++l;return l;
}
template<class G> inline std::optional<Fix> place(const G& g,std::size_t lo,std::size_t hi,const Cand& c){
    if(hi<=lo)return {};
    const auto s=lower_ix(g,lo,hi,c.u),e=lower_ix(g,lo,hi,u64(c.u)+1);auto h=s;
    while(h<e && (g.csr[h][1]&ckix::VALID))++h;if(h==e)return {};
    const u64 w=c.key|c.a;g.csr[h]={w,u64(c.u)|ckix::VALID};return Fix{c.u,w,h,c.tok};
}
template<class G> inline void prelude_ix(const G& g) {
    auto& p=*plan;const auto end=std::size_t(CEND.load());if(end>g.ccap)sys::fallback("click index range");
    std::size_t first=0;while(first<end && !(g.csr[first][1]&ckix::VALID))++first;
    for(std::size_t tid=0;tid<g.nt;++tid){auto& t=g.t(tid);for(const auto& [a,b]:t.gen_edges){mark_complex(g,a);mark_complex(g,b);}
        for(std::size_t i=0;i<t.odd_clicks.size();){const auto c=t.odd_clicks[i];if(!(c[0]&RAW))if(auto user=g.normal(c[0])){
            const auto [u,a]=*user;
            if(auto x=place(g,first,end,Cand{u,a,c[1],c[2]})){p.fixes.push_back(*x);t.odd_clicks[i]=t.odd_clicks.back();t.odd_clicks.pop_back();continue;}
            g.utab[u]|=COMPLEX;
        }++i;}
    }
    // Binary searches advance in lockstep across user buckets.
    std::vector<std::size_t> lo(g.nub+1,first),hi(g.nub+1,end);bool active;
    do{active=false;for(std::size_t ub=1;ub<g.nub;++ub)if(lo[ub]<hi[ub]){
        active=true;auto mid=lo[ub]+(hi[ub]-lo[ub])/2,m=mid;while(m<hi[ub] && !(g.csr[m][1]&ckix::VALID))++m;
        if(m<hi[ub] && u32(g.csr[m][1])<(u64(ub)<<UB))lo[ub]=m+1;else hi[ub]=mid;
    }}while(active);
    p.base.assign(g.nub+1,first);for(std::size_t ub=1;ub<g.nub;++ub){auto m=lo[ub];while(m<end && !(g.csr[m][1]&ckix::VALID))++m;p.base[ub]=m;}p.base[g.nub]=end;
    for(std::size_t ub=0;ub<g.nub;++ub){auto a=p.base[ub],b=p.base[ub+1];if(a>b)sys::fallback("click index order");
        if(b-a>std::max<std::size_t>((end-first)/HEAVY_DIV,1<<17)){auto h=std::make_unique<Heavy>(ub);build_pieces(g,*h,a,b);p.heavy.push_back(std::move(h));}
        else p.normal.push_back(u32(ub));}
}
template<class G,class T> inline void links_ix(const G& g,T& t,std::size_t ub,Cx& cx) {
    std::size_t seen=0,expected=0;bool counting=true;
    auto visit=[&](auto f){for(std::size_t th=0;th<g.nt;++th){if(counting)expected+=g.t(th).us.count(ub);g.t(th).us.for_each(ub,[&](auto rows){if(counting)seen+=rows.size();for(auto r:rows)f(r.w0);});}if(counting&&seen!=expected)sys::fallback("link partition count");counting=false;};
    process_links(t,utab_of(g,ub),ub<<UB,visit,cx);
}
template<class G> inline void compact_and_sort(const G& g,Scratch& sc,std::size_t ub,std::size_t lo,std::size_t hi,sys::Instant::time_point start,u64& d2) {
    if(hi<=lo)return;auto off=off_of(g,ub);std::fill_n(off.begin(),UBSIZE,UINT32_MAX);
    if(!piece_ix(g,lo,hi,i64(ub<<UB)-1,u32(ub<<UB),off.data()))sys::fallback("click index order");
    fill_off(off,lo,hi);d2=elapsed_ns(start);sort_users(g,off.data(),0,UBSIZE,sc);
}
template<class G> inline void prelude_a(const G& g) {
    auto& p=*plan;p.cand.resize(g.nub);p.range.resize(g.nub);
    // The index shares one array across concurrently compacted user buckets.
    // Prove disjoint ownership before any job can modify a bucket's slots.
    std::size_t previous_end=0;
    for(std::size_t ub=0;ub<g.nub;++ub){
        Range bound{SIZE_MAX,0};
        for(std::size_t th=0;th<g.nt;++th){auto [lo,hi]=g.t(th).us.ordinal_range(ub);bound.first=std::min(bound.first,lo);bound.second=std::max(bound.second,hi);}
        if(bound.first<bound.second){if(bound.first<previous_end || bound.second>g.ccap)sys::fallback("click index order");previous_end=bound.second;}
    }
    for(std::size_t tid=0;tid<g.nt;++tid){auto& t=g.t(tid);for(auto [a,b]:t.gen_edges){mark_complex(g,a);mark_complex(g,b);}
        for(std::size_t i=0;i<t.odd_clicks.size();){const auto c=t.odd_clicks[i];if(!(c[0]&RAW))if(auto user=g.normal(c[0])){
            auto [u,a]=*user;p.cand[u>>UB].push_back({u,a,c[1],c[2]});t.odd_clicks[i]=t.odd_clicks.back();t.odd_clicks.pop_back();continue;
        }++i;}
    }
    std::vector<std::size_t> counts(g.nub);std::size_t total=0;
    for(std::size_t ub=0;ub<g.nub;++ub){for(std::size_t th=0;th<g.nt;++th)counts[ub]+=g.t(th).us.count(ub);total+=counts[ub];}
    for(std::size_t ub=0;ub<g.nub;++ub){if(counts[ub]>std::max<std::size_t>(total/HEAVY_DIV,1<<17)){
        auto h=std::make_unique<Heavy>(ub);h->links.resize(g.nt);h->mm.assign(g.nt,{SIZE_MAX,0});p.heavy.push_back(std::move(h));
    }else p.normal.push_back(u32(ub));}
}
template<class G,class T> inline void place_a(const G& g,T& t,std::size_t ub,std::size_t lo,std::size_t hi) {
    auto& p=*plan;for(const auto& c:p.cand[ub]) {
        if(auto f=place(g,lo,hi,c)){std::lock_guard lk(p.fixes_lock);p.fixes.push_back(*f);}
        else {g.utab[c.u]|=COMPLEX;t.odd_clicks.push_back({keyrec::scramble((u64(c.u)<<9)|c.a,g.ukey),c.key,c.tok});}
    }
}
template<class G> inline void scatter_a(const G& g,std::span<const Rec> rows,u64 u0,ScratchVec<u64>& lk,Range& mm) {
    auto j=lk.size();lk.resize(j+rows.size()+1);auto [lo,hi]=mm;
    for(auto r:rows){const auto flag=r.w0>>63;const auto c=std::size_t(u32(r.w0));lk[j]=r.w0;j+=std::size_t(flag);
        lo=std::min(lo,c|(0-std::size_t(flag)));hi=std::max(hi,(c+1)&~(0-std::size_t(flag)));
        if(!flag)ckix::store_pair(g.csr[c],r.w1,(u0+local_user(r.w0))|ckix::VALID);
    }lk.resize(j);mm={lo,hi};
}
template<class G> inline void scatter_partition(const G& g,std::size_t ub,std::size_t th,ScratchVec<u64>& lk,Range& mm){
    std::size_t seen=0;g.t(th).us.for_each(ub,[&](auto rows){seen+=rows.size();scatter_a(g,rows,u64(ub)<<UB,lk,mm);});if(seen!=g.t(th).us.count(ub))sys::fallback("click partition count");
}
template<class G,class T> inline void bucket_a(const G& g,T& t,Scratch& sc,std::size_t ub) {
    const auto start=sys::Instant::now();Range mm{SIZE_MAX,0};sc.lk.clear();for(std::size_t th=0;th<g.nt;++th)scatter_partition(g,ub,th,sc.lk,mm);
    if(mm.second<=mm.first)mm={0,0};plan->range[ub]=mm;auto [lo,hi]=mm;
    const auto d1=elapsed_ns(start);place_a(g,t,ub,lo,hi);process_links(t,utab_of(g,ub),ub<<UB,[&](auto f){for(auto w:sc.lk)f(w);},t.cx_edges);auto d2=elapsed_ns(start);compact_and_sort(g,sc,ub,lo,hi,start,d2);
    const auto el=elapsed_ns(start);t.prof[5]+=d1;t.prof[6]+=d2;t.prof[3]+=el;t.prof[4]=std::max(t.prof[4],el);
}
template<class G,class T> inline bool job_index(const G& g,T& t,Scratch& sc) {
    auto& p=*plan;for(auto& hp:p.heavy){auto& h=*hp;
        if(g.cpart){auto th=next_unit(h.scat_next,g.nt);if(th<g.nt){scatter_partition(g,h.ub,th,h.links[th],h.mm[th]);h.scat_fin.fetch_add(1);return true;}
            if(h.scat_fin.load()<g.nt)continue;
            if(!h.mid_taken.load() && !h.mid_taken.exchange(true)){Range mm{SIZE_MAX,0};for(auto [lo,hi]:h.mm){mm.first=std::min(mm.first,lo);mm.second=std::max(mm.second,hi);}if(mm.second<=mm.first)mm={0,0};
                auto [lo,hi]=mm;place_a(g,t,h.ub,lo,hi);build_pieces(g,h,lo,hi);p.range[h.ub]=mm;h.mid_done.store(true);return true;}
            if(!h.mid_done.load())continue;
        }
        auto np=h.pieces.size()-1;auto j=next_unit(h.piece_next,np);
        if(j<np){auto [a,prev]=h.pieces[j];if(!piece_ix(g,a,h.pieces[j+1].first,prev,u32(h.ub<<UB),off_of(g,h.ub).data()))sys::fallback("click index order");h.piece_fin.fetch_add(1);return true;}
        if(h.piece_fin.load()==np && !h.fin_taken.load() && !h.fin_taken.exchange(true)){
            auto off=off_of(g,h.ub);if(g.cpart){process_links(t,utab_of(g,h.ub),h.ub<<UB,[&](auto f){for(const auto& l:h.links)for(auto w:l)f(w);},h.cx);
                auto [lo,hi]=p.range[h.ub];if(hi>lo)fill_off(off,lo,hi);else std::fill(off.begin(),off.end(),0);}
            else {links_ix(g,t,h.ub,h.cx);fill_off(off,p.base[h.ub],p.base[h.ub+1]);}h.fin_done.store(true);return true;}
        if(h.fin_done.load() && sort_step(g,h,sc))return true;
    }
    const auto n=p.normal.size();if(p.next.load(std::memory_order_relaxed)>=n)return false;
    const auto i=p.next.fetch_add(1,std::memory_order_relaxed);if(i>=n)return false;
    auto ub=p.normal[i];if(g.cpart)bucket_a(g,t,sc,ub);else{const auto start=sys::Instant::now();links_ix(g,t,ub,t.cx_edges);const auto d1=elapsed_ns(start);auto d2=d1;compact_and_sort(g,sc,ub,p.base[ub],p.base[ub+1],start,d2);const auto el=elapsed_ns(start);t.prof[5]+=d1;t.prof[6]+=d2;t.prof[3]+=el;t.prof[4]=std::max(t.prof[4],el);}p.fin.fetch_add(1);return true;
}
template<class G> inline void fix_odd(const G& g,const Fix& f) {
    auto i=std::size_t(f.user)+(f.user>>UB),a=std::size_t(g.csr_off[i]),b=std::size_t(g.csr_off[i+1]);
    const auto ts=f.w0>>32,y=keyrec::scramble(f.slot,g.ckey);std::span<Click> rows{g.csr+a,b-a};
    auto lo=std::partition_point(rows.begin(),rows.end(),[&](const auto& r){return (r[0]>>32)<ts;}),hi=std::partition_point(lo,rows.end(),[&](const auto& r){return (r[0]>>32)<=ts;});
    bool found=false,mixed=false;for(auto p=lo;p!=hi;++p){if((*p)[0]==f.w0 && (*p)[1]==y){(*p)[1]=f.tok;found=true;}mixed|=(((*p)[0]^f.w0)>>4)!=0;}
    if(!found)sys::fallback("odd click row");if(mixed)tie_group(g,rows,std::size_t(lo-rows.begin())+1);
}
}
