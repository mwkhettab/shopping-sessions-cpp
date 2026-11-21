#pragma once
#define SIMDE_ENABLE_NATIVE_ALIASES
#include <simde/x86/avx512.h>
#include <bit>
#include <cstdint>
using __mmask8 = simde__mmask8;
inline __m256d session_mm256_cvtepu64_pd(__m256i v) {
    auto a=simde__m256i_to_private(v);simde__m256d_private b;
    for(unsigned i=0;i<4;++i)b.f64[i]=static_cast<double>(a.u64[i]);return simde__m256d_from_private(b);
}
inline __m256i session_mm256_cvttpd_epu64(__m256d v) {
    auto a=simde__m256d_to_private(v);simde__m256i_private b;
    for(unsigned i=0;i<4;++i)b.u64[i]=static_cast<uint64_t>(a.f64[i]);return simde__m256i_from_private(b);
}
inline __m256i session_mm256_mullo_epi64(__m256i v,__m256i w) {
    auto a=simde__m256i_to_private(v),b=simde__m256i_to_private(w);
    for(unsigned i=0;i<4;++i)a.u64[i]*=b.u64[i];return simde__m256i_from_private(a);
}
inline __m256i session_mm256_min_epu64(__m256i v,__m256i w) {
    auto a=simde__m256i_to_private(v),b=simde__m256i_to_private(w);
    for(unsigned i=0;i<4;++i)a.u64[i]=std::min(uint64_t(a.u64[i]),uint64_t(b.u64[i]));return simde__m256i_from_private(a);
}
inline __m256i session_mm256_lzcnt_epi64(__m256i v) {
    auto a=simde__m256i_to_private(v);
    for(unsigned i=0;i<4;++i)a.u64[i]=std::countl_zero(uint64_t(a.u64[i]));return simde__m256i_from_private(a);
}
inline __m256i session_mm256_mask_sub_epi64(__m256i v,unsigned mask,__m256i a,__m256i b) {
    return _mm256_mask_mov_epi64(v,mask,_mm256_sub_epi64(a,b));
}
inline __m512i session_mm512_lzcnt_epi64(__m512i v) {
    auto a=simde__m512i_to_private(v);
    for(unsigned i=0;i<8;++i)a.u64[i]=std::countl_zero(uint64_t(a.u64[i]));return simde__m512i_from_private(a);
}
inline __m512i session_mm512_zextsi256_si512(__m256i v){
    simde__m512i_private a{};a.m256i[0]=v;return simde__m512i_from_private(a);
}
inline __m512i session_mm512_cvtepu32_epi64(__m256i v) {
    auto a=simde__m256i_to_private(v);simde__m512i_private b;
    for(unsigned i=0;i<8;++i)b.u64[i]=a.u32[i];return simde__m512i_from_private(b);
}
inline uint64_t session_mm512_mask_reduce_min_epu64(unsigned m,__m512i v) {
    auto a=simde__m512i_to_private(v);uint64_t n=UINT64_MAX;
    for(unsigned i=0;i<8;++i)if(m&(1u<<i))n=std::min(n,uint64_t(a.u64[i]));return n;
}
inline __m512i session_mm512_alignr_epi32(__m512i va,__m512i vb,unsigned count) {
    auto a=simde__m512i_to_private(va),b=simde__m512i_to_private(vb);simde__m512i_private r;
    for(unsigned i=0;i<16;++i){auto j=i+(count&15);r.u32[i]=j<16?b.u32[j]:a.u32[j-16];}
    return simde__m512i_from_private(r);
}
inline uint16_t session_mm512_cmpgt_epu32_mask(__m512i a,__m512i b) {return uint16_t(~_mm512_cmple_epu32_mask(a,b));}
inline uint16_t session_mm512_cmplt_epu32_mask(__m512i a,__m512i b) {return uint16_t(~_mm512_cmple_epu32_mask(b,a));}
inline uint16_t session_mm512_cmpneq_epi32_mask(__m512i a,__m512i b) {return uint16_t(~_mm512_cmpeq_epi32_mask(a,b));}
inline uint8_t session_mm512_mask_cmpneq_epu64_mask(unsigned m,__m512i a,__m512i b) {return uint8_t(m&~_mm512_cmpeq_epi64_mask(a,b));}
inline __m512i session_mm512_maskz_loadu_epi32(unsigned m,const void* ptr) {
    const auto* p=static_cast<const uint8_t*>(ptr);simde__m512i_private v{};
    for(unsigned i=0;i<16;++i)if (m&(1u<<i)) {uint32_t x;std::memcpy(&x,p+4*i,4);v.u32[i]=x;}
    return simde__m512i_from_private(v);
}
inline uint32_t _bzhi_u32(uint32_t x,unsigned n) {return n>=32?x:x&((1u<<n)-1);}
// SIMDe 0.8.2's portable masked store zeroes inactive lanes. Real hardware leaves
// them untouched. Access active lanes only, including masks at an allocation end.
inline void session_mm512_mask_storeu_epi32(void* ptr,unsigned m,__m512i v) {
    auto a=simde__m512i_to_private(v);auto* p=static_cast<uint8_t*>(ptr);
    for(unsigned i=0;i<16;++i)if(m&(1u<<i)){uint32_t x=a.u32[i];std::memcpy(p+4*i,&x,4);}
}
inline __m512i session_mm512_maskz_loadu_epi64(unsigned m,const void* ptr) {
    const auto* p=static_cast<const uint8_t*>(ptr);simde__m512i_private v{};
    for(unsigned i=0;i<8;++i)if(m&(1u<<i)){uint64_t x;std::memcpy(&x,p+8*i,8);v.u64[i]=x;}
    return simde__m512i_from_private(v);
}
