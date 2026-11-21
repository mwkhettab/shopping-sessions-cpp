#pragma once
#include "parse_sink.hpp"
#include "k_ref.hpp"
#include <cstring>

namespace pipeline::parse {
template<class C> [[gnu::noinline]] void link_slow(C& c,std::string_view buf,std::size_t s,std::size_t e) {
    const auto fields=k_ref::split_line<2>(buf,s,e); if(!fields) return;
    const auto [a,b]=*fields;
    auto left=buf.substr(a.first,a.second-a.first),right=buf.substr(b.first,b.second-b.first);
    if(left.empty() || right.empty()) return;
    ++c.t.n_lk_rows; if(left==right) return;
    const auto ia=k_ref::parse_id(buf,a,'u'),ib=k_ref::parse_id(buf,b,'u');
    if(!((ia|ib)&ID_ODD)) {
        const auto x=c.g.normal(ia),y=c.g.normal(ib);
        if(x && y && x->first==y->first) {
            const auto u=u64(x->first);
            c.t.us.push(u>>UB,(u64{1}<<63)|((u&(UBSIZE-1))<<43)|(u64(x->second)<<4)|y->second,0);
            return;
        }
    }
    const auto ta=(ia&ID_ODD) ? c.raw_token(c.base+a.first,left.size()) : ia;
    const auto tb=(ib&ID_ODD) ? c.raw_token(c.base+b.first,right.size()) : ib;
    c.t.gen_edges.emplace_back(ta,tb);
}
struct alignas(64) LkRow {
    std::array<u8,32> e{},m{};
    i64 offa{},offb{};
    // A recognized link shape has at least one base36 digit in each ID.
    u64 na=0,nb=0;
    std::array<u64,4> pad{};
};
static_assert(sizeof(LkRow)==128 && offsetof(LkRow,offa)==64 && offsetof(LkRow,na)==80);
inline constexpr auto LKP=[] {
    std::array<LkRow,32*16+33> table{};
    for(unsigned a=2;a<=11;++a) for(unsigned b=2;b<=11;++b) {
        const auto nl=a+1+b,qa=unsigned(a>=10),qb=unsigned(b>=10);
        auto& row=table[nl*16+a]; row.offa=1+qa;row.offb=a+2+qb;
        row.na=a-1-2*qa;row.nb=b-1-2*qb;
        auto byte=[&](unsigned i,u8 v){row.m[i]=255;row.e[i]=v;};
        if(qa){byte(0,'"');byte(a-1,'"');}
        if(qb){byte(a+1,'"');byte(nl-1,'"');}
        byte(qa,'u');byte(a+1+qb,'u');byte(a,',');byte(nl,'\n');
    }
    return table;
}();
struct alignas(64) LkBatch {std::array<u64,NB> a{},b{},na{},nb{},at{};};
inline std::pair<std::size_t,std::size_t> lk_shape(const char* p) {
    std::size_t nl=32,cm=32;
    for(std::size_t i=0;i<32;++i) {if(nl==32 && p[i]=='\n') nl=i; if(cm==32 && p[i]==',') cm=i;}
    return {nl,cm};
}
struct LkMasks {u64 regular{},gen{},decoded{};};
#if defined(__x86_64__)
PIPELINE_ROUTING_X64 inline int lk_loop_x64(const char*& p,const char* lim,LkBatch& lb,std::size_t& n) {
    const auto lf=_mm256_set1_epi8('\n'),cm=_mm256_set1_epi8(',');
    while(p<=lim) {
        const auto row=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        auto nl=std::countr_zero(u32(_mm256_movemask_epi8(_mm256_cmpeq_epi8(row,lf))));
        auto cp=std::countr_zero(u32(_mm256_movemask_epi8(_mm256_cmpeq_epi8(row,cm))));
        const auto& pat=LKP[nl*16+cp]; if(!pat.na) return 1;
        const auto diff=_mm256_xor_si256(row,_mm256_load_si256(reinterpret_cast<const __m256i*>(pat.e.data())));
        if(!_mm256_testz_si256(diff,_mm256_load_si256(reinterpret_cast<const __m256i*>(pat.m.data())))) return 1;
        std::memcpy(&lb.a[n],p+pat.offa,8);std::memcpy(&lb.b[n],p+pat.offb,8);
        lb.na[n]=pat.na;lb.nb[n]=pat.nb;lb.at[n]=std::uintptr_t(p);p+=nl+1;
        if(++n==NB) return 2;
    }
    return 0;
}
PIPELINE_ROUTING_INLINE __m256i b36_pairs(__m256i d) {
    return _mm256_madd_epi16(_mm256_maddubs_epi16(d,_mm256_set1_epi16(0x0124)),_mm256_set1_epi32(0x10510));
}
struct B36 {__m512i val; __mmask8 ok;};
PIPELINE_ROUTING_INLINE B36 b36_vec8(__m512i word,__m512i n) {
    auto sh=_mm512_slli_epi64(_mm512_sub_epi64(splat8(8),n),3);
    auto fill=_mm512_srlv_epi64(splat8(0x3030303030303030ull>>1),_mm512_sub_epi64(splat8(63),sh));
    auto w=_mm512_or_si512(_mm512_sllv_epi64(word,sh),fill);
    auto d0=_mm512_sub_epi8(w,_mm512_set1_epi8('0'));
    auto lower=_mm512_cmplt_epu8_mask(_mm512_sub_epi8(w,_mm512_set1_epi8('a')),_mm512_set1_epi8(26));
    auto good8=_mm512_cmplt_epu8_mask(d0,_mm512_set1_epi8(10))|lower;
    auto bad=_mm512_movm_epi8(~good8);
    auto d=_mm512_mask_sub_epi8(d0,lower,d0,_mm512_set1_epi8(39));
    auto qlo=b36_pairs(_mm512_castsi512_si256(d)),qhi=b36_pairs(_mm512_extracti64x4_epi64(d,1));
    auto q=_mm512_inserti64x4(_mm512_castsi256_si512(qlo),qhi,1);
    auto r=_mm512_add_epi64(mulu32_8(q,splat8(1679616)),_mm512_srli_epi64(q,32));
    auto canon=_mm512_cmpeq_epi64_mask(n,splat8(1))|_mm512_cmpneq_epi64_mask(_mm512_and_si512(word,splat8(255)),splat8('0'));
    return {r,__mmask8(_mm512_testn_epi64_mask(bad,bad)&canon&_mm512_cmple_epu64_mask(r,splat8(keyrec::MASK)))};
}
template<class C> PIPELINE_ROUTING_X64 LkMasks lk_packet(C& c,LkBatch& lb,std::size_t n) {
    LkMasks mask;
    const auto uk=splat8(c.g.ukey&keyrec::MASK),cap=splat8(c.g.ucap),line=splat8(std::uintptr_t(c.t.us.lines_ptr()));
    const auto even=_mm512_setr_epi64(0,8,1,9,2,10,3,11),odd=_mm512_setr_epi64(4,12,5,13,6,14,7,15);
    for(std::size_t j=0;j<n;j+=8) {
        auto [ia,ma]=b36_vec8(_mm512_load_si512(lb.a.data()+j),_mm512_load_si512(lb.na.data()+j));
        auto [ib,mb]=b36_vec8(_mm512_load_si512(lb.b.data()+j),_mm512_load_si512(lb.nb.data()+j));
        _mm512_store_si512(lb.a.data()+j,ia);_mm512_store_si512(lb.b.data()+j,ib);
        auto wa=decode8<false>(ia,uk),wb=decode8<false>(ib,uk);
        auto ua=_mm512_srli_epi64(wa,9),ub=_mm512_srli_epi64(wb,9);
        auto aa=_mm512_and_si512(wa,splat8(511)),ab=_mm512_and_si512(wb,splat8(511));
        auto ne=ma&mb&_mm512_cmpneq_epi64_mask(ia,ib);
        auto ok=ne&_mm512_cmplt_epu64_mask(ua,cap)&_mm512_cmplt_epu64_mask(ub,cap)&_mm512_cmplt_epu64_mask(aa,splat8(NALIAS))&_mm512_cmplt_epu64_mask(ab,splat8(NALIAS))&_mm512_cmpeq_epi64_mask(ua,ub);
        auto w0=_mm512_or_si512(splat8(u64{1}<<63),_mm512_or_si512(_mm512_slli_epi64(_mm512_and_si512(ua,splat8(UBSIZE-1)),43),_mm512_or_si512(_mm512_slli_epi64(aa,4),ab)));
        auto bucket=_mm512_srli_epi64(ua,UB),zero=_mm512_setzero_si512();
        _mm512_store_si512(c.out.rec.data()+j,_mm512_permutex2var_epi64(w0,even,zero));_mm512_store_si512(c.out.rec.data()+j+4,_mm512_permutex2var_epi64(w0,odd,zero));
        _mm512_store_si512(c.out.ob.data()+j,bucket);_mm512_store_si512(c.out.lp.data()+j,_mm512_add_epi64(line,_mm512_slli_epi64(bucket,6)));
        mask.regular|=u64(ok)<<j;mask.gen|=u64(ne&~ok)<<j;mask.decoded|=u64(ma&mb)<<j;
    }
    return mask;
}
#endif
inline int lk_loop(const char*& p,const char* lim,LkBatch& lb,std::size_t& n) {
#if defined(__x86_64__)
    if(routing_x64()) return lk_loop_x64(p,lim,lb,n);
#endif
    while(p<=lim) {
        const auto [nl,cm]=lk_shape(p);const auto& pat=LKP[nl*16+cm]; if(!pat.na) return 1;
        for(std::size_t i=0;i<32;++i) if((u8(p[i])^pat.e[i])&pat.m[i]) return 1;
        std::memcpy(&lb.a[n],p+pat.offa,8);std::memcpy(&lb.b[n],p+pat.offb,8);
        lb.na[n]=pat.na;lb.nb[n]=pat.nb;lb.at[n]=std::uintptr_t(p);p+=nl+1;
        if(++n==NB) return 2;
    }
    return 0;
}
template<class C> inline LkMasks lk_scalar(C& c,LkBatch& lb,std::size_t n) {
    LkMasks m;
    for(std::size_t i=0;i<n;++i) {
        auto [ia,ok1]=keyrec::base36_word(lb.a[i],u32(lb.na[i])); auto [ib,ok2]=keyrec::base36_word(lb.b[i],u32(lb.nb[i]));
        if(!ok1 || !ok2 || (ia|ib)>keyrec::MASK) continue;
        lb.a[i]=ia;lb.b[i]=ib;m.decoded|=u64{1}<<i;
        if(ia==ib) continue;
        auto a=c.g.normal(ia),b=c.g.normal(ib);
        if(!a || !b || a->first!=b->first) {m.gen|=u64{1}<<i;continue;}
        const u64 u=a->first,ub=u>>UB;
        c.out.rec[i]={(u64{1}<<63)|((u&(UBSIZE-1))<<43)|(u64(a->second)<<4)|b->second,0};
        c.out.ob[i]=ub;c.out.lp[i]=std::uintptr_t(c.t.us.lines_ptr())+(ub<<6);m.regular|=u64{1}<<i;
    }
    return m;
}
template<class C> inline void lk_batch(C& c,std::string_view buf,LkBatch& lb,std::size_t n) {
    if(!n) return;
    const auto m=
#if defined(__x86_64__)
       routing_x64() ? lk_packet(c,lb,n) :
#endif
       lk_scalar(c,lb,n);
    const auto active=active_mask(n),decoded=m.decoded&active;
    c.t.us.push_many(m.regular&active,n,c.out.ob.data(),c.out.lp.data(),c.out.rec.data());
    c.t.n_lk_rows+=std::popcount(decoded);
    for(auto bits=active&~decoded;bits;bits&=bits-1) {
        const auto s=lb.at[std::countr_zero(bits)]-std::uintptr_t(buf.data());
        link_slow(c,buf,s,buf.find('\n',s));
    }
    for(auto bits=m.gen&active;bits;bits&=bits-1) {auto i=std::countr_zero(bits); c.t.gen_edges.emplace_back(lb.a[i],lb.b[i]);}
}
template<class C> inline void links_chunk(C& c,std::string_view buf) {
    LkBatch lb; std::size_t n=0,pos=0;
    if(buf.size()>=40) {
        auto p=buf.data(); const auto lim=p+buf.size()-40;
        for(;;) {
            const auto code=lk_loop(p,lim,lb,n);pos=std::size_t(p-buf.data());
            if(code==2) {lk_batch(c,buf,lb,n);n=0;}
            else if(code==1) {auto e=buf.find('\n',pos);if(e==buf.npos) break;link_slow(c,buf,pos,e);p=buf.data()+e+1;}
            else break;
        }
    }
    lk_batch(c,buf,lb,n);
    while(pos<buf.size()) {auto e=buf.find('\n',pos); if(e==buf.npos) e=buf.size();link_slow(c,buf,pos,e);pos=e+1;}
}
}
