#pragma once
#include "pqw_encoding.hpp"

namespace pipeline::pqw {

// Out reserves 32 trailing bytes, enough for a final overlapping vector store.
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
template<unsigned W> [[gnu::target("avx2,bmi2")]] inline void pack_user_words(
    u8* dst,const u32* src,std::size_t n,u32 base) {
    const auto bias=_mm256_set1_epi32(std::bit_cast<i32>(base));
    constexpr u64 mask=(u64{1}<<W)-1;
    if constexpr(W<=8) {
        constexpr auto bytes=mask*0x0101010101010101ULL;
        const auto perm=_mm256_setr_epi32(0,4,1,5,2,6,3,7);
        for(;n>=32;n-=32,src+=32,dst+=4*W) {
            const auto a=_mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src)),bias);
            const auto b=_mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src+8)),bias);
            const auto c=_mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src+16)),bias);
            const auto d=_mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src+24)),bias);
            const auto lanes=_mm256_packus_epi16(_mm256_packus_epi32(a,b),_mm256_packus_epi32(c,d));
            const auto ordered=_mm256_permutevar8x32_epi32(lanes,perm);
            const auto lo=_mm256_castsi256_si128(ordered),hi=_mm256_extracti128_si256(ordered,1);
            store(dst,_pext_u64(_mm_cvtsi128_si64(lo),bytes));
            store(dst+W,_pext_u64(_mm_extract_epi64(lo,1),bytes));
            store(dst+2*W,_pext_u64(_mm_cvtsi128_si64(hi),bytes));
            store(dst+3*W,_pext_u64(_mm_extract_epi64(hi,1),bytes));
        }
    } else if constexpr(W<=16) {
        constexpr auto words=mask*0x0001000100010001ULL;
        for(;n>=16;n-=16,src+=16,dst+=2*W) {
            const auto a=_mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src)),bias);
            const auto b=_mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src+8)),bias);
            const auto ordered=_mm256_permute4x64_epi64(_mm256_packus_epi32(a,b),0xd8);
            const auto lo=_mm256_castsi256_si128(ordered),hi=_mm256_extracti128_si256(ordered,1);
            store(dst,__uint128_t(_pext_u64(_mm_cvtsi128_si64(lo),words)) |
                (__uint128_t(_pext_u64(_mm_extract_epi64(lo,1),words))<<(4*W)));
            store(dst+W,__uint128_t(_pext_u64(_mm_cvtsi128_si64(hi),words)) |
                (__uint128_t(_pext_u64(_mm_extract_epi64(hi,1),words))<<(4*W)));
        }
    }
    for(;n>=8;n-=8,src+=8,dst+=W) {
        constexpr auto pairs=mask|(mask<<32);
        const auto v=_mm256_sub_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src)),bias);
        const auto lo=_mm256_castsi256_si128(v),hi=_mm256_extracti128_si256(v,1);
        const auto a=__uint128_t(_pext_u64(_mm_cvtsi128_si64(lo),pairs)) |
            (__uint128_t(_pext_u64(_mm_extract_epi64(lo,1),pairs))<<(2*W));
        const auto b=__uint128_t(_pext_u64(_mm_cvtsi128_si64(hi),pairs)) |
            (__uint128_t(_pext_u64(_mm_extract_epi64(hi,1),pairs))<<(2*W));
        if constexpr(W==32) {store(dst,a);store(dst+16,b);}
        else {
            store(dst,a|(b<<(4*W)));
            if constexpr(W>16) store(dst+16,b>>(128-4*W));
        }
    }
    if(n) {
        std::array<u32,8> tail{};
        for(std::size_t i=0;i<n;++i) tail[i]=src[i]-base;
        pack_group<W>(dst,tail);
    }
}
#endif

inline void emit_user_words(Out& o,std::span<const u32> values,u32 base,u32 width) {
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__)) && !defined(SHOP_SPARSE_FORCE_SCALAR)
    if(__builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi2")) {
        if(values.empty()) return;
        const auto groups=(values.size()+7)/8;
        o.put_varint(2*groups+1);
        static constexpr auto pack=[]<std::size_t... I>(std::index_sequence<I...>){
            return std::array{&pack_user_words<I+1>...};
        }(std::make_index_sequence<32>{});
        pack.at(width-1)(o.tail(groups*width),values.data(),values.size(),base);
        o.commit(groups*width);
        return;
    }
#endif
    emit_packed(o,values.size(),width,[&](auto i){return values[i]-base;});
}

inline void key_run(u8*& dst,u32 key,std::size_t length,unsigned record_bytes) {
    // Scratch includes space for the last full-width store.
    const auto value=u64(key)<<8;
    while(length>63) {store(dst,value|126);dst+=record_bytes;length-=63;}
    store(dst,value|(2*length));dst+=record_bytes;
}

