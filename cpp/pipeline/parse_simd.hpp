#pragma once
#include "model_types.hpp"
#include "keyrec.hpp"

namespace pipeline::parse {
inline u64 active_mask(std::size_t n) { return n >= 64 ? UINT64_MAX : (u64{1} << n) - 1; }
}

#if defined(__x86_64__)
#include <immintrin.h>
#define PIPELINE_ROUTING_X64 __attribute__((target("avx2,bmi,bmi2,popcnt,avx512f,avx512bw,avx512vl,avx512dq")))
#define PIPELINE_ROUTING_INLINE __attribute__((target("avx2,bmi,bmi2,popcnt,avx512f,avx512bw,avx512vl,avx512dq"),always_inline)) inline

namespace pipeline::parse {
inline bool routing_x64() {
#ifdef __AVX512VL__
    return true;
#else
    static const bool available = __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw")
        && __builtin_cpu_supports("avx512vl") && __builtin_cpu_supports("avx512dq")
        && __builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi")
        && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("popcnt");
    return available;
#endif
}
PIPELINE_ROUTING_INLINE __m512i splat8(u64 n) {
    return _mm512_set1_epi64(std::bit_cast<i64>(n));
}
PIPELINE_ROUTING_INLINE __m256i splat4(u64 n) {
    return _mm256_set1_epi64x(std::bit_cast<i64>(n));
}
PIPELINE_ROUTING_INLINE __m512i mul8(__m512i x, __m512i y) {
    const auto lo = _mm256_mullo_epi64(_mm512_castsi512_si256(x),_mm512_castsi512_si256(y));
    const auto hi = _mm256_mullo_epi64(_mm512_extracti64x4_epi64(x,1),_mm512_extracti64x4_epi64(y,1));
    return _mm512_inserti64x4(_mm512_castsi256_si512(lo),hi,1);
}
PIPELINE_ROUTING_INLINE __m512i mulu32_8(__m512i x, __m512i y) {
    auto lo = _mm256_mul_epu32(_mm512_castsi512_si256(x),_mm512_castsi512_si256(y));
    auto hi = _mm256_mul_epu32(_mm512_extracti64x4_epi64(x,1),_mm512_extracti64x4_epi64(y,1));
    return _mm512_inserti64x4(_mm512_castsi256_si512(lo),hi,1);
}
template<bool Ifma> PIPELINE_ROUTING_INLINE __m512i mul_id(__m512i x,__m512i y) {
    if constexpr (Ifma) {
        // The dispatcher checks IFMA. Keeping this instruction in the cascade-lake
        // function lets both multiplies inline even when the image targets that ISA.
        auto result=_mm512_setzero_si512();
        asm("vpmadd52luq {%2, %1, %0|%0, %1, %2}" : "+v"(result) : "v"(x),"v"(y));
        return result;
    }
    return mul8(x,y);
}
template<bool Ifma> PIPELINE_ROUTING_INLINE __m512i decode8(__m512i value,__m512i key) {
    const auto mask=splat8(keyrec::MASK);
    auto x=_mm512_and_si512(mul_id<Ifma>(value,splat8(keyrec::BINV)),mask);
    x=_mm512_xor_si512(x,_mm512_xor_si512(_mm512_srli_epi64(x,17),_mm512_srli_epi64(x,34)));
    return _mm512_and_si512(mul_id<Ifma>(_mm512_xor_si512(x,key),splat8(keyrec::AINV)),mask);
}
PIPELINE_ROUTING_INLINE __m256i decode4(__m256i value,__m256i key) {
    const auto mask=splat4(keyrec::MASK);
    auto x=_mm256_and_si256(_mm256_mullo_epi64(value,splat4(keyrec::BINV)),mask);
    x=_mm256_xor_si256(x,_mm256_xor_si256(_mm256_srli_epi64(x,17),_mm256_srli_epi64(x,34)));
    return _mm256_and_si256(_mm256_mullo_epi64(_mm256_xor_si256(x,key),splat4(keyrec::AINV)),mask);
}
}
#endif
