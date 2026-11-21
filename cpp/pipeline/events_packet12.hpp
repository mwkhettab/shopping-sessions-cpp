#pragma once
#include "parse_event12.hpp"

namespace pipeline::events {
inline void scatter_record(Rec r,Rec* slots,u64 gen) {
    auto& s=slots[r.w1&(BSIZE-1)];
    if(u16(s.w1)!=u16(gen) || ((r.w1>>16)&ING_MAX)<((s.w1>>16)&ING_MAX))
        s={r.w0,(r.w1&~u64{0xffff})|gen};
}
inline void scatter12_scalar(const u8* p,std::size_t n,Rec* slots,u64 gen) {
    for(std::size_t i=0;i<n;++i) scatter_record(parse::unpack_event12(p+12*i),slots,gen);
}
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
[[gnu::noinline,gnu::cold]] inline void scatter12_duplicate(Rec* s,__m128i record) {
    const auto bits=u64(_mm_extract_epi16(record,5))<<16 | u64(_mm_extract_epi16(record,6))<<32;
    if(((bits>>16)&ING_MAX)<((s->w1>>16)&ING_MAX))
        _mm_storeu_si128(reinterpret_cast<__m128i*>(s),record);
}
inline void scatter12_slot(Rec* s,__m128i r,u64 gen) {
    if(u16(s->w1)==u16(gen)) [[unlikely]] {scatter12_duplicate(s,r);return;}
    _mm_storeu_si128(reinterpret_cast<__m128i*>(s),r);
}
[[gnu::target("avx2,avx512f,avx512vl")]] inline void scatter12_wide(const u8* p,std::size_t n,
                                                                   Rec* slots,u64 gen,const i8* next) {
    const auto ix=_mm256_setr_epi32(1,2,0,1,4,5,3,4),shift=_mm256_setr_epi32(27,0,0,0,27,0,0,0);
    const auto keep=_mm256_setr_epi32(-1,-1,i32(0xffff0000u),0x07ffffff,-1,-1,i32(0xffff0000u),0x07ffffff);
    const auto gv=_mm256_setr_epi32(0,0,i32(gen),0,0,0,i32(gen),0);
    if(next) __builtin_prefetch(next+parse::LINK_OFF);
    std::size_t i=0;
    for(;n-i>=2;i+=2) {
        const u8* row=p+i*12;
        // The final two records occupy exactly 24 bytes, including at a guard-page boundary.
        const auto x=n-i==2?_mm256_maskz_loadu_epi32(0x3f,row):_mm256_loadu_si256(reinterpret_cast<const __m256i*>(row));
        const auto words=_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(x,ix),shift);
        const auto records=_mm256_ternarylogic_epi32(words,keep,gv,0xea);
        u16 id0,id1;std::memcpy(&id0,row,2);std::memcpy(&id1,row+12,2);
        if(next && !(i&3)) __builtin_prefetch(next+i*12);
        scatter12_slot(slots+(id0&(BSIZE-1)),_mm256_castsi256_si128(records),gen);
        scatter12_slot(slots+(id1&(BSIZE-1)),_mm256_extracti128_si256(records,1),gen);
    }
    if(i<n) scatter_record(parse::unpack_event12(p+12*i),slots,gen);
}
#endif
inline void scatter_chunk12(const u8* p,std::size_t n,Rec* slots,u64 gen,const i8* next) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(SHOP_R12_FORCE_SCALAR)
    if(__builtin_cpu_supports("avx2") && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl")) {
        scatter12_wide(p,n,slots,gen,next);return;
    }
#endif
    scatter12_scalar(p,n,slots,gen);
}
}
