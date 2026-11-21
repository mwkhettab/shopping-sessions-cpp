#pragma once
#include "parse_simd.hpp"
#include <atomic>
#include <optional>

namespace pipeline::ckix {
inline constexpr u64 VALID = u64{1} << 32;
struct alignas(64) Pend {
    std::array<std::array<u64,2>,NB + 8> rec{};
    std::array<u64,NB + 8> at{};
    std::size_t n{};
    u64 end{};
};
struct CkxP {
    i64 cbase;
    u64 clen,ukey,ucap,ckey,ccap;
    const u8* ctab;
    std::optional<std::pair<u64,u64>> crange;
    std::array<u64,2>* csr;
};
inline void store_pair(std::array<u64,2>& destination, u64 key, u64 user) {
    // Identical click copies can reach different workers. No consumer reads
    // the array until every producer has finished its last pending write.
    std::atomic_ref<u64>(destination[0]).store(key, std::memory_order_relaxed);
    std::atomic_ref<u64>(destination[1]).store(user, std::memory_order_relaxed);
}
inline void flush(Pend& pd,std::array<u64,2>* csr) {
    for(std::size_t i=0;i<pd.n;++i) store_pair(csr[pd.at[i]],pd.rec[i][0],pd.rec[i][1]);
    pd.n=0;
}
inline void store_one(Pend& pd,std::array<u64,2>* csr,u64 c,u64 w0,u64 user) {
    store_pair(csr[c],w0,user|VALID);
    pd.end=std::max(pd.end,c+1);
}
inline void prefetch_pending(const Pend& pd,std::array<u64,2>* csr) {
    for(std::size_t i=0;i<pd.n;++i) __builtin_prefetch(csr+pd.at[i],1,3);
}
inline u64 ck_scalar_ix(const CkBatch& cb,Pend& pd,std::size_t n,const CkxP& p) {
    flush(pd,p.csr);
    u64 special=0;
    for(std::size_t i=0;i<n;++i) {
        const u64 rel=u64(cb.ts[i])-u64(p.cbase), camp=cb.camp[i], uid=cb.uid[i], cid=cb.cid[i];
        const bool present=p.crange ? camp-p.crange->first<=p.crange->second : camp<CBOUND && p.ctab[camp]!=255;
        if(!present || uid==ID_EMPTY || rel>=p.clen) continue;
        const u64 w=keyrec::unscramble(uid,p.ukey), user=w>>9, alias=w&511;
        const u64 c=keyrec::unscramble(cid,p.ckey);
        if(uid>keyrec::MASK || user>=p.ucap || alias>=NALIAS || cid>keyrec::MASK || c>=p.ccap) {
            special |= u64{1}<<i;
            continue;
        }
        pd.at[pd.n]=c;
        pd.rec[pd.n++]={(rel<<32)|(camp<<4)|alias, user|VALID};
        pd.end=std::max(pd.end,c+1);
    }
    prefetch_pending(pd,p.csr);
    return special;
}
#if defined(__x86_64__)
PIPELINE_ROUTING_X64 inline u64 ck_packet_ix(const CkBatch& cb,Pend& pd,std::size_t n,const CkxP& p) {
    using parse::splat8;
    flush(pd,p.csr);
    const auto [lo,span]=p.crange.value_or(std::pair<u64,u64>{});
    const auto user_key=splat8(p.ukey&keyrec::MASK), click_key=splat8(p.ckey&keyrec::MASK);
    const auto even=_mm512_setr_epi64(0,8,1,9,2,10,3,11);
    const auto odd=_mm512_setr_epi64(4,12,5,13,6,14,7,15);
    const auto active=parse::active_mask(n);
    u64 special=0;
    std::size_t k=0;
    auto end=_mm512_setzero_si512();
    for(std::size_t j=0;j<n;j+=8) {
        auto rel=_mm512_sub_epi64(_mm512_load_si512(cb.ts.data()+j),splat8(u64(p.cbase)));
        auto cid=_mm512_load_si512(cb.cid.data()+j), uid=_mm512_load_si512(cb.uid.data()+j);
        auto camp=_mm512_load_si512(cb.camp.data()+j);
        __mmask8 present;
        if(p.crange) {
            present=_mm512_cmple_epu64_mask(_mm512_sub_epi64(camp,splat8(lo)),splat8(span));
        } else {
            const auto inbounds=_mm512_cmplt_epu64_mask(camp,splat8(CBOUND));
            const auto ff=splat8(255);
            const auto channel=_mm512_and_si512(_mm512_mask_i64gather_epi64(ff,inbounds,camp,p.ctab,1),ff);
            present=_mm512_mask_cmpneq_epi64_mask(inbounds,channel,ff);
        }
        auto keep=present & _mm512_cmpneq_epi64_mask(uid,splat8(ID_EMPTY))
                          & _mm512_cmplt_epu64_mask(rel,splat8(p.clen));
        auto w=parse::decode8<false>(uid,user_key);
        auto user=_mm512_srli_epi64(w,9), alias=_mm512_and_si512(w,splat8(511));
        auto c=parse::decode8<false>(cid,click_key);
        auto normal=_mm512_cmple_epu64_mask(uid,splat8(keyrec::MASK)) &
                    _mm512_cmplt_epu64_mask(user,splat8(p.ucap)) & _mm512_cmplt_epu64_mask(alias,splat8(NALIAS));
        auto cidok=_mm512_cmple_epu64_mask(cid,splat8(keyrec::MASK)) & _mm512_cmplt_epu64_mask(c,splat8(p.ccap));
        auto live=__mmask8(active>>j);
        __mmask8 ok=__mmask8(keep & normal & cidok & live);
        auto word0=_mm512_or_si512(_mm512_slli_epi64(rel,32),_mm512_or_si512(_mm512_slli_epi64(camp,4),alias));
        auto word1=_mm512_or_si512(user,splat8(VALID));
        auto a=_mm512_maskz_compress_epi64(ok,word0), b=_mm512_maskz_compress_epi64(ok,word1);
        _mm512_storeu_si512(pd.at.data()+k,_mm512_maskz_compress_epi64(ok,c));
        _mm512_storeu_si512(pd.rec.data()+k,_mm512_permutex2var_epi64(a,even,b));
        _mm512_storeu_si512(pd.rec.data()+k+4,_mm512_permutex2var_epi64(a,odd,b));
        end=_mm512_mask_max_epu64(end,ok,end,_mm512_add_epi64(c,splat8(1)));
        k+=std::popcount(unsigned(ok));
        special |= u64(keep & ~ok & live)<<j;
    }
    pd.n=k;
    prefetch_pending(pd,p.csr);
    pd.end=std::max<u64>(pd.end,_mm512_reduce_max_epu64(end));
    return special;
}
#endif
inline u64 ck_vec_ix(const CkBatch& cb,Pend& pd,std::size_t n,const CkxP& p) {
#if defined(__x86_64__)
    if (parse::routing_x64()) return ck_packet_ix(cb,pd,n,p);
#endif
    return ck_scalar_ix(cb,pd,n,p);
}
}