inline std::size_t scalar_key_runs(u8* dst,std::span<const u32> keys,u32 base,unsigned record_bytes) {
    auto* start=dst;
    for(std::size_t i=0;i<keys.size();) {
        std::size_t end=i+1;
        while(end<keys.size() && keys[end]==keys[i]) ++end;
        key_run(dst,keys[i]-base,end-i,record_bytes);
        i=end;
    }
    return std::size_t(dst-start);
}

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
template<unsigned RecordBytes> [[gnu::target("avx512f,avx512vl,avx2,popcnt")]] inline std::size_t vector_key_runs(
    u8* dst,std::span<const u32> keys,u32 base) {
    auto* start=dst;
    const auto bias=_mm256_set1_epi32(std::bit_cast<i32>(base));
    const auto lane=_mm256_setr_epi32(0,1,2,3,4,5,6,7);
    const auto shuffle=RecordBytes==3
        ?_mm_setr_epi8(0,1,2,4,5,6,8,9,10,12,13,14,-1,-1,-1,-1)
        :_mm_setr_epi8(0,1,4,5,8,9,12,13,-1,-1,-1,-1,-1,-1,-1,-1);
    const auto perm=_mm256_broadcastsi128_si256(shuffle);
    std::size_t i=0,next=0;
    for(;i+8<keys.size();i+=8) {
        const auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(keys.data()+i));
        const auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(keys.data()+i+1));
        const unsigned ends_mask=_mm256_cmpneq_epi32_mask(a,b);
        if(!ends_mask) continue;
        const unsigned count=std::popcount(ends_mask);
        const auto ends=_mm256_maskz_compress_epi32(ends_mask,_mm256_add_epi32(_mm256_set1_epi32(int(i+1)),lane));
        const auto previous=_mm256_alignr_epi32(ends,_mm256_set1_epi32(int(next)),7);
        const auto lengths=_mm256_sub_epi32(ends,previous);
        if(_mm256_mask_cmpgt_epu32_mask((1u<<count)-1,lengths,_mm256_set1_epi32(63))) {
            for(auto mask=ends_mask;mask;mask&=mask-1) {
                const auto at=i+std::countr_zero(mask);
                key_run(dst,keys[at]-base,at+1-next,RecordBytes);
                next=at+1;
            }
        } else {
            const auto values=_mm256_maskz_compress_epi32(ends_mask,_mm256_sub_epi32(a,bias));
            const auto pairs=_mm256_or_si256(_mm256_slli_epi32(lengths,1),_mm256_slli_epi32(values,8));
            const auto bytes=_mm256_shuffle_epi8(pairs,perm);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst),_mm256_castsi256_si128(bytes));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst+4*RecordBytes),_mm256_extracti128_si256(bytes,1));
            dst+=count*RecordBytes;
            next=i+std::bit_width(ends_mask);
        }
    }
    for(;i<keys.size();++i) {
        if(i+1==keys.size() || keys[i]!=keys[i+1]) {
            key_run(dst,keys[i]-base,i+1-next,RecordBytes);
            next=i+1;
        }
    }
    return std::size_t(dst-start);
}

[[gnu::target("avx2,popcnt")]] inline std::size_t count_key_runs(std::span<const u32> keys) {
    if(keys.empty()) return 0;
    std::size_t runs=1,i=0;
    for(;i+8<keys.size();i+=8) {
        const auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(keys.data()+i));
        const auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(keys.data()+i+1));
        const unsigned equal=_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(a,b)));
        runs+=8-std::popcount(equal);
    }
    for(;i+1<keys.size();++i) runs+=keys[i]!=keys[i+1];
    return runs;
}
#endif

inline std::size_t encode_user_runs(u8* dst,std::span<const u32> keys,u32 base,unsigned record_bytes) {
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__)) && !defined(SHOP_SPARSE_FORCE_SCALAR)
    if(keys.size()>=16 && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl") &&
       __builtin_cpu_supports("avx2") && __builtin_cpu_supports("popcnt")) {
        if(record_bytes==2) return vector_key_runs<2>(dst,keys,base);
        if(record_bytes==3) return vector_key_runs<3>(dst,keys,base);
    }
#endif
    return scalar_key_runs(dst,keys,base,record_bytes);
}

inline std::size_t estimated_run_bytes(std::span<const u32> keys,unsigned record_bytes) {
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__)) && !defined(SHOP_SPARSE_FORCE_SCALAR)
    if(__builtin_cpu_supports("avx2") && __builtin_cpu_supports("popcnt"))
        return count_key_runs(keys)*record_bytes;
#endif
    std::size_t count=!keys.empty();
    for(std::size_t i=1;i<keys.size();++i) count+=keys[i-1]!=keys[i];
    return count*record_bytes;
}

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
// The six-bit bound keeps each block of eight codes and the pending bits in one u64.
[[gnu::target("avx2,bmi2,popcnt")]] inline void pack_user_categories(
    u8* levels,u8* dst,std::span<const u8> categories,unsigned width) {
    constexpr u64 byte_lanes=0x0101010101010101ULL;
    const auto bits=byte_lanes*((u64{1}<<width)-1);
    u64 pending=0;
    unsigned used=0;
    std::array<u8,32> tail;
    for(std::size_t i=0;i<categories.size();i+=32) {
        const auto n=std::min<std::size_t>(32,categories.size()-i);
        const u8* src=categories.data()+i;
        if(n<32) {tail.fill(UINT8_MAX);std::memcpy(tail.data(),src,n);src=tail.data();}
        const auto values=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
        const auto null=_mm256_cmpeq_epi8(values,_mm256_set1_epi8(-1));
        const u32 valid=~u32(_mm256_movemask_epi8(null));
        std::memcpy(levels+i/8,&valid,(n+7)/8);
        for(unsigned j=0;j<4;++j) {
            const unsigned lanes=(valid>>(8*j))&255;
            u64 group;
            std::memcpy(&group,src+8*j,sizeof(group));
            const auto mask=_pdep_u64(lanes,byte_lanes)*255;
            pending|=_pext_u64(group,mask&bits)<<used;
            used+=std::popcount(lanes)*width;
            store(dst,pending);
            const unsigned bytes=used/8;
            dst+=bytes;pending>>=8*bytes;used%=8;
        }
    }
    store(dst,pending);
}
#endif
}
