#pragma once
#include "events_runs.hpp"
#include <charconv>

namespace pipeline::events {
inline constexpr std::array<u64,9> POW36={1,36,1296,46656,1679616,60466176,2176782336,78364164096,2821109907456};
inline u64 name_key_scalar(u64 ukey,u32 u,u32 klass) {
    u64 best=UINT64_MAX;
    for (auto m=klass;m;m&=m-1) {
        auto y=keyrec::scramble((u64(u)<<9)|std::countr_zero(m),ukey);
        const auto len=1+(y>=POW36[1])+(y>=POW36[2])+(y>=POW36[3])+(y>=POW36[4])+
            (y>=POW36[5])+(y>=POW36[6])+(y>=POW36[7]);
        best=std::min(best,((y*POW36[8-len])<<4)|u64(len));
    }
    return best;
}
inline std::pair<u64,std::size_t> key_chars(u64 best) {
    const auto norm=best>>4,len=best&15;
    const auto hi=u32(norm/1679616),lo=u32(norm%1679616);
    const auto le=u64(PAIR36[hi/1296])|(u64(PAIR36[hi%1296])<<16)|(u64(PAIR36[lo/1296])<<32)|(u64(PAIR36[lo%1296])<<48);
    return {__builtin_bswap64(le)&(UINT64_MAX<<(8*(8-len))),len};
}
inline auto fast_name(u64 ukey,u32 u,u32 klass) {
    const auto best=name_key_scalar(ukey,u,klass);
    return best==UINT64_MAX ? std::pair<u64,std::size_t>{UINT64_MAX,0} : key_chars(best);
}
inline bool fast_cpu() {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512dq") &&
        __builtin_cpu_supports("avx512vl") && __builtin_cpu_supports("avx512bw") &&
        __builtin_cpu_supports("avx512cd") && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("avx2") && __builtin_cpu_supports("popcnt");
#else
    return false;
#endif
}

inline constexpr std::size_t FN_=BSIZE+GUARD+64;
inline constexpr std::size_t OQ_SIDX=r64(8*FN_),OQ_STS=OQ_SIDX+r64(4*FN_),
    OQ_SDAY=OQ_STS+r64(4*FN_)+4*GUARD,OQ_SCAMP=OQ_STS+2*r64(4*FN_),
    OQ_SLOTS=r64(16*(BSIZE+16))-16*8,OQ_ROWDAY=OQ_STS+3*r64(4*FN_),
    OQ_ROWS0=OQ_STS+4*r64(4*FN_),OQ_ROWR0=OQ_STS+5*r64(4*FN_),OQ_ROWREV=OQ_STS+6*r64(4*FN_),
    OP_RUNS0=3*r64(4*FN_), OP_RUNROW0=2*r64(4*FN_), OP_RUNPIDX=r64(4*FN_),
    OP_ROWCAT=r64(FN_)+3*r64(4*FN_),OP_CELL=r64(2*(BSIZE+64)),OP_SID=OP_CELL+r64(4*BSIZE),
    OP_ROW=OP_SID+r64(4*BSIZE),OP_W=OP_ROW+r64(4*BSIZE),OP_NMV=OP_W+r64(4*BSIZE),
    OP_NMK=OP_NMV+r64(8*(8*FN_+16)),OP_NMR=OP_NMK+r64(8*(8*FN_+16)),
    OP_NMBEST=OP_NMR+r64(4*(8*FN_+16)),OR_WLS=r64(256*RC_N),OR_WLR=OR_WLS+r64(4*(WL_CAP+WL_RUN+64)),
    OB_KEEPM=r64(2*(BSIZE/8+2)),OB_PKM=OB_KEEPM+r64(BSIZE/8+8),OB_BLKNS=OB_PKM+r64(BSIZE/8+8),
    OB_SSB=OB_BLKNS+r64(4*(BSIZE/16+2)),OB_BLKNROW=OB_SSB+r64(2*(BSIZE/16+2)),
    OB_RWB=OB_BLKNROW+r64(4*(BSIZE/16+2));
struct alignas(64) V16 {u32 v[16];};
struct alignas(64) Rc {u32 ts[16],camp[16],conf[16],vm,pad[15];};
static_assert(sizeof(Rc)==256 && sizeof(V16)==64);
alignas(64) inline constexpr auto ONEHOT=[]{
    std::array<V16,16> a{};for (std::size_t i=0;i<16;++i)a[i].v[i]=1;return a;
}();
template<class T> inline T* at(const void* p,std::ptrdiff_t off) {
    return reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(p)+off);
}
struct Fast {
    Rec* slots;u64* q;u16* blkk;u8 *keepm,*pkm;
    u32 *sidx,*sts,*sday,*scamp,*blk_ns;u16* ssb;
    u32 *row_day,*row_s0,*row_r0,*row_rev;u8* row_cat;
    u32 *blk_nrow;u16* rwb;u32 *run_s0,*run_row0,*run_pidx;
    u16* plist;u32 *p_cell,*p_sid,*p_row,*p_w;
    u64 *nm_v,*nm_k;u32* nm_r;u64* nm_best;
    std::vector<u32> mix;
    std::vector<V16> f;
    u64* last;u32 gbase{};V16* asst;u32* cred;Rc* rc;
    u32 *wl_s,*wl_r,*alist;Edge* edge{};
    Fast(std::size_t nc,std::size_t nd,const Share& sh):f(64) {
        const auto [dense,hot]=fast_sizes(nc,nd);
        constexpr auto cold=OP_ROWCAT+OP_NMBEST+r64(8*FN_);
        auto *hp=sh.dense?sh.dense:sys::anon(dense);
        if (sh.dense) std::memset(hp,0,r64(16*(BSIZE+16)));
        const auto spn=cold+(sh.hot?0:r4k(hot));
        auto* sp=sys::anon(spn);
#if defined(MADV_NOHUGEPAGE)
        ::madvise(sp,spn,MADV_NOHUGEPAGE);
#endif
        auto* ht=sh.hot?sh.hot:sp;auto* cd=sh.hot?sp:sp+r4k(hot);
        slots=at<Rec>(hp,8*16);q=at<u64>(hp,r64(16*(BSIZE+16)));
        sidx=at<u32>(q,OQ_SIDX);sts=at<u32>(q,OQ_STS);sday=at<u32>(q,OQ_SDAY);scamp=at<u32>(q,OQ_SCAMP);
        row_day=at<u32>(q,OQ_ROWDAY);row_s0=at<u32>(q,OQ_ROWS0);row_r0=at<u32>(q,OQ_ROWR0);row_rev=at<u32>(q,OQ_ROWREV);
        blkk=at<u16>(ht,0);keepm=ht+OB_KEEPM;pkm=ht+OB_PKM;blk_ns=at<u32>(ht,OB_BLKNS);ssb=at<u16>(ht,OB_SSB);
        blk_nrow=at<u32>(ht,OB_BLKNROW);rwb=at<u16>(ht,OB_RWB);
        last=at<u64>(rwb,r64(2*(BSIZE/16+2)));asst=at<V16>(last,r64(8*(CBOUND+2)));
        cred=at<u32>(asst,r64(64*(nc+2)));rc=at<Rc>(cred,r64(4*(nc+2)*nd));wl_s=at<u32>(rc,OR_WLS);
        wl_r=at<u32>(rc,OR_WLR);alist=at<u32>(wl_r,r64(4*(WL_CAP+WL_RUN+64)));
        row_cat=cd;plist=at<u16>(cd,OP_ROWCAT);run_s0=at<u32>(plist,-std::ptrdiff_t(OP_RUNS0));
        run_row0=at<u32>(plist,-std::ptrdiff_t(OP_RUNROW0));run_pidx=at<u32>(plist,-std::ptrdiff_t(OP_RUNPIDX));
        p_cell=at<u32>(plist,OP_CELL);p_sid=at<u32>(plist,OP_SID);p_row=at<u32>(plist,OP_ROW);p_w=at<u32>(plist,OP_W);
        nm_v=at<u64>(plist,OP_NMV);nm_k=at<u64>(plist,OP_NMK);nm_r=at<u32>(plist,OP_NMR);nm_best=at<u64>(plist,OP_NMBEST);
        if (at<u8>(row_rev,r64(4*FN_))!=hp+dense || at<u8>(alist,r64(4*(AL_CAP+64)))!=ht+hot ||
            at<u8>(nm_best,r64(8*FN_))!=cd+cold) sys::fallback("scratch layout");
        if (sh.hot) {
            std::memset(last,0,r64(8*(CBOUND+2)));std::memset(asst,0,r64(64*(nc+2)));
            std::memset(cred,0,r64(4*(nc+2)*nd));
        }
    }
};
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define PIPELINE_EVENTS_NATIVE 1
#define PIPELINE_EV_ATTR __attribute__((target("avx512f,avx512dq,avx512vl,avx512bw,avx512cd,avx2,bmi2,popcnt")))
#if defined(__clang__)
#pragma clang attribute push(PIPELINE_EV_ATTR,apply_to=function)
#else
#pragma GCC push_options
#pragma GCC target("avx512f,avx512dq,avx512vl,avx512bw,avx512cd,avx2,bmi2,popcnt")
#endif
namespace pipeline::events {
inline __m512i iota16() {return _mm512_setr_epi32(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);}
inline __m512i even64() {return _mm512_setr_epi64(0,2,4,6,8,10,12,14);}
inline __m512i join32(__m256i a,__m256i b) {return _mm512_inserti64x4(_mm512_castsi256_si512(a),b,1);}
inline __m256i get256(const void* p){return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));}
inline void put256(void* p,__m256i v){_mm256_storeu_si256(reinterpret_cast<__m256i*>(p),v);}
inline __m512i get512(const void* p){return _mm512_loadu_si512(p);}
inline void put512(void* p,__m512i v){_mm512_storeu_si512(p,v);}
inline u32 lane_tail(std::size_t n) {return n>=16 ? 65535:(1u<<n)-1;}
inline __m256i mul32_256(__m256i a,__m256i b) {
#if defined(__GNUC__) || defined(__clang__)
    __m256i out;asm("vpmuludq %2, %1, %0":"=v"(out):"v"(a),"v"(b));return out;
#else
    return _mm256_mul_epu32(a,b);
#endif
}
inline constexpr double NAME_C=(1.0/1679616.0)*(1.0-1.0/1099511627776.0);
inline __m256i key_chars4(__m256i best) {
    const auto norm = _mm256_srli_epi64(best, 4);
    const auto len = _mm256_and_si256(best, _mm256_set1_epi64x(15));
    const auto d = _mm256_set1_epi64x(1679616);
    auto hi = _mm256_cvttpd_epu64(_mm256_mul_pd(_mm256_cvtepu64_pd(norm), _mm256_set1_pd(NAME_C)));
    auto lo = _mm256_sub_epi64(norm, mul32_256(hi, d));
    const auto fix = _mm256_cmpge_epu64_mask(lo, d);
    hi = _mm256_mask_add_epi64(hi, fix, hi, _mm256_set1_epi64x(1));
    lo = _mm256_mask_sub_epi64(lo, fix, lo, d);
    const auto m = _mm256_set1_epi64x(207127);
    const auto c1296 = _mm256_set1_epi64x(1296);
    const auto h1 = _mm256_srli_epi64(mul32_256(_mm256_srli_epi64(hi, 4), m), 24);
    const auto h0 = _mm256_sub_epi64(hi, mul32_256(h1, c1296));
    const auto l1 = _mm256_srli_epi64(mul32_256(_mm256_srli_epi64(lo, 4), m), 24);
    const auto l0 = _mm256_sub_epi64(lo, mul32_256(l1, c1296));
    const auto p = _mm256_or_si256(_mm256_or_si256(h1, _mm256_slli_epi64(h0, 16)), _mm256_or_si256(_mm256_slli_epi64(l1, 32), _mm256_slli_epi64(l0, 48)));
    const auto d1 = _mm256_mulhi_epu16(p, _mm256_set1_epi16(1821));
    const auto d0 = _mm256_sub_epi16(p, _mm256_mullo_epi16(d1, _mm256_set1_epi16(36)));
    const auto dg = _mm256_or_si256(d1, _mm256_slli_epi16(d0, 8));
    const auto ch = _mm256_add_epi8(_mm256_add_epi8(dg, _mm256_set1_epi8(48)), _mm256_and_si256(_mm256_cmpgt_epi8(dg, _mm256_set1_epi8(9)), _mm256_set1_epi8(39)));
    const auto sh = _mm256_sub_epi64(_mm256_set1_epi64x(64), _mm256_slli_epi64(len, 3));
    return _mm256_and_si256(ch, _mm256_srlv_epi64(_mm256_set1_epi64x(-1), sh));
}
inline __m256i name_key4v(u64 ukey,__m256i v) {
    constexpr u64 A=keyrec::A,B=keyrec::B,M40=keyrec::MASK;
    const auto one = _mm256_set1_epi64x(1);
    const auto m40 = _mm256_set1_epi64x(M40);
    auto m = _mm256_and_si256(_mm256_xor_si256(_mm256_mullo_epi64(v, _mm256_set1_epi64x(A)), _mm256_set1_epi64x(ukey)), m40);
    m = _mm256_xor_si256(m, _mm256_srli_epi64(m, 17));
    const auto y = _mm256_and_si256(_mm256_mullo_epi64(m, _mm256_set1_epi64x(B)), m40);
    const auto bits = _mm256_sub_epi64(_mm256_set1_epi64x(64), _mm256_lzcnt_epi64(_mm256_or_si256(y, one)));
    const auto g = _mm256_add_epi64(_mm256_srli_epi64(_mm256_mul_epu32(_mm256_sub_epi64(bits, one), _mm256_set1_epi64x(99)), 9), one);
    const auto pow_a = _mm512_loadu_si512(POW36.data()+1);
    const auto pow_b = _mm512_loadu_si512(POW36.data());
    const auto p = _mm512_castsi512_si256(_mm512_permutexvar_epi64(_mm512_zextsi256_si512(_mm256_sub_epi64(g, one)), pow_a));
    const auto len = _mm256_mask_add_epi64(g, _mm256_cmpge_epu64_mask(y, p), g, one);
    const auto fac = _mm512_castsi512_si256(_mm512_permutexvar_epi64(_mm512_zextsi256_si512(_mm256_sub_epi64(_mm256_set1_epi64x(8), len)), pow_b));
    return _mm256_or_si256(_mm256_slli_epi64(_mm256_mullo_epi64(y, fac), 4), len);
}
inline __m256i name_key4(u64 ukey,u32 u,__m256i al) {
        return name_key4v(ukey, _mm256_or_si256(_mm256_set1_epi64x(((u64(u)) << 9)), al));
}
inline u64 min4(__m256i k,u32 n) {
    k = _mm256_mask_mov_epi64(_mm256_set1_epi64x(-1), ((1u << n) - 1), k);
    const auto a = _mm256_min_epu64(k, _mm256_permute4x64_epi64(k, 0b01001110));
    const auto b = _mm256_min_epu64(a, _mm256_permute4x64_epi64(a, 0b00000001));
    return _mm256_extract_epi64(b, 0);
}
inline auto fast_name_y(u64 ukey,u32 u,u32 klass) {
    const auto n=u32(std::popcount(klass));
    if (!n || n>8) return fast_name(ukey,u,klass);
    const auto al=_mm512_maskz_compress_epi32(klass,iota16());
    auto best=min4(name_key4(ukey,u,_mm256_cvtepu32_epi64(_mm512_castsi512_si128(al))),std::min(n,4u));
    if (n>4) best=std::min(best,min4(name_key4(ukey,u,_mm256_cvtepu32_epi64(_mm512_extracti32x4_epi32(al,1))),n-4));
    return key_chars(best);
}
inline auto fast_name_v(u64 ukey,u32 u,u32 klass) {
    const auto n=u32(std::popcount(klass));
    if (!n || n>8) return fast_name(ukey,u,klass);
    const auto one=_mm512_set1_epi64(1),m40=_mm512_set1_epi64(keyrec::MASK);
    const auto al=_mm512_cvtepu32_epi64(_mm512_castsi512_si256(_mm512_maskz_compress_epi32(klass,iota16())));
    const auto v=_mm512_or_si512(_mm512_set1_epi64(u64(u)<<9),al);
    auto m=_mm512_and_si512(_mm512_xor_si512(_mm512_mullo_epi64(v,_mm512_set1_epi64(keyrec::A)),_mm512_set1_epi64(ukey)),m40);
    m=_mm512_xor_si512(m,_mm512_srli_epi64(m,17));
    const auto y=_mm512_and_si512(_mm512_mullo_epi64(m,_mm512_set1_epi64(keyrec::B)),m40);
    const auto bits=_mm512_sub_epi64(_mm512_set1_epi64(64),_mm512_lzcnt_epi64(_mm512_or_si512(y,one)));
    const auto g=_mm512_add_epi64(_mm512_srli_epi64(_mm512_mul_epu32(_mm512_sub_epi64(bits,one),_mm512_set1_epi64(99)),9),one);
    const auto p=_mm512_permutexvar_epi64(_mm512_sub_epi64(g,one),get512(POW36.data()+1));
    const auto len=_mm512_mask_add_epi64(g,_mm512_cmpge_epu64_mask(y,p),g,one);
    const auto fac=_mm512_permutexvar_epi64(_mm512_sub_epi64(_mm512_set1_epi64(8),len),get512(POW36.data()));
    return key_chars(_mm512_mask_reduce_min_epu64((1u<<n)-1,_mm512_or_si512(_mm512_slli_epi64(_mm512_mullo_epi64(y,fac),4),len)));
}
[[gnu::noinline]] inline Seg edge_copy_k(std::size_t tid,u32 u,Fast& fx,std::size_t a,std::size_t n) {
    auto [dst,seg]=fx.edge->seg(tid,u,n);
    if (!n) return seg;
    auto blk=kept_block(fx.blkk,a),at=std::size_t(fx.blkk[blk]);
    const auto end=a+n;auto p=dst;
    while (at<end) {
        if (blk>=BSIZE/8) sys::fallback("kept records");
        const u8 m=fx.keepm[blk];const auto cnt=std::size_t(std::popcount(m));
        if (at>=a && at+cnt<=end) {
            auto r0=get512(fx.slots+8*blk),r1=get512(fx.slots+8*blk+4);
            put512(p,_mm512_maskz_compress_epi64(m,_mm512_permutex2var_epi64(r0,even64(),r1)));p+=cnt;
        } else {
            auto j=at;
            for (auto mm=m;mm;mm&=mm-1,++j)
                if (j>=a && j<end) *p++=fx.slots[8*blk+std::countr_zero(mm)].w0;
        }
        at+=cnt;++blk;
    }
    if (p!=dst+n) sys::fallback("kept records");
    return seg;
}
[[gnu::noinline]] inline void flush_names(Fast& fx,core::Core& c,u64 ukey,std::size_t row_cap,std::size_t& np,std::size_t& na) {
    for(std::size_t p=0;p<np;p+=4) put256(fx.nm_k+p,name_key4v(ukey,get256(fx.nm_v+p)));
    for(std::size_t p=0;p<np;++p) {
        auto& b=fx.nm_best[fx.nm_r[p]];b=std::min(b,fx.nm_k[p]);
    }
    for(std::size_t a=0;a<na;a+=4) put256(fx.nm_k+a,key_chars4(get256(fx.nm_best+a)));
    auto nl=c.names.len();const auto no=c.names_off.len();
    c.names.reserve_total(nl+9*na+32);c.names_off.reserve_total(no+na+1);
    const auto ncap=std::min({c.names.cap(),row_cap*10,std::size_t(UINT32_MAX)});
    for (std::size_t a=0;a<na;++a) {
        if (nl+16>ncap) sys::fallback("avec full");
        const auto b=fx.nm_best[a],ch=b==UINT64_MAX?UINT64_MAX:fx.nm_k[a];
        auto q=c.names.as_mut_ptr()+nl;*q='u';std::memcpy(q+1,&ch,8);
        nl+=1+(b==UINT64_MAX?0:b&15);c.names_off.as_mut_ptr()[no+a]=u32(nl);
    }
    c.names_off.truncate(no+na);c.names.truncate(nl);np=na=0;
}
[[gnu::noinline]] inline void light_flush(Fast& fx,std::size_t nw,u32 org) {
    const auto week=_mm512_set1_epi32(WEEK_MS);std::size_t nl=0;
    auto flush=[&]{for(std::size_t j=0;j<nl;++j) ++fx.asst[fx.alist[j]>>4].v[fx.alist[j]&15];nl=0;};
    for(std::size_t i=0;i<nw;++i) {
        auto x=fx.wl_s[i];const auto& rc=fx.rc[fx.wl_r[i]/sizeof(Rc)];
        const auto sv=_mm512_set1_epi32(fx.sts[x]),dv=_mm512_set1_epi32(fx.sday[x]);
        const u32 win=_mm512_cmple_epu32_mask(_mm512_sub_epi32(_mm512_load_si512(rc.ts),sv),week)&rc.vm;
        const auto cv=rc.camp[31-std::countl_zero(win|1u)]>>4;
        fx.scamp[x]=win?cv:org;
        const auto am=u32(_mm512_cmplt_epu32_mask(_mm512_load_si512(rc.conf),_mm512_set1_epi32(win&-win)))&win;
        const auto cells=_mm512_add_epi32(_mm512_load_si512(rc.camp),dv);
        put512(fx.alist+nl,_mm512_maskz_compress_epi32(am,cells));nl+=std::popcount(am);
        if (nl>AL_CAP) flush();
    }
    flush();
}
template<bool All> inline void run_clicks_k(Fast& fx,std::span<const std::array<u64,2>> cks,u32 klass,
                                              std::size_t s0,std::size_t nsr,u32 org) {
    auto* last=reinterpret_cast<u32*>(fx.last);
    if (std::size_t(fx.gbase)+nsr+1>=(1ULL<<31)) {std::memset(last,0,4*(CBOUND+2));fx.gbase=0;}
    const auto g0=std::size_t(fx.gbase);fx.gbase=u32(g0+nsr+1);
    auto* sts=fx.sts+s0;auto* sc=fx.scamp+s0;
    if (fx.f.size()<nsr+1) fx.f.resize(nsr+1);
    auto* f=fx.f.data();
    auto acc=_mm512_setzero_si512();
    _mm512_store_si512(f,acc);
    for(std::size_t x=0;x<nsr;++x) {
        acc=_mm512_add_epi32(acc,_mm512_load_si512(&ONEHOT[fx.sday[s0+x]]));
        _mm512_store_si512(f+x+1,acc);
    }
    const auto saved=get512(sts+nsr);put512(sts+nsr,_mm512_set1_epi32(-1));
    const auto orgv=_mm512_set1_epi32(org),week=_mm512_set1_epi32(WEEK_MS);
    auto lo_load=[&](__m512i v) PIPELINE_EV_ATTR {return _mm512_max_epu32(v,_mm512_add_epi32(v,week));};
    std::size_t lo_p=0,hi_p=0,lob=0,hib=0;
    auto cpv=orgv,hiv=get512(sts),lov=lo_load(hiv);
    const auto cl=klass|(klass<<16);
    for (std::size_t k=0;k<cks.size();++k) {
        const u64 k0=cks[k][0];
        if constexpr (!All) if (!((cl>>(k0&31))&1)) continue;
        const auto c=(k0>>4)&(CBOUND-1);
        if (k+4<cks.size()) __builtin_prefetch(fx.asst+((cks[k+4][0]>>4)&(CBOUND-1)));
        const auto tv=_mm512_set1_epi32(k0>>32);
        auto m=_mm512_cmple_epu32_mask(hiv,tv);
        while (m==0xffff) {hib+=16;hiv=get512(sts+hib);m=_mm512_cmple_epu32_mask(hiv,tv);}
        const auto hi=hib+std::popcount(m);
        m=_mm512_cmplt_epu32_mask(lov,tv);
        while (m==0xffff) {lob+=16;lov=lo_load(get512(sts+lob));m=_mm512_cmplt_epu32_mask(lov,tv);}
        const auto lo=lob+std::popcount(m),to=std::min(lo,hi_p);
        put512(sc+lo_p,cpv);
        for (auto x=lo_p+16;x<to;x+=16) put512(sc+x,cpv);
        put512(sc+to,orgv);lo_p=lo;hi_p=hi;cpv=_mm512_set1_epi32(c);
        const auto from=std::max(lo+g0,std::size_t(last[c]))-g0;last[c]=u32(g0+hi);
        const auto d=_mm512_sub_epi32(_mm512_load_si512(f+hi),_mm512_load_si512(f+from));
        _mm512_store_si512(fx.asst+c,_mm512_add_epi32(_mm512_load_si512(fx.asst+c),d));
    }
    const auto to=std::min(nsr,hi_p);
    auto x=lo_p;do {put512(sc+x,cpv);x+=16;} while(x<to);
    put512(sc+to,orgv);put512(sts+nsr,saved);
}
[[gnu::noinline]] inline void run_clicks(Fast& fx,std::span<const std::array<u64,2>> cks,u32 klass,
                                          std::size_t s0,std::size_t nsr,u32 org) {
    const auto n=cks.size();
    const auto classv=_mm512_setr_epi64(klass,-1,klass,-1,klass,-1,klass,-1);
    const auto am=_mm512_setr_epi64(15,0,15,0,15,0,15,0),one=_mm512_set1_epi64(1);
    auto out=_mm512_setzero_si512();std::size_t i=0;
    for(;i+4<=n;i+=4) {
        auto a=_mm512_and_si512(get512(cks.data()+i),am);
        out=_mm512_ternarylogic_epi64(out,_mm512_sllv_epi64(one,a),classv,0xf4);
    }
    if (i<n) {
        const __mmask8 lm=(1u<<(2*(n-i)))-1;
        const auto a=_mm512_and_si512(_mm512_maskz_loadu_epi64(lm,cks.data()+i),am);
        out=_mm512_ternarylogic_epi64(out,_mm512_maskz_mov_epi64(lm,_mm512_sllv_epi64(one,a)),classv,0xf4);
    }
    if (!_mm512_test_epi64_mask(out,out)) run_clicks_k<true>(fx,cks,klass,s0,nsr,org);
    else run_clicks_k<false>(fx,cks,klass,s0,nsr,org);
}
struct PassB {__m512i tlast,ulast;u32 carry{},cur{};std::size_t ns{},nmix{};};
inline void pass_b_step(PassB& st,Fast& fx,std::size_t i,u32 tail,const u16* utab) {
    const auto even=_mm512_setr_epi32(0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30);
    const auto odd=_mm512_setr_epi32(1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31);
    const auto q0=get512(fx.q+i),q1=get512(fx.q+i+8);
    const auto tv=_mm512_permutex2var_epi32(q0,even,q1),uv=_mm512_permutex2var_epi32(q0,odd,q1);
    const auto tpv=_mm512_alignr_epi32(tv,st.tlast,15),upv=_mm512_alignr_epi32(uv,st.ulast,15);
    st.tlast=tv;st.ulast=uv;
    const auto x=_mm512_xor_si512(uv,upv),c15=_mm512_set1_epi32(15);
    const u32 rs=_mm512_test_epi32_mask(x,_mm512_set1_epi32(0x7ffffff0))&tail,acm=_mm512_test_epi32_mask(x,c15);
    const u32 gap=_mm512_cmpgt_epu32_mask(_mm512_sub_epi32(tv,tpv),_mm512_set1_epi32(GAP_MS));
    const u32 ss=(rs|gap)&tail,un=_mm512_cmplt_epu32_mask(tv,tpv)&~rs;
    auto ut=_mm512_set1_epi32(st.cur);
    for (auto m=rs;m;m&=m-1) {
        const auto p=std::countr_zero(m);st.cur=utab[fx.q[i+p]>>36];
        ut=_mm512_mask_mov_epi32(ut,0xffffu<<p,_mm512_set1_epi32(st.cur));
    }
    const auto abit=_mm512_sllv_epi32(_mm512_set1_epi32(1),_mm512_and_si512(uv,c15));
    const u32 bad=_mm512_test_epi32_mask(abit,_mm512_andnot_si512(ut,_mm512_set1_epi32(MASK)));
    const u32 badp=((bad<<1)|st.carry)&65535;st.carry=(bad>>15)&1;
    for(auto m=(un|((acm&~rs)&(bad|badp)))&tail;m;m&=m-1)
        fx.mix[st.nmix++]=u32(i+std::countr_zero(m));
    const auto base=_mm512_add_epi32(_mm512_set1_epi32(i),iota16());
    const auto idxf=_mm512_mask_or_epi32(base,rs,base,_mm512_set1_epi32(FLAG));
    put512(fx.sidx+st.ns,_mm512_maskz_compress_epi32(ss,idxf));
    put512(fx.sts+st.ns,_mm512_maskz_compress_epi32(ss,tv));
    fx.blk_ns[i>>4]=u32(st.ns);fx.ssb[i>>4]=u16(ss);st.ns+=std::popcount(ss);
}
[[gnu::noinline]] inline std::size_t pass_b(Fast& fx,std::size_t k,const u16* utab) {
    PassB st{_mm512_setzero_si512(),_mm512_set1_epi32(0x40000000)};
    if (fx.mix.size()<k+1) fx.mix.resize(k+1);
    std::size_t i=0;for (;i+16<=k;i+=16) pass_b_step(st,fx,i,65535,utab);
    if (i<k) pass_b_step(st,fx,i,lane_tail(k-i),utab);
    fx.mix[st.nmix]=UINT32_MAX;
    return st.ns;
}
[[gnu::noinline]] inline void purch_scan(Fast& fx,std::array<u64,2>* aggp,std::size_t nd,std::size_t np) {
    for(std::size_t j=0;j<np;++j) {
        const auto sl=fx.plist[j];const auto wv=fx.slots[sl].w0;
        const auto i=fx.blkk[sl>>3]+std::popcount(_bzhi_u32(fx.keepm[sl>>3],sl&7));
        const auto sid=fx.blk_ns[i>>4]+std::popcount(_bzhi_u32(fx.ssb[i>>4],(i&15)+1))-1;
        const auto cell=fx.scamp[sid]*nd+fx.sday[sid];
        const auto row=fx.blk_nrow[sid>>4]+std::popcount(_bzhi_u32(fx.rwb[sid>>4],(sid&15)+1))-1;
        __builtin_prefetch(aggp+cell);fx.p_cell[j]=u32(cell);fx.p_sid[j]=sid;fx.p_row[j]=row;fx.p_w[j]=u32((wv>>4)&0x1fffffff);
    }
}
[[gnu::noinline]] inline void purch_add(Fast& fx,std::array<u64,2>* aggp,std::size_t np) {
    std::array<u64,32> csum{};std::array<u32,32> crow;crow.fill(UINT32_MAX);
    u32 prev=UINT32_MAX,cur=UINT32_MAX;u64 best=0;
    for(std::size_t j=0;j<np;++j) {
        const auto w=fx.p_w[j],cat=w&31,sid=fx.p_sid[j],cell=fx.p_cell[j],row=fx.p_row[j];
        const auto price=u64(w>>5);aggp[cell][0]+=sid!=prev;aggp[cell][1]+=price;prev=sid;
        const auto v=(crow[cat]==row?csum[cat]:0)+price;csum[cat]=v;crow[cat]=row;
        if (cur!=row) best=0;
        cur=row;best=std::max(best,(v<<5)|(31-cat));
        fx.row_rev[row]=u32(std::min(u64{0x80000000},fx.row_rev[row]+price));
        fx.row_cat[row]=u8(31-(best&31));
    }
}
struct Walk {
    std::size_t cw{};u64 cb{};std::size_t nw{},nrc{},nm_np{},nm_na{},mixp{};
    u64 c_runs{},c_simple{},c_sess{};
};
template<class G> [[gnu::noinline]] std::size_t walk_runs(Fast& fx,const G& g,Walk& wk,std::size_t r,
                                                           std::size_t nrun,u32 pidx0,u32 org) {
    const auto orgv=_mm512_set1_epi32(org);
    auto cw=wk.cw;auto cb=wk.cb;auto nw=wk.nw,nrc=wk.nrc,np=wk.nm_np,na=wk.nm_na,mixp=wk.mixp;
    u64 cnt=0,simple=0,sess=0;
    for (;r<nrun;++r) {
        const auto s0=fx.run_s0[r],s1=fx.run_s0[r+1],a=fx.sidx[s0]&M31,e=fx.sidx[s1]&M31;
        const auto qa=fx.q[a];const auto u=u32(qa>>36);const auto tv=g.utab[u];
        while (fx.mix[mixp]<a) ++mixp;
        if (!r || r+1==nrun || fx.mix[mixp]<e || (tv&(COMPLEX|EXTRA))) break;
        ++cnt;simple+=e-a;
        if ((u>>6)!=cw) {claim_word(g,cw,cb);cw=u>>6;cb=0;}
        const u64 ub=u64{1}<<(u&63);
        if (cb&ub) sys::fallback("a user has two runs");
        cb|=ub;
        const u32 mask=tv&MASK,bit=u32{1}<<((qa>>32)&15),klass=(bit&mask)?mask:bit;
        const auto ncl=std::size_t(std::popcount(klass));
        fx.nm_best[na]=UINT64_MAX;fx.run_pidx[r]=pidx0+u32(na);
        if (ncl-1>=8) fx.nm_best[na]=name_key_scalar(g.ukey,u,klass);
        else {
            const auto al=_mm512_cvtepu32_epi64(_mm512_castsi512_si256(_mm512_maskz_compress_epi32(klass,iota16())));
            put512(fx.nm_v+np,_mm512_or_si512(_mm512_set1_epi64(u64(u)<<9),al));
            put256(fx.nm_r+np,_mm256_set1_epi32(na));np+=ncl;
        }
        ++na;sess+=s1-s0;
        for(auto x=s0;x<s1;x+=16) put512(fx.scamp+x,orgv);
        const auto cks=g.csr_range(u);const auto n=cks.size();const auto nsr=s1-s0;
        bool light=!n;
        if (n && n<=16 && nsr<=WL_RUN) {
            if (nrc==RC_N || nw+nsr>WL_CAP) {light_flush(fx,nw,org);nw=0;nrc=0;}
            const auto* p=cks.data();
            __m512i ka,kb;
            bool padded=false;
            if constexpr(requires{g.csr_cap;g.ncsr;}) {
                // Published CSR buffers include sixteen guard records. Small stand-alone ranges
                // use a masked boundary load when they do not share that padded storage.
                const auto off=std::size_t(p-g.csr);
                padded=off<=g.csr_cap && g.csr_cap-off>=16;
            }
            if (padded) {
                ka=_mm512_permutex2var_epi64(get512(p),even64(),get512(p+4));
                kb=_mm512_permutex2var_epi64(get512(p+8),even64(),get512(p+12));
            } else {
                auto load=[&](unsigned off) PIPELINE_EV_ATTR {
                    if (off+4<=n) return get512(p+off);
                    if (off>=n) return _mm512_setzero_si512();
                    return _mm512_maskz_loadu_epi64((1u<<(2*(n-off)))-1,p+off);
                };
                ka=_mm512_permutex2var_epi64(load(0),even64(),load(4));
                kb=_mm512_permutex2var_epi64(load(8),even64(),load(12));
            }
            const auto lo=join32(_mm512_cvtepi64_epi32(ka),_mm512_cvtepi64_epi32(kb));
            const auto ts=join32(_mm512_cvtepi64_epi32(_mm512_srli_epi64(ka,32)),_mm512_cvtepi64_epi32(_mm512_srli_epi64(kb,32)));
            const auto vm=(1u<<n)-1;
            const auto inc=_mm512_test_epi32_mask(_mm512_sllv_epi32(_mm512_set1_epi32(1),_mm512_and_si512(lo,_mm512_set1_epi32(15))),_mm512_set1_epi32(klass));
            if ((inc&vm)==vm) {
                light=true;
                const auto camp=_mm512_and_si512(lo,_mm512_set1_epi32((CBOUND-1)<<4));
                auto& rc=fx.rc[nrc];_mm512_store_si512(rc.ts,ts);_mm512_store_si512(rc.camp,camp);
                _mm512_store_si512(rc.conf,_mm512_conflict_epi32(camp));rc.vm=vm;
                for (std::size_t x=0;x<nsr;x+=16) {
                    put512(fx.wl_s+nw+x,_mm512_add_epi32(_mm512_set1_epi32(s0+x),iota16()));
                    put512(fx.wl_r+nw+x,_mm512_set1_epi32(nrc*sizeof(Rc)));
                }
                ++nrc;nw+=nsr;
            }
        }
        if (!light) run_clicks(fx,cks,klass,s0,nsr,org);
    }
    wk.cw=cw;wk.cb=cb;wk.nw=nw;wk.nrc=nrc;wk.nm_np=np;wk.nm_na=na;wk.mixp=mixp;
    wk.c_runs+=cnt;wk.c_simple+=simple;wk.c_sess+=sess;return r;
}
template<class T> inline std::size_t pass_a(Fast& fx,T& worker,u64 gen) {
    const auto inv=_mm512_set1_epi64(INVALID_TS),m16=_mm512_set1_epi64(65535),m15=_mm512_set1_epi64(15);
    const auto oddi=_mm512_setr_epi64(1,3,5,7,9,11,13,15),pmask=_mm512_set1_epi64(u64{0xffffff}<<9);
    auto tags=_mm512_add_epi64(_mm512_setr_epi64(0,1,2,3,4,5,6,7),_mm512_set1_epi64(gen));
    std::size_t k=0;
    auto step=[&](std::size_t blk) PIPELINE_EV_ATTR {
        const auto r0=get512(fx.slots+8*blk),r1=get512(fx.slots+8*blk+4);
        const auto w0=_mm512_permutex2var_epi64(r0,even64(),r1),w1=_mm512_permutex2var_epi64(r0,oddi,r1);
        const auto occ=_mm512_testn_epi64_mask(_mm512_sub_epi64(w1,tags),m16);
        tags=_mm512_add_epi64(tags,_mm512_set1_epi64(8));
        const auto ts=_mm512_srli_epi64(w0,33);
        const auto valid=_mm512_mask_cmpneq_epu64_mask(occ,ts,inv);
        const u8 odd=valid&_mm512_movepi64_mask(w1);
        if (odd) odd_records(worker,fx.slots,blk,odd);
        const u8 keep=valid&~odd;
        const auto ua=_mm512_ternarylogic_epi64(_mm512_srli_epi64(w1,33),w0,m15,0xd8);
        put512(fx.q+k,_mm512_maskz_compress_epi64(keep,_mm512_or_si512(_mm512_slli_epi64(ua,32),ts)));
        fx.blkk[blk]=u16(k);fx.keepm[blk]=keep;fx.pkm[blk]=keep&_mm512_test_epi64_mask(w0,pmask);
        k+=std::popcount(keep);
    };
    for(std::size_t b=0;b<BSIZE/8;b+=2) {step(b);step(b+1);}
    fx.blkk[BSIZE/8]=u16(k);return k;
}
inline std::pair<std::size_t,std::size_t> pass_c(Fast& fx,u64 ws_mod,std::size_t ns,std::size_t k) {
    constexpr u64 DIV_M=6515625;
    const auto wsm=_mm512_set1_epi32(ws_mod); const auto mg=_mm256_set1_epi64x(DIV_M);
    const auto flag=_mm512_set1_epi32(FLAG),m31=_mm512_set1_epi32(M31);
    auto div=[&](__m256i v) PIPELINE_EV_ATTR {
        const auto pe=_mm256_srli_epi64(mul32_256(v,mg),39);
        const auto po=_mm256_srli_epi64(mul32_256(_mm256_srli_epi64(v,32),mg),39);
        return _mm256_or_si256(pe,_mm256_slli_epi64(po,32));
    };
    fx.sday[-1]=UINT32_MAX;std::size_t nrow=0;
    for (std::size_t s=0;s<ns;s+=16) {
        const auto x=_mm512_srli_epi32(_mm512_add_epi32(get512(fx.sts+s),wsm),10);
        const auto day=join32(div(_mm512_castsi512_si256(x)),div(_mm512_extracti64x4_epi64(x,1)));
        put512(fx.sday+s,day);
        const auto prev=get512(fx.sday+s-1),iv=get512(fx.sidx+s);
        const u16 rw=(_mm512_movepi32_mask(iv)|_mm512_cmpneq_epi32_mask(day,prev))&lane_tail(ns-s);
        fx.blk_nrow[s>>4]=u32(nrow);fx.rwb[s>>4]=rw;
        const auto ord=_mm512_or_si512(_mm512_add_epi32(_mm512_set1_epi32(s),iota16()),_mm512_and_si512(iv,flag));
        put512(fx.row_day+nrow,_mm512_maskz_compress_epi32(rw,day));
        put512(fx.row_s0+nrow,_mm512_maskz_compress_epi32(rw,ord));
        put512(fx.row_r0+nrow,_mm512_maskz_compress_epi32(rw,_mm512_and_si512(iv,m31)));
        nrow+=std::popcount(rw);
    }
    fx.row_s0[nrow]=u32(ns)|FLAG;fx.row_r0[nrow]=u32(k);
    std::size_t nrun=0;
    for (std::size_t r=0;r<nrow;r+=16) {
        const auto v=get512(fx.row_s0+r);
        const u16 m=_mm512_movepi32_mask(v)&lane_tail(nrow-r);
        put512(fx.run_s0+nrun,_mm512_maskz_compress_epi32(m,_mm512_and_si512(v,m31)));
        put512(fx.run_row0+nrun,_mm512_maskz_compress_epi32(m,_mm512_add_epi32(_mm512_set1_epi32(r),iota16())));
        nrun+=std::popcount(m);
    }
    fx.run_s0[nrun]=u32(ns);fx.run_row0[nrun]=u32(nrow);
    std::memset(fx.row_rev,0,4*nrow);std::memset(fx.row_cat,255,nrow);
    return {nrow,nrun};
}
inline void write_rows(Fast& fx,core::Core& c,i32 day0,std::size_t row_cap,std::size_t nrow,std::size_t nrun) {
    auto n=c.day.len();
    if (c.keys.len()!=n || c.ses.len()!=n || c.evs.len()!=n || c.rev.len()!=n || c.cat.len()!=n) sys::fallback("row columns differ in length");
    const auto need=n+nrow+16;
    c.keys.reserve_total(need);c.day.reserve_total(need);c.ses.reserve_total(need);c.evs.reserve_total(need);c.rev.reserve_total(need);c.cat.reserve_total(need);
    row_cap=std::min({row_cap,c.keys.cap(),c.day.cap(),c.ses.cap(),c.evs.cap(),c.rev.cap(),c.cat.cap()});
    const auto m31=_mm512_set1_epi32(M31);
    std::size_t r=0;
    while (r<nrun) {
        if (fx.run_pidx[r]==NONE) {++r;continue;}
        const auto ra=fx.run_row0[r];auto rb=ra;
        while (r<nrun && fx.run_pidx[r]!=NONE) {
            const auto xb=fx.run_row0[r+1];
            if (n+xb-ra+16>row_cap) sys::fallback("avec full");
            for(auto x=rb;x<xb;x+=16) put512(c.keys.as_mut_ptr()+n+x-ra,_mm512_set1_epi32(fx.run_pidx[r]));
            rb=xb;++r;
        }
        for(auto x=ra;x<rb;x+=16) {
            const auto o=n+x-ra;
            put512(c.day.as_mut_ptr()+o,_mm512_add_epi32(get512(fx.row_day+x),_mm512_set1_epi32(day0)));
            const auto sa=_mm512_and_si512(get512(fx.row_s0+x),m31),sb=_mm512_and_si512(get512(fx.row_s0+x+1),m31);
            put512(c.ses.as_mut_ptr()+o,_mm512_sub_epi32(sb,sa));
            put512(c.evs.as_mut_ptr()+o,_mm512_sub_epi32(get512(fx.row_r0+x+1),get512(fx.row_r0+x)));
            const auto rv=get512(fx.row_rev+x);
            if (_mm512_cmpgt_epu32_mask(rv,_mm512_set1_epi32(INT32_MAX))&lane_tail(rb-x)) sys::fallback("revenue_cents or events over i32");
            put512(c.rev.as_mut_ptr()+o,rv);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(c.cat.as_mut_ptr()+o),_mm_loadu_si128(reinterpret_cast<const __m128i*>(fx.row_cat+x)));
        }
        n+=rb-ra;
    }
    c.keys.truncate(n);c.day.truncate(n);c.ses.truncate(n);c.evs.truncate(n);c.rev.truncate(n);c.cat.truncate(n);
}
inline void fold_aggregates(Fast& fx,core::Core& c,std::size_t nc,std::size_t nd) {
    if (nd<=16 && c.agg.len()>=(nc+1)*nd) {
        auto* agg=reinterpret_cast<u32*>(c.agg.as_mut_ptr());
        const auto lo=_mm512_setr_epi32(0,16,1,17,2,18,3,19,4,20,5,21,6,22,7,23);
        const auto hi=_mm512_setr_epi32(8,24,9,25,10,26,11,27,12,28,13,29,14,30,15,31);
        const u16 ml=lane_tail(2*nd),mh=lane_tail(std::max<std::size_t>(2*nd,16)-16),mc=lane_tail(nd);
        for(std::size_t i=0;i<=nc;++i) {
            const auto cr=_mm512_maskz_loadu_epi32(mc,fx.cred+i*nd),a=_mm512_load_si512(fx.asst+i);
            auto* p=agg+2*i*nd;
            _mm512_mask_storeu_epi32(p,ml,_mm512_add_epi32(_mm512_maskz_loadu_epi32(ml,p),_mm512_permutex2var_epi32(cr,lo,a)));
            if (mh) {_mm512_mask_storeu_epi32(p+16,mh,_mm512_add_epi32(_mm512_maskz_loadu_epi32(mh,p+16),_mm512_permutex2var_epi32(cr,hi,a)));}
        }
    } else {
        for(std::size_t i=0;i<=nc;++i) for(std::size_t d=0;d<nd;++d) {
            auto& cell=c.agg[i*nd+d];cell[0]+=fx.cred[i*nd+d];cell[1]+=fx.asst[i].v[d];
        }
    }
}
template<class G> void stage3a(const G& g,std::size_t tid,Pool* pool,const Share& sh) {
    auto& t=g.t(tid);Fast fx(g.nc,g.nd,sh);fx.edge=Edge::start(tid,pool);
    const auto cells=t.core.agg.len();
    if (!t.n_runs && !t.n_complex && !t.core.n_sess && sh.agg && sh.aggp) {
        std::memset(sh.agg,0,8*cells);std::memset(sh.aggp,0,16*cells);
        t.core.agg.set_ptr(reinterpret_cast<std::array<u32,2>*>(sh.agg));
        t.core.aggp.set_ptr(reinterpret_cast<std::array<u64,2>*>(sh.aggp));
    }
    t.cx_raw.reserve(g.ev_bytes/64/g.nt/16);
    slab_cols(t.core,sh.slab,sh.slab_len);
    const auto nd=g.nd,dig_min=DIG_MIN.load(relaxed); const auto org=u32(g.nc),trash=u32(g.nc+1);
    u64 gen=0;
    using clock=std::chrono::steady_clock;
    auto ns_since=[](auto a,auto b){return std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();};
    for(;;) {
        const auto b=g.next.fetch_add(1,relaxed);if (b>=g.nb) break;
        const auto ta=clock::now();
        if (++gen>BSIZE) {std::memset(fx.slots,0,BSIZE*sizeof(Rec));gen=1;}
        bool any=false;
        auto scatter=[&](const Rec* p,std::size_t n,const i8* next){any=true;scatter_chunk(p,n,fx.slots,gen,next);};
        for(std::size_t th=0;th<g.nt;++th) {
            const auto& part=g.t(th).ev;
            if constexpr (requires{part.for_each_chunk(b,scatter);}) part.for_each_chunk(b,scatter);
            else part.for_each(b,[&](std::span<const Rec> records){
                const i8* next=nullptr;
                if (records.size()==4032/16) std::memcpy(&next,reinterpret_cast<const u8*>(records.data())+4032,sizeof(next));
                scatter(records.data(),records.size(),next);
            });
        }
        auto& info=g.edges[b];info.n=0;if (!any) continue;
        const auto tb=clock::now();const auto k=pass_a(fx,t,gen);
        const auto tc=clock::now();
        t.prof[0]+=ns_since(ta,tb);t.prof[1]+=ns_since(tb,tc);
        if (!k) continue;
        const auto ns=pass_b(fx,k,g.utab);fx.sidx[ns]=u32(k)|FLAG;
        const auto [nrow,nrun]=pass_c(fx,g.ws_mod,ns,k);
        Walk wk;std::array<DigRun,2> digs{};std::size_t ndig=0,r=0;
        for (;;) {
            r=walk_runs(fx,g,wk,r,nrun,u32(t.core.names_off.len()-1),org);
            if (r>=nrun) break;
            const auto s0=fx.run_s0[r],s1=fx.run_s0[r+1],a=fx.sidx[s0]&M31,e=fx.sidx[s1]&M31;
            const u32 u=fx.q[a]>>36;const auto tv=g.utab[u];
            const bool special=fx.mix[wk.mixp]<e,edge=!r || r+1==nrun;
            fx.run_pidx[r]=NONE;
            for(auto x=s0;x<s1;x+=16) put512(fx.scamp+x,_mm512_set1_epi32(trash));
            if (edge && !special && !(tv&COMPLEX) && e-a>=dig_min && 3+2*(s1-s0)+e-a<=EP_WORDS) {
                digs[ndig++]={!r,u,a,e,s0,s1};info.n=r?2:1;
            } else if (!r) {info.first=edge_copy_k(tid,u,fx,a,e-a);info.n=1;}
            else if (r+1==nrun) {info.last=edge_copy_k(tid,u,fx,a,e-a);info.n=2;}
            else {
                flush_names(fx,t.core,g.ukey,g.row_cap,wk.nm_np,wk.nm_na);
                auto buf=std::move(t.runbuf);buf.clear();
                for_kept(fx.slots,fx.blkk,fx.keepm,a,e-a,[&](u64 w){buf.push_back({w,u64(u)<<37});});
                process_run(g,t,u,buf);t.runbuf=std::move(buf);
            }
            ++r;
        }
        flush_names(fx,t.core,g.ukey,g.row_cap,wk.nm_np,wk.nm_na);
        t.n_runs+=wk.c_runs;t.n_simple+=wk.c_simple;t.core.n_sess+=wk.c_sess;
        light_flush(fx,wk.nw,org);claim_word(g,wk.cw,wk.cb);
        for(std::size_t s=0;s<ns;++s) ++fx.cred[fx.scamp[s]*nd+fx.sday[s]];
        auto* aggp=t.core.aggp.as_mut_ptr();
        std::size_t np=0;
        for(std::size_t wd=0;wd<BSIZE/64;++wd) {
            u64 pm;std::memcpy(&pm,fx.pkm+8*wd,8);const auto n=std::popcount(pm);const u32 base=64*wd;
            for(unsigned j=0;j<4;++j) {fx.plist[np+j]=u16(base+std::countr_zero(pm));pm&=pm-1;}
            for(auto j=np+4;pm;pm&=pm-1,++j) fx.plist[j]=u16(base+std::countr_zero(pm));
            __builtin_prefetch(fx.slots+fx.plist[np]);np+=n;
        }
        purch_scan(fx,aggp,nd,np);
        for(std::size_t d=0;d<ndig;++d) {
            const auto s=digest_put(tid,fx,digs[d],fx.q,fx.sidx,fx.sts,np);
            (digs[d].first?info.first:info.last)=s;
        }
        purch_add(fx,aggp,np);write_rows(fx,t.core,g.day0,g.row_cap,nrow,nrun);
        t.prof[2]+=ns_since(tc,clock::now());
    }
    slab_settle(t.core);fold_aggregates(fx,t.core,g.nc,nd);
}
}
#if defined(__clang__)
#pragma clang attribute pop
#else
#pragma GCC pop_options
#endif
#endif
