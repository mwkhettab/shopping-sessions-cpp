#pragma once
#include "links_sort.hpp"
#include <bit>
#include <cstdint>

#if defined(__x86_64__) || defined(SHOP_TEST_LINKS_PAIR_SIMDE)
#if defined(SHOP_TEST_LINKS_PAIR_SIMDE)
#define SHOP_PAIR_INLINE inline
#define SHOP_PAIR_TARGET
#else
#define SHOP_PAIR_INLINE PIPELINE_ROUTING_INLINE
#define SHOP_PAIR_TARGET PIPELINE_ROUTING_X64
#endif

namespace pipeline::links::pair {
struct Z {__m256i lo,hi;};
static_assert(sizeof(Z)==64);

// Masked lanes can point beyond the last row. Forming their machine address
// this way keeps C++ pointer arithmetic inside live objects.
template<class T> SHOP_PAIR_INLINE T* offset(T* p,std::size_t bytes) {
    return reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(p)+bytes);
}
SHOP_PAIR_INLINE __m256i keep_ymm(__m256i x) {
#if !defined(SHOP_TEST_LINKS_PAIR_SIMDE)
    asm("" : "+v"(x));
#endif
    return x;
}
SHOP_PAIR_INLINE Z load(const void* p) {
    return {keep_ymm(_mm256_loadu_si256(static_cast<const __m256i*>(p))),
            keep_ymm(_mm256_loadu_si256(static_cast<const __m256i*>(offset(p,32))))};
}
SHOP_PAIR_INLINE void store(void* p,Z x) {
    _mm256_storeu_si256(static_cast<__m256i*>(p),keep_ymm(x.lo));
    _mm256_storeu_si256(static_cast<__m256i*>(offset(p,32)),keep_ymm(x.hi));
}
SHOP_PAIR_INLINE Z load64(u8 mask,const void* p) {
    return {_mm256_maskz_loadu_epi64(mask,p),_mm256_maskz_loadu_epi64(mask>>4,offset(p,32))};
}
SHOP_PAIR_INLINE Z load32(u16 mask,const u32* p) {
    return {_mm256_maskz_loadu_epi32(u8(mask),p),_mm256_maskz_loadu_epi32(u8(mask>>8),offset(p,32))};
}
SHOP_PAIR_INLINE void store64(void* p,u8 mask,Z v) {
    _mm256_mask_storeu_epi64(p,mask,v.lo);
    _mm256_mask_storeu_epi64(offset(p,32),mask>>4,v.hi);
}
SHOP_PAIR_INLINE std::size_t compress32(u32* p,u16 mask,Z v) {
    const auto nlo=std::popcount(u8(mask));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p),_mm256_maskz_compress_epi32(u8(mask),v.lo));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p+nlo),_mm256_maskz_compress_epi32(u8(mask>>8),v.hi));
    return std::popcount(mask);
}
SHOP_PAIR_INLINE u8 mask64(u8 lo,u8 hi) {
#if defined(SHOP_TEST_LINKS_PAIR_SIMDE)
    return u8((lo&15)|(hi<<4));
#else
    __mmask8 shifted,result;
    asm("kshiftlb {$4, %3, %0|%0, %3, 4}\n\t"
        "korb {%2, %0, %1|%1, %0, %2}" : "=&k"(shifted),"=k"(result) : "k"(lo),"k"(hi));
    return result;
#endif
}
SHOP_PAIR_INLINE u16 mask32(u8 lo,u8 hi) {
#if defined(SHOP_TEST_LINKS_PAIR_SIMDE)
    return u16(lo)|(u16(hi)<<8);
#else
    __mmask16 result;
    asm("kunpckbw {%2, %1, %0|%0, %1, %2}" : "=k"(result) : "k"(hi),"k"(lo));
    return result;
#endif
}
SHOP_PAIR_INLINE Z splat64(u64 x) {const auto v=_mm256_set1_epi64x(std::bit_cast<i64>(x));return {v,v};}
SHOP_PAIR_INLINE Z splat32(u32 x) {const auto v=_mm256_set1_epi32(std::bit_cast<i32>(x));return {v,v};}
SHOP_PAIR_INLINE Z set64(i64 a,i64 b,i64 c,i64 d,i64 e,i64 f,i64 g,i64 h) {
    return {_mm256_setr_epi64x(a,b,c,d),_mm256_setr_epi64x(e,f,g,h)};
}
SHOP_PAIR_INLINE Z pos8(std::size_t i) {
    return set64(i<<IDX_SH,(i+1)<<IDX_SH,(i+2)<<IDX_SH,(i+3)<<IDX_SH,
                 (i+4)<<IDX_SH,(i+5)<<IDX_SH,(i+6)<<IDX_SH,(i+7)<<IDX_SH);
}
SHOP_PAIR_INLINE Z bor(Z a,Z b) {return {_mm256_or_si256(a.lo,b.lo),_mm256_or_si256(a.hi,b.hi)};}
SHOP_PAIR_INLINE Z andnot(Z a,Z b) {return {_mm256_andnot_si256(a.lo,b.lo),_mm256_andnot_si256(a.hi,b.hi)};}
SHOP_PAIR_INLINE Z minimum(Z a,Z b) {return {_mm256_min_epu64(a.lo,b.lo),_mm256_min_epu64(a.hi,b.hi)};}
SHOP_PAIR_INLINE Z maximum(Z a,Z b) {return {_mm256_max_epu64(a.lo,b.lo),_mm256_max_epu64(a.hi,b.hi)};}
SHOP_PAIR_INLINE Z mask_or(Z src,u8 m,Z a,Z b) {
    return {_mm256_mask_or_epi64(src.lo,m,a.lo,b.lo),_mm256_mask_or_epi64(src.hi,m>>4,a.hi,b.hi)};
}
SHOP_PAIR_INLINE u8 test64(Z a,Z b) {
    return mask64(_mm256_test_epi64_mask(a.lo,b.lo),_mm256_test_epi64_mask(a.hi,b.hi));
}
SHOP_PAIR_INLINE Z idx(Z k) {return {_mm256_srli_epi64(k.lo,IDX_SH),_mm256_srli_epi64(k.hi,IDX_SH)};}
SHOP_PAIR_INLINE Z permute(Z i,Z a) {
    return {_mm256_permutex2var_epi64(a.lo,i.lo,a.hi),_mm256_permutex2var_epi64(a.lo,i.hi,a.hi)};
}
SHOP_PAIR_INLINE __m256i permute_row(Z a,__m256i i,Z b) {
    const auto va=_mm256_permutex2var_epi64(a.lo,i,a.hi),vb=_mm256_permutex2var_epi64(b.lo,i,b.hi);
    return _mm256_mask_blend_epi64(_mm256_test_epi64_mask(i,_mm256_set1_epi64x(8)),va,vb);
}
SHOP_PAIR_INLINE Z permute2(Z a,Z i,Z b) {return {permute_row(a,i.lo,b),permute_row(a,i.hi,b)};}
SHOP_PAIR_INLINE __m256i ev4(Z x) {return _mm256_permute4x64_epi64(_mm256_unpacklo_epi64(x.lo,x.hi),0xd8);}
SHOP_PAIR_INLINE __m256i od4(Z x) {return _mm256_permute4x64_epi64(_mm256_unpackhi_epi64(x.lo,x.hi),0xd8);}
SHOP_PAIR_INLINE Z even(Z a,Z b) {return {ev4(a),ev4(b)};}
SHOP_PAIR_INLINE Z odd(Z a,Z b) {return {od4(a),od4(b)};}
SHOP_PAIR_INLINE Z prev(Z a,Z b) {return {_mm256_alignr_epi64(a.lo,b.hi,3),_mm256_alignr_epi64(a.hi,a.lo,3)};}
SHOP_PAIR_INLINE Z cx(Z v,Z perm,u8 upper) {
    const auto p=permute(perm,v),lo=minimum(v,p),hi=maximum(v,p);
    return {_mm256_mask_mov_epi64(lo.lo,upper,hi.lo),_mm256_mask_mov_epi64(lo.hi,upper>>4,hi.hi)};
}
SHOP_PAIR_INLINE Z sort8(Z v) {
    const auto p1=set64(1,0,3,2,5,4,7,6);
    v=cx(v,p1,0xaa);v=cx(v,set64(3,2,1,0,7,6,5,4),0xcc);v=cx(v,p1,0xaa);
    v=cx(v,set64(7,6,5,4,3,2,1,0),0xf0);v=cx(v,set64(2,3,0,1,6,7,4,5),0xcc);
    return cx(v,p1,0xaa);
}
SHOP_PAIR_INLINE Z merge8(Z v) {
    v={_mm256_min_epu64(v.lo,v.hi),_mm256_max_epu64(v.lo,v.hi)};
    v=cx(v,set64(2,3,0,1,6,7,4,5),0xcc);
    return cx(v,set64(1,0,3,2,5,4,7,6),0xaa);
}
SHOP_PAIR_INLINE Z rev8(Z v) {
    return {_mm256_permute4x64_epi64(v.hi,0x1b),_mm256_permute4x64_epi64(v.lo,0x1b)};
}
SHOP_PAIR_INLINE u8 tie_mask(Z keys,Z previous) {
    const Z x{_mm256_xor_si256(keys.lo,previous.lo),_mm256_xor_si256(keys.hi,previous.hi)};
    const auto hi=splat64(0xffffffff00000000ULL);
    return mask64(_mm256_testn_epi64_mask(x.lo,hi.lo),_mm256_testn_epi64_mask(x.hi,hi.hi))
        &test64(x,splat64(0xfffffff0));
}
SHOP_PAIR_INLINE Z rows(__m256i keys,__m256i pay) {
    const auto a=_mm256_unpacklo_epi64(keys,pay),b=_mm256_unpackhi_epi64(keys,pay);
    return {_mm256_permute2x128_si256(a,b,0x20),_mm256_permute2x128_si256(a,b,0x31)};
}
template<class G> SHOP_PAIR_INLINE void sort2(const G& g,const Click* src,std::span<Click> s) {
    const auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
    const auto b=_mm_loadu_si128(reinterpret_cast<const __m128i*>(src+1));
    const auto ka=src[0][0],kb=src[1][0];
    const auto sw=u8(-int((kb>>32)<(ka>>32)))&3;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(s.data()),_mm_mask_blend_epi64(sw,a,b));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(s.data()+1),_mm_mask_blend_epi64(sw,b,a));
    const auto diff=ka^kb;
    if(!(diff>>32) && (diff>>4)) fix_ties(g,s);
}
template<class G> SHOP_PAIR_INLINE void sort_r1(const G& g,const Click* src,std::span<Click> s) {
    const auto m=(1u<<(2*s.size()))-1,km=(1u<<s.size())-1;
    const auto posmask=splat64(IDX_BITS);
    const auto lo=load64(u8(m),src),hi=load64(u8(m>>8),offset(src,64));
    const auto kv=even(lo,hi),vv=odd(lo,hi);
    if(test64(kv,posmask)) {slow_from(g,src,s);return;}
    const auto k=sort8(mask_or(splat64(UINT64_MAX),u8(km),kv,pos8(0)));
    const auto pv=permute(idx(k),vv),kc=andnot(posmask,k);
    const auto tie=tie_mask(kc,prev(kc,kc))&km&0xfe;
    store64(s.data(),u8(m),rows(kc.lo,pv.lo));
    store64(offset(s.data(),64),u8(m>>8),rows(kc.hi,pv.hi));
    if(tie) fix_ties(g,s);
}
template<class G> SHOP_PAIR_INLINE void sort_r2(const G& g,const Click* src,std::span<Click> s) {
    const auto m=(1u<<(2*(s.size()-8)))-1,km=(1u<<(s.size()-8))-1;
    const auto posmask=splat64(IDX_BITS);
    const auto lo0=load(src),hi0=load(offset(src,64));
    const auto lo1=load64(u8(m),offset(src,128)),hi1=load64(u8(m>>8),offset(src,192));
    const auto kv0=even(lo0,hi0),v0=odd(lo0,hi0),kv1=even(lo1,hi1),v1=odd(lo1,hi1);
    if(test64(bor(kv0,kv1),posmask)) {slow_from(g,src,s);return;}
    const auto a=sort8(bor(kv0,pos8(0))),b=rev8(sort8(mask_or(splat64(UINT64_MAX),u8(km),kv1,pos8(8))));
    const auto k0=merge8(minimum(a,b)),k1=merge8(maximum(a,b));
    const auto p0=permute2(v0,idx(k0),v1),p1=permute2(v0,idx(k1),v1);
    const auto kc0=andnot(posmask,k0),kc1=andnot(posmask,k1);
    const auto tie=(tie_mask(kc0,prev(kc0,kc0))&0xfe)|(tie_mask(kc1,prev(kc1,kc0))&km);
    store(s.data(),rows(kc0.lo,p0.lo));store(offset(s.data(),64),rows(kc0.hi,p0.hi));
    store64(offset(s.data(),128),u8(m),rows(kc1.lo,p1.lo));
    store64(offset(s.data(),192),u8(m>>8),rows(kc1.hi,p1.hi));
    if(tie) fix_ties(g,s);
}
template<std::size_t R,class G> [[gnu::noinline]] SHOP_PAIR_TARGET void sort_net(const G& g,const Click* src,std::span<Click> s) {
    static_assert(R && !(R&(R-1)) && R*8<=(1u<<(32-IDX_SH)));
    const auto n=s.size(),nb=(n+7)/8;
    if(nb>R || !n) {slow_from(g,src,s);return;}
    const auto sentinel=splat64(UINT64_MAX);
    std::array<Z,R> r;r.fill(sentinel);
    alignas(32) std::array<u64,8*R> pay;pay.fill(UINT64_MAX);
    auto orall=splat64(0);
    for(std::size_t b=0;b<nb;++b) {
        const auto count=std::min<std::size_t>(n-8*b,8);
        const auto mask=(1u<<(2*count))-1;
        const auto km=u8((1u<<count)-1);
        const auto lo=load64(u8(mask),offset(src,128*b)),hi=load64(u8(mask>>8),offset(src,128*b+64));
        const auto kv=even(lo,hi);orall=bor(orall,kv);
        r[b]=sort8(mask_or(sentinel,km,kv,pos8(8*b)));
        store(pay.data()+8*b,odd(lo,hi));
    }
    if(test64(orall,splat64(IDX_BITS))) {slow_from(g,src,s);return;}
    for(std::size_t k=1;k<R;k*=2) for(std::size_t base=0;base<R;base+=2*k) {
        auto i=base+k,j=base+2*k-1;
        while(i<j) {const auto x=rev8(r[i]),y=rev8(r[j]);r[i++]=y;r[j--]=x;}
        if(i==j) r[i]=rev8(r[i]);
        for(auto d=k;d;d/=2)
            for(auto blk=base;blk<base+2*k;blk+=2*d)
                for(auto t=blk;t<blk+d;++t) {const auto x=r[t],y=r[t+d];r[t]=minimum(x,y);r[t+d]=maximum(x,y);}
        for(auto t=base;t<base+2*k;++t) r[t]=merge8(r[t]);
    }
    const auto keys=std::bit_cast<std::array<u64,8*R>>(r);
    const volatile u64* pv=pay.data();
    auto previous=keys[0]&~IDX_BITS;
    bool tie=false;
    for(std::size_t i=0;i<n;++i) {
        const auto k=keys[i],kc=k&~IDX_BITS,x=kc^previous;
        tie|=!(x>>32) && (x>>4);previous=kc;
        s[i]={kc,pv[(k>>IDX_SH)&(8*R-1)]};
    }
    if(tie) fix_ties(g,s);
}
template<class G> SHOP_PAIR_TARGET inline void small(const G& g,const Click* src,std::span<Click> s) {
    const auto n=s.size();
    if(n<2) {if(n)s[0]=*src;return;}
    if(n==2) sort2(g,src,s);
    else if(n<=8) sort_r1(g,src,s);
    else if(n<=16) sort_r2(g,src,s);
    else if(n<=32) pair::sort_net<4>(g,src,s);
    else if(n<=64) pair::sort_net<8>(g,src,s);
    else if(n<=128) pair::sort_net<16>(g,src,s);
    else slow_from(g,src,s);
}
SHOP_PAIR_TARGET inline void classify(const u32* off,std::size_t a,std::size_t b,std::array<ScratchVec<u32>,4>& lists) {
    std::array<std::size_t,4> counts{};
    for(auto& l:lists) {l.clear();l.resize(b-a+16);}
    const Z iota{_mm256_setr_epi32(0,1,2,3,4,5,6,7),_mm256_setr_epi32(8,9,10,11,12,13,14,15)};
    for(auto u=a;u<b;u+=16) {
        const auto tail=u16((1u<<std::min<std::size_t>(b-u,16))-1);
        const auto after=load32(tail,off+u+1),before=load32(tail,off+u);
        const Z n{_mm256_sub_epi32(after.lo,before.lo),_mm256_sub_epi32(after.hi,before.hi)};
        const auto base=splat32(u32(u));
        const Z uv{_mm256_add_epi32(base.lo,iota.lo),_mm256_add_epi32(base.hi,iota.hi)};
        const auto two=_mm256_set1_epi32(2),three=_mm256_set1_epi32(3),five=_mm256_set1_epi32(5);
        const auto nine=_mm256_set1_epi32(9),seven=_mm256_set1_epi32(7),big=_mm256_set1_epi32(17);
        const auto m2=mask32(_mm256_cmpeq_epi32_mask(n.lo,two),_mm256_cmpeq_epi32_mask(n.hi,two));
        const auto m8=mask32(_mm256_cmple_epu32_mask(_mm256_sub_epi32(n.lo,three),five),_mm256_cmple_epu32_mask(_mm256_sub_epi32(n.hi,three),five));
        const auto m16=mask32(_mm256_cmple_epu32_mask(_mm256_sub_epi32(n.lo,nine),seven),_mm256_cmple_epu32_mask(_mm256_sub_epi32(n.hi,nine),seven));
        const auto mb=mask32(_mm256_cmpge_epu32_mask(n.lo,big),_mm256_cmpge_epu32_mask(n.hi,big));
        counts[0]+=compress32(lists[0].data()+counts[0],m2,uv);
        counts[1]+=compress32(lists[1].data()+counts[1],m8,uv);
        counts[2]+=compress32(lists[2].data()+counts[2],m16,uv);
        counts[3]+=compress32(lists[3].data()+counts[3],mb,uv);
    }
    for(unsigned i=0;i<4;++i) lists[i].resize(counts[i]);
}
template<class G> SHOP_PAIR_TARGET inline void users(const G& g,const u32* off,std::size_t a,std::size_t b,Scratch& sc) {
    auto& lists=sc.lens;
    classify(off,a,b,lists);
    auto row=[&](u32 u) {return std::span<Click>(g.csr+off[u],off[u+1]-off[u]);};
    for(auto u:lists[0]) {auto s=row(u);sort2(g,s.data(),s);}
    for(std::size_t ph=0;ph<8;++ph) for(auto i=ph;i<lists[1].size();i+=8) {
        auto s=row(lists[1][i]);sort_r1(g,s.data(),s);
    }
    for(std::size_t ph=0;ph<8;++ph) for(auto i=ph;i<lists[2].size();i+=8) {
        auto s=row(lists[2][i]);sort_r2(g,s.data(),s);
    }
    for(auto u:lists[3]) {
        auto s=row(u);
        if(s.size()<=32) pair::sort_net<4>(g,s.data(),s);
        else if(s.size()<=64) pair::sort_net<8>(g,s.data(),s);
        else if(s.size()<=128) pair::sort_net<16>(g,s.data(),s);
        else links::sort_from(g,s.data(),s,sc);
    }
}
}
#undef SHOP_PAIR_INLINE
#undef SHOP_PAIR_TARGET
#endif
