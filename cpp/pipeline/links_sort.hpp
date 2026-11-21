#pragma once
#include "events_runs.hpp"
#include "parse_simd.hpp"
#include <mutex>

namespace pipeline::links {
using Click=std::array<u64,2>;
// Slots beyond the written count serve as output space for compress stores.
// Default-initialize these trivial values without clearing every scratch page.
template<class T> struct ScratchAlloc:std::allocator<T> {
    using value_type=T;
    template<class U> struct rebind {using other=ScratchAlloc<U>;};
    ScratchAlloc()=default;
    template<class U> ScratchAlloc(const ScratchAlloc<U>&) noexcept {}
    template<class U,class... A> void construct(U* p,A&&... a) {
        if constexpr(sizeof...(a)==0) ::new (static_cast<void*>(p)) U;
        else std::construct_at(p,std::forward<A>(a)...);
    }
};
template<class T> using ScratchVec=std::vector<T,ScratchAlloc<T>>;
struct Scratch {
    ScratchVec<Click> tmp;
    ScratchVec<u64> lk;
    std::array<ScratchVec<u32>,4> lens;
};
inline constexpr unsigned IDX_SH=18;
inline constexpr u64 IDX_BITS=0xffffffffu & ~((1u<<IDX_SH)-1);
inline constexpr std::size_t SPLIT_MIN=128,SPLIT_PER=6,LSD_MIN=2048;
inline std::atomic_bool DEFER_RAW{};
inline std::mutex deferred_mutex;
inline std::vector<std::span<Click>> deferred;
template<class G> inline void raw_ties(const G& g,std::span<Click> group) {
    std::stable_sort(group.begin(),group.end(),[&](const Click& a,const Click& b){
        const auto cmp=events::cid_cmp(g,b[1],a[1]);return cmp<0 || (cmp==0 && a[0]<b[0]);
    });
}
template<class G> inline std::size_t tie_group(const G& g,std::span<Click> s,std::size_t i) {
    const auto ts=s[i][0]>>32;
    auto a=i-1,b=i+1;
    while(a && s[a-1][0]>>32==ts) --a;
    while(b<s.size() && s[b][0]>>32==ts) ++b;
    const auto grp=s.subspan(a,b-a);
    if(std::all_of(grp.begin(),grp.end(),[](const auto& c){return !(c[1]&RAW);})) {
        if(grp.size()<=16) {
            std::array<u64,16> rank;
            for(std::size_t i=0;i<grp.size();++i) rank[i]=enc36(grp[i][1]).first;
            for(std::size_t x=1;x<grp.size();++x) {
                const auto ex=rank[x];const Click cx=grp[x];auto y=x;
                while(y && (rank[y-1]<ex || (rank[y-1]==ex && grp[y-1][0]>cx[0]))) {rank[y]=rank[y-1];grp[y]=grp[y-1];--y;}
                rank[y]=ex;grp[y]=cx;
            }
        } else {
            std::vector<std::pair<u64,Click>> v;v.reserve(grp.size());for(auto c:grp) v.emplace_back(enc36(c[1]).first,c);
            std::sort(v.begin(),v.end(),[](const auto& x,const auto& y){return x.first>y.first || (x.first==y.first && x.second[0]<y.second[0]);});
            for(std::size_t j=0;j<grp.size();++j) grp[j]=v[j].second;
        }
    } else if(DEFER_RAW.load(std::memory_order_relaxed)) {std::lock_guard lk(deferred_mutex);deferred.push_back(grp);}
    else raw_ties(g,grp);
    return b;
}
template<class G> inline void fix_ties(const G& g,std::span<Click> s) {
    for(std::size_t i=1;i<s.size();) {
        const u64 x=s[i-1][0]^s[i][0];
        if(!(x>>32) && (x>>4)) {
            const u64 ts=s[i][0]>>32;
            if((i<2 || s[i-2][0]>>32!=ts) && (i+1==s.size() || s[i+1][0]>>32!=ts) && !((s[i][1]|s[i-1][1])&RAW)) {
                const auto e0=enc36(s[i-1][1]).first,e1=enc36(s[i][1]).first;
                if(e0<e1 || (e0==e1 && s[i-1][0]>s[i][0])) std::swap(s[i-1],s[i]);
                ++i;
            } else i=tie_group(g,s,i);
        } else ++i;
    }
}
template<class G> inline void slow_from(const G& g,const Click* src,std::span<Click> s) {
    if(src!=s.data()) std::copy_n(src,s.size(),s.begin());
    std::stable_sort(s.begin(),s.end(),[](const auto& a,const auto& b){return (a[0]>>32)<(b[0]>>32);});
    fix_ties(g,s);
}
#if defined(__x86_64__)
PIPELINE_ROUTING_INLINE __m512i compare_exchange(__m512i v,__m512i perm,__mmask8 upper) {
    auto p=_mm512_permutexvar_epi64(perm,v);
    return _mm512_mask_max_epu64(_mm512_min_epu64(v,p),upper,v,p);
}
PIPELINE_ROUTING_INLINE __m512i sort8(__m512i v) {
    const auto p1=_mm512_setr_epi64(1,0,3,2,5,4,7,6);
    v=compare_exchange(v,p1,0xaa);v=compare_exchange(v,_mm512_setr_epi64(3,2,1,0,7,6,5,4),0xcc);v=compare_exchange(v,p1,0xaa);
    v=compare_exchange(v,_mm512_setr_epi64(7,6,5,4,3,2,1,0),0xf0);v=compare_exchange(v,_mm512_setr_epi64(2,3,0,1,6,7,4,5),0xcc);return compare_exchange(v,p1,0xaa);
}
PIPELINE_ROUTING_INLINE __m512i rev8(__m512i v){return _mm512_permutexvar_epi64(_mm512_setr_epi64(7,6,5,4,3,2,1,0),v);}
PIPELINE_ROUTING_INLINE __m512i merge8(__m512i v){v=compare_exchange(v,_mm512_setr_epi64(4,5,6,7,0,1,2,3),0xf0);v=compare_exchange(v,_mm512_setr_epi64(2,3,0,1,6,7,4,5),0xcc);return compare_exchange(v,_mm512_setr_epi64(1,0,3,2,5,4,7,6),0xaa);}
template<std::size_t R,class G> PIPELINE_ROUTING_X64 inline void sort_net(const G& g,const Click* src,std::span<Click> s) {
    using parse::splat8;
    const auto max=splat8(UINT64_MAX),even=_mm512_setr_epi64(0,2,4,6,8,10,12,14),odd=_mm512_setr_epi64(1,3,5,7,9,11,13,15);
    const auto iota=_mm512_setr_epi64(0,1,2,3,4,5,6,7);
    __m512i keys[R],pay[R];for(auto& k:keys) k=max;
    auto orall=_mm512_setzero_si512();
    for(std::size_t b=0;b<(s.size()+7)/8;++b) {
        const auto n=std::min<std::size_t>(8,s.size()-8*b);
        const auto km=__mmask8((1u<<n)-1);auto wm=(1u<<(n*2))-1;
        const auto* p=src+b*8;
        auto lo=_mm512_maskz_loadu_epi64(__mmask8(wm),p);
        auto hi=n>4 ? _mm512_maskz_loadu_epi64(__mmask8(wm>>8),p+4) : _mm512_setzero_si512();
        auto k=_mm512_permutex2var_epi64(lo,even,hi);orall=_mm512_or_si512(orall,k);
        auto pos=_mm512_slli_epi64(_mm512_add_epi64(iota,splat8(8*b)),IDX_SH);
        keys[b]=sort8(_mm512_mask_or_epi64(max,km,k,pos));pay[b]=_mm512_permutex2var_epi64(lo,odd,hi);
    }
    if(_mm512_test_epi64_mask(orall,splat8(IDX_BITS))) {slow_from(g,src,s);return;}
    for(std::size_t k=1;k<R;k*=2) for(std::size_t b=0;b<R;b+=2*k) {
        auto i=b+k,j=b+2*k-1;
        while(i<j) {auto a=rev8(keys[i]),z=rev8(keys[j]);keys[i++]=z;keys[j--]=a;}if(i==j) keys[i]=rev8(keys[i]);
        for(auto d=k;d;d/=2) for(auto q=b;q<b+2*k;q+=2*d) for(auto t=q;t<q+d;++t) {auto x=keys[t],y=keys[t+d];keys[t]=_mm512_min_epu64(x,y);keys[t+d]=_mm512_max_epu64(x,y);}
        for(auto t=b;t<b+2*k;++t) keys[t]=merge8(keys[t]);
    }
    bool tie=false;u64 prev=0;
    if constexpr(R<=2) {
        const auto ix0=_mm512_setr_epi64(0,8,1,9,2,10,3,11),ix1=_mm512_setr_epi64(4,12,5,13,6,14,7,15);
        for(std::size_t b=0;b<(s.size()+7)/8;++b){
            auto len=std::min<std::size_t>(8,s.size()-b*8);auto wm=(1u<<(2*len))-1;auto idx=_mm512_srli_epi64(keys[b],IDX_SH);
            __m512i v;if constexpr(R==1) v=_mm512_permutexvar_epi64(idx,pay[0]);else v=_mm512_permutex2var_epi64(pay[0],idx,pay[1]);
            auto k=_mm512_andnot_si512(splat8(IDX_BITS),keys[b]);auto old=_mm512_permutexvar_epi64(_mm512_setr_epi64(0,0,1,2,3,4,5,6),k);
            if(b) old=_mm512_mask_mov_epi64(old,1,splat8(prev));
            auto diff=_mm512_xor_si512(k,old);auto tm=_mm512_testn_epi64_mask(diff,splat8(0xffffffff00000000ull))&_mm512_test_epi64_mask(diff,splat8(0xfffffff0));
            tie|=(tm&((1u<<len)-1))!=0;
            _mm512_mask_storeu_epi64(s.data()+8*b,__mmask8(wm),_mm512_permutex2var_epi64(k,ix0,v));
            if(len>4) _mm512_mask_storeu_epi64(s.data()+8*b+4,__mmask8(wm>>8),_mm512_permutex2var_epi64(k,ix1,v));
            prev=s[b*8+len-1][0];
        }
    } else {
        alignas(64) std::array<u64,8*R> k,v;
        for(std::size_t i=0;i<R;++i) { _mm512_store_si512(k.data()+i*8,keys[i]);if(i<(s.size()+7)/8)_mm512_store_si512(v.data()+i*8,pay[i]); }
        prev=k[0]&~IDX_BITS;
        for(std::size_t i=0;i<s.size();++i) {const auto kc=k[i]&~IDX_BITS,x=kc^prev;tie|=!(x>>32)&&(x>>4);prev=kc;s[i]={kc,v[(k[i]>>IDX_SH)&(R*8-1)]};}
    }
    if(tie) fix_ties(g,s);
}
#endif
template<class G> inline void sort_small(const G& g,const Click* src,std::span<Click> s) {
    if(s.size()<2){if(!s.empty())s[0]=src[0];return;}
    if(s.size()==2){auto a=src[0],b=src[1];if((b[0]>>32)<(a[0]>>32))std::swap(a,b);s[0]=a;s[1]=b;auto x=a[0]^b[0];if(!(x>>32)&&(x>>4))fix_ties(g,s);return;}
#if defined(__x86_64__)
    if(parse::routing_x64()) {
        if(s.size()<=8)sort_net<1>(g,src,s);else if(s.size()<=16)sort_net<2>(g,src,s);else if(s.size()<=32)sort_net<4>(g,src,s);else if(s.size()<=64)sort_net<8>(g,src,s);else sort_net<16>(g,src,s);return;
    }
#endif
    // The packed position precedes campaign/alias bits for equal timestamps.
    slow_from(g,src,s);
}
template<class G> inline void radix_split(const G& g,std::span<Click> s,Click* tmp,unsigned sh,unsigned bits,bool first) {
    if(bits==0 || bits>11 || s.size()>UINT32_MAX){slow_from(g,s.data(),s);return;}
    const std::size_t np=1u<<bits;
    auto digit=[&](u64 k){return ((k>>32)>>sh)&(np-1);};
    std::array<u32,2049> start{};u64 any=0;
    for(auto& c:s){++start[digit(c[0])+1];any|=c[0];}
    if(first && ((any>>32)>>sh)>=np){slow_from(g,s.data(),s);return;}
    for(std::size_t i=0;i<np;++i)start[i+1]+=start[i];
    std::array<u32,2048> next;std::copy_n(start.begin(),np,next.begin());
    for(auto& c:s)tmp[next[digit(c[0])]++]=c;
    for(std::size_t i=0;i<np;++i){const auto a=start[i],n=start[i+1]-a;auto p=s.subspan(a,n);if(n<=SPLIT_MIN)sort_small(g,tmp+a,p);else if(!sh)slow_from(g,tmp+a,p);else{std::copy_n(tmp+a,n,p.begin());auto b=std::min(sh,11u);radix_split(g,p,tmp+a,sh-b,b,false);}}
}
template<class G> inline void lsd_sort(const G& g,std::span<Click> s,Scratch& sc) {
    std::array<std::array<u32,256>,4> h{};
    for(const auto& c:s) {const auto k=u32(c[0]>>32);for(unsigned d=0;d<4;++d)++h[d][(k>>(8*d))&255];}
    sc.tmp.clear();sc.tmp.resize(s.size()+16);auto *src=s.data(),*dst=sc.tmp.data();const auto k=s[0][0]>>32;
    for(unsigned d=0;d<4;++d){if(h[d][(k>>(8*d))&255]==s.size())continue;
        std::array<u32,256> pos;u32 at=0;for(unsigned b=0;b<256;++b){pos[b]=at;at+=h[d][b];}
        for(std::size_t i=0;i<s.size();++i){const auto& c=src[i];dst[pos[(c[0]>>(32+8*d))&255]++]=c;}std::swap(src,dst);
    }
    if(src!=s.data())std::copy_n(src,s.size(),s.begin());fix_ties(g,s);
}
template<class G> inline void sort_from(const G& g,const Click* src,std::span<Click> s,Scratch& sc) {
    auto n=s.size();if(n<=SPLIT_MIN) {sort_small(g,src,s);return;}
    if(src!=s.data())std::copy_n(src,n,s.begin());
    if(n>=LSD_MIN){lsd_sort(g,s,sc);return;}
    auto bits=std::clamp<unsigned>(std::bit_width(n/SPLIT_PER),1,11);auto top=std::clamp<unsigned>(std::bit_width(std::max<u64>(g.clen,1)),11,32);
    sc.tmp.clear();sc.tmp.resize(n+16);radix_split(g,s,sc.tmp.data(),top-bits,bits,true);
}
#if defined(__x86_64__)
PIPELINE_ROUTING_X64 inline void classify_users(const u32 *off,std::size_t a,std::size_t b,std::array<ScratchVec<u32>,4>& lens) {
    std::array<std::size_t,4> ns{};for(auto& l:lens){l.clear();l.resize(b-a+16);}
    const auto iota=_mm512_setr_epi32(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
    for(std::size_t i=a;i<b;i+=16){auto live=__mmask16((1u<<std::min<std::size_t>(16,b-i))-1);
        auto n=_mm512_sub_epi32(_mm512_maskz_loadu_epi32(live,off+i+1),_mm512_maskz_loadu_epi32(live,off+i));auto uv=_mm512_add_epi32(iota,_mm512_set1_epi32(i));
        __mmask16 m[]={_mm512_cmpeq_epi32_mask(n,_mm512_set1_epi32(2)),_mm512_cmple_epu32_mask(_mm512_sub_epi32(n,_mm512_set1_epi32(3)),_mm512_set1_epi32(5)),_mm512_cmple_epu32_mask(_mm512_sub_epi32(n,_mm512_set1_epi32(9)),_mm512_set1_epi32(7)),_mm512_cmpge_epu32_mask(n,_mm512_set1_epi32(17))};
        for(unsigned j=0;j<4;++j){_mm512_mask_compressstoreu_epi32(lens[j].data()+ns[j],m[j],uv);ns[j]+=std::popcount(u32(m[j]));}
    } for(unsigned j=0;j<4;++j)lens[j].resize(ns[j]);
}
#endif
template<class G> inline void sort_users(const G& g,const u32* off,std::size_t ua,std::size_t ub,Scratch& sc) {
    auto& lists=sc.lens;
#if defined(__x86_64__)
    if(parse::routing_x64()) classify_users(off,ua,ub,lists);else
#endif
    {for(auto& l:lists){l.clear();l.reserve(ub-ua+16);}for(auto u=ua;u<ub;++u){const auto n=off[u+1]-off[u];if(n>=2)lists[n==2?0:n<=8?1:n<=16?2:3].push_back(u32(u));}}
    for(unsigned cat=0;cat<4;++cat){const std::size_t stride=(cat==1 || cat==2)?8:1;
        const auto& l=lists[cat];for(std::size_t phase=0;phase<stride;++phase)for(auto i=phase;i<l.size();i+=stride){const u32 u=l[i],a=off[u],n=off[u+1]-a;sort_from(g,g.csr+a,std::span<Click>(g.csr+a,n),sc);}}
}
}
