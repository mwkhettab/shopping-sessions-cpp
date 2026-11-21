#include "k_free.hpp"
#if defined(__x86_64__)
#include "k_free_tables.hpp"
#include <bit>
#include <cstring>
#include <immintrin.h>

#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("avx2,bmi,bmi2,lzcnt,popcnt,avx512f,avx512bw,avx512vl,avx512dq"))),apply_to=function)
#else
#pragma GCC push_options
#pragma GCC target("avx2,bmi,bmi2,lzcnt,popcnt,avx512f,avx512bw,avx512vl,avx512dq")
#endif

namespace pipeline::k_free::detail {
namespace {
using V=__m128i;
using W=__m256i;
using P=const u8 *;
template<class S> inline S read(const void *p) { S v; std::memcpy(&v,p,sizeof(v)); return v; }
template<class S> inline void write(void *p,S v) { std::memcpy(p,&v,sizeof(v)); }
inline V ld(const void *p) { return _mm_loadu_si128(static_cast<const V *>(p)); }
inline W ld2(const void *p) { return _mm256_load_si256(static_cast<const W *>(p)); }
inline W lu2(const void *p) { return _mm256_loadu_si256(static_cast<const W *>(p)); }
inline W join(V a,V b) { return _mm256_inserti128_si256(_mm256_castsi128_si256(a),b,1); }
inline bool nz(V x) { return !_mm_testz_si128(x,x); }
inline bool nz(W x) { return !_mm256_testz_si256(x,x); }
inline u64 tz(u64 x) { return _tzcnt_u64(x); }
inline u64 lz(u64 x) { return _lzcnt_u64(x); }
inline u64 blsr(u64 x) { return _blsr_u64(x); }
inline u64 bzhi(u64 x,u64 n) { return _bzhi_u64(x,unsigned(n)); }
inline u64 shl(u64 x,u64 n) { return x<<(n&63); }
inline u64 shr(u64 x,u64 n) { return x>>(n&63); }
inline void prefetch(P p) { _mm_prefetch(reinterpret_cast<const char *>(std::uintptr_t(p)+1024),_MM_HINT_T0); }
inline u64 hi32(W v,char c) {
    return u64(u32(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v,_mm256_set1_epi8(c)))))<<32;
}

struct Masks { u64 c,q,n; };
inline Masks masks(P p) {
    auto z=_mm512_loadu_si512(p);
    return {_mm512_cmpeq_epi8_mask(z,_mm512_set1_epi8(',')),_mm512_cmpeq_epi8_mask(z,_mm512_set1_epi8('"')),
            _mm512_cmpeq_epi8_mask(z,_mm512_set1_epi8('\n'))};
}
inline u64 commas(P p) { return _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(p),_mm512_set1_epi8(',')); }

struct DateEnt { A<u8,12> key; u32 strikes; A<i64,2> ms; };
static_assert(sizeof(DateEnt)==32 && offsetof(DateEnt,ms)==16 && sizeof(DateEnt)*64==SCRATCH*8);

template<class B> struct Cx {
    Bytes data;
    P base,end;
    B *b;
    u8 *cache;
    Sink<B> sink;
    Cx(Bytes bytes,Sink<B> s):data(bytes),base(reinterpret_cast<P>(bytes.data())),end(base+bytes.size()),
        b(s.batch),cache(reinterpret_cast<u8 *>(s.batch->scratch.data())),sink(s) {}
    Bytes val(k_ref::Bounds r) const {return data.substr(r.first,r.second-r.first);}
};

inline u8 *date_entry(u8 *cache,P key,u64 mask) {
    return cache+_pext_u64(read<u16>(key+9),mask)*sizeof(DateEnt);
}
[[gnu::cold,gnu::noinline]] std::optional<i64> date_miss(P key,bool quoted,u8 *cache) {
    if ((quoted && key[0]!='"') || key[5]!='-' || key[8]!='-' || key[11]!='T') return {};
    for (auto i:{1,2,3,4,6,7,9,10}) if (key[i]<'0' || key[i]>'9') return {};
    const int year=key[1]*1000+key[2]*100+key[3]*10+key[4]-'0'*1111;
    const int month=10*key[6]+key[7]-'0'*11,day=10*key[9]+key[10]-'0'*11;
    if (month<1 || month>12 || day<1 || day>k_ref::days_in_month(year,month)) return {};
    const i64 ms=k_ref::days_from_civil(year,month,day)*86400000;
    u8 *raw=date_entry(cache,key,DATE_PEXT);
    auto ent=read<DateEnt>(raw);
    if (ent.key[1]==',' || ent.strikes>=3) {
        ent.key[0]='"';
        std::memcpy(ent.key.data()+1,key+1,11);
        ent.ms={ms,ms};
        ent.strikes=0;
        write(raw,ent);
    } else if (std::memcmp(ent.key.data()+1,key+1,11)!=0) {
        write(raw+offsetof(DateEnt,strikes),ent.strikes+1);
    }
    return ms;
}

inline std::pair<V,V> ids2v(V xa,const IdShape &a,V xb,const IdShape &b) {
    auto va=_mm_sub_epi8(_mm_shuffle_epi8(xa,ld(a.ctrl.data())),ld(a.subv.data()));
    auto vb=_mm_sub_epi8(_mm_shuffle_epi8(xb,ld(b.ctrl.data())),ld(b.subv.data()));
    auto d=_mm_blend_epi32(va,vb,12), check=_mm_blend_epi32(vb,va,12);
    auto e=_mm_sub_epi8(d,_mm_set1_epi8(49));
    auto nondigit=_mm_subs_epu8(d,_mm_set1_epi8(9)), nonletter=_mm_subs_epu8(e,_mm_set1_epi8(25));
    auto val=_mm_min_epu8(d,_mm_sub_epi8(d,_mm_set1_epi8(39)));
    auto zero=_mm_subs_epu8(_mm_or_si128(ld(a.lz.data()),ld(b.lz.data())),val);
    auto bad=_mm_or_si128(_mm_or_si128(check,zero),_mm_min_epu8(nondigit,nonletter));
    auto m1=_mm_maddubs_epi16(val,_mm_set1_epi16(0x0124));
    auto m2=_mm_madd_epi16(m1,_mm_set1_epi32(0x00010510));
    auto ids=_mm_add_epi64(_mm_mul_epu32(m2,_mm_set1_epi64x(1679616)),_mm_srli_epi64(m2,32));
    return {_mm_or_si128(ids,ld(b.orv.data())),bad};
}
inline std::pair<W,W> ids2j(V xa,V xb,const JidRow &r) {
    auto x=join(xa,xb);
    auto v=_mm256_sub_epi8(_mm256_shuffle_epi8(x,ld2(r.ctrl.data())),ld2(r.subv.data()));
    auto e=_mm256_sub_epi8(v,_mm256_set1_epi8(49));
    auto nondigit=_mm256_subs_epu8(v,_mm256_set1_epi8(9)),nonletter=_mm256_subs_epu8(e,_mm256_set1_epi8(25));
    auto val=_mm256_min_epu8(v,_mm256_sub_epi8(v,_mm256_set1_epi8(39)));
    auto zero=_mm256_subs_epu8(ld2(r.lz.data()),val);
    auto cls=_mm256_ternarylogic_epi64(_mm256_min_epu8(nondigit,nonletter),v,_mm256_set_epi64x(-1,0,-1,0),0xf8);
    auto m1=_mm256_maddubs_epi16(val,_mm256_set1_epi16(0x0124));
    auto m2=_mm256_madd_epi16(m1,_mm256_set1_epi32(0x00010510));
    auto ids=_mm256_add_epi64(_mm256_mul_epu32(m2,_mm256_set1_epi64x(1679616)),_mm256_srli_epi64(m2,32));
    return {_mm256_or_si256(ids,ld2(r.orv.data())),_mm256_or_si256(cls,zero)};
}
template<bool Four> inline std::pair<V,V> decimal(V x,const DecShape &s) {
    auto d=_mm_sub_epi8(_mm_shuffle_epi8(x,ld(s.ctrl.data())),ld(s.subv.data()));
    auto bad=_mm_or_si128(_mm_subs_epu8(ld(s.lz.data()),d),
                          _mm_subs_epu8(d,_mm_set_epi64x(0,Four?0x09090909:0x0909090909090909LL)));
    auto m1=_mm_maddubs_epi16(d,_mm_set_epi64x(0,Four?0x010a010a:0x010a010a010a010aLL));
    auto m2=_mm_madd_epi16(m1,_mm_set_epi64x(0,Four?0x00010064:0x0001006400010064LL));
    if constexpr (Four) return {m2,bad};
    return {_mm_madd_epi16(_mm_packus_epi32(m2,m2),_mm_set_epi64x(0,0x00012710)),bad};
}
inline std::optional<u32> price(P end,std::size_t n) {
    if (!n) return 0;
    if (n>8) return {};
    u64 y=(read<u64>(end-8)^0x3030303030303030ULL)&(~u64{0}<<(8*(8-n)));
    if ((y|(y+0x0606060606060606ULL))&0xf0f0f0f0f0f0f0f0ULL) return 0;
    y=(y*2561>>8)&0x00ff00ff00ff00ffULL;
    y=(y*6553601>>16)&0x0000ffff0000ffffULL;
    u32 v=u32(y*42949672960001ULL>>32);
    return v-1<10000000?v:0;
}

template<bool Full> inline std::optional<i64> ts_careful(P rs,std::size_t raw,u8 *cache) {
    if (raw>=64) return {};
    const auto &a=T.tsa[raw];const auto &b=T.tsb[raw];const auto &c=T.tsc[raw];
    auto kp=rs+c.koff;
    auto *ent=date_entry(cache,kp,DATE_PEXT);
    auto bad_key=_mm_and_si128(_mm_xor_si128(ld(kp),ld(ent)),ld(b.kmaskv.data()));
    auto day=read<i64>(ent+16);
    if (nz(bad_key)) {
        auto miss=date_miss(kp,b.kmaskv[0]!=0,cache);
        if (!miss) return {};
        day=*miss;
    }
    auto end=rs+raw;
    auto d=_mm_sub_epi8(_mm_shuffle_epi8(ld(end-16),ld(a.ctrl.data())),ld(a.base.data()));
    auto m1=_mm_maddubs_epi16(d,ld(a.w1.data()));
    auto m2=_mm_madd_epi16(m1,ld(b.w2.data()));
    auto m3=_mm_madd_epi16(_mm_packus_epi32(m2,m2),ld(b.w3.data()));
    auto bad=_mm_or_si128(_mm_subs_epu8(d,ld(a.lim.data())),_mm_cmpgt_epi32(m2,ld(b.limv.data())));
    if (nz(bad)) return {};
    u64 r=u64(_mm_cvtsi128_si64(m3));
    if constexpr (Full)
        return (day&i64(c.dmask))+i64(u32(r))*c.me+i64(r>>32)*c.mf[(end[c.soff]>>1)&1];
    return day+i64(u32(r))*1000;
}
template<class B> [[gnu::cold,gnu::noinline]] std::optional<std::pair<u64,u64>>
cold_ids(Cx<B> &cx,P p,P c0,P c1,char prefix) {
    auto a=k_ref::field_value(cx.data,p-cx.base,c0-cx.base);
    auto b=k_ref::field_value(cx.data,c0+1-cx.base,c1-cx.base);
    if (!a || a->first==a->second || !b) return {};
    return std::pair{k_ref::parse_id(cx.data,*a,prefix),k_ref::parse_id(cx.data,*b,'u')};
}
template<class B,class F> [[gnu::cold,gnu::noinline]] auto
cold_field(Cx<B> &cx,P first,P last,F parse)->std::optional<decltype(parse(Bytes{}))> {
    auto v=k_ref::field_value(cx.data,first-cx.base,last-cx.base);
    if (!v) return {};
    return parse(cx.val(*v));
}
template<class B> [[gnu::cold,gnu::noinline]] bool fix_time(Cx<B> &cx,P first,P end,i64 &out) {
    if (first>end || std::memchr(first,'\n',end-first)) return false;
    auto v=ts_careful<true>(first,end-first,cx.cache);
    if (!v) v=cold_field(cx,first,end,k_ref::parse_ts);
    if (!v) return false;
    out=*v;
    return true;
}

template<class B> [[gnu::cold,gnu::noinline]] P scan(Cx<B> &cx,P p) {
    auto pos=std::size_t(p-cx.base);
    auto end=cx.data.find('\n',pos);
    if (end==Bytes::npos) end=cx.data.size();
    if constexpr (std::is_same_v<B,EvBatch>) {
        auto f=k_ref::split_line<7>(cx.data,pos,end);
        if (f && (*f)[0].first!=(*f)[0].second) {
            u8 kind=k_ref::parse_kind(cx.val((*f)[4]));
            event(cx.sink,{k_ref::parse_id(cx.data,(*f)[0],'e'),k_ref::parse_id(cx.data,(*f)[1],'u'),
                  k_ref::parse_ts(cx.val((*f)[2])),k_ref::parse_f1(cx.val((*f)[3])),kind,
                  k_ref::parse_product(cx.val((*f)[5])),kind==KIND_PURCHASE?k_ref::parse_price(cx.val((*f)[6])):0});
        }
    } else {
        auto f=k_ref::split_line<4>(cx.data,pos,end);
        if (f && (*f)[0].first!=(*f)[0].second)
            click(cx.sink,{k_ref::parse_id(cx.data,(*f)[0],'k'),k_ref::parse_id(cx.data,(*f)[1],'u'),
                  k_ref::parse_campaign(cx.val((*f)[2])),k_ref::parse_ts(cx.val((*f)[3]))});
    }
    return cx.base+end+(end<cx.data.size());
}

[[gnu::cold,gnu::noinline]] P ev_cold(Cx<EvBatch> &cx,P p,std::size_t &w) {
    cx.b->n=w;
    auto slow=[&]{auto next=scan(cx,p); w=cx.b->n; return next;};
    auto h=masks(p),tail=masks(p+40);
    auto c0=tz(h.c), c1=tz(blsr(h.c)),c2=tz(blsr(blsr(h.c)));
    auto m=tz(tail.n),tb=bzhi(tail.c,m);
    if (m>=64 || (h.n<<24) || std::popcount(h.c<<24)+std::popcount(tb)!=6 ||
        std::popcount(tb)<3 || c2>62) return slow();
    unsigned c5=63^lz(tb), c4=63^lz(bzhi(tb,c5)), c3=63^lz(bzhi(tb,c4));
    if (c2>=40+c3) return slow();
    auto length=m-c5-1;
    auto pq=bzhi(shr(tail.q,c5+1),length),qb=pq&1;
    if (pq!=qb+shl(qb,length-1)) return slow();

    auto [id,id_bad]=ids2v(ld(p+c0-16),T.eid[2*c0+(h.q&1)],
                           ld(p+c1-16),T.uid[2*(c1-c0-1)+(shr(h.q,c0+1)&1)]);
    u64 eid=_mm_cvtsi128_si64(id),uid=_mm_extract_epi64(id,1);
    if (nz(id_bad)) {
        auto differing=cold_ids(cx,p,p+c0,p+c1,'e');
        if (!differing) return slow();
        eid=differing->first;uid=differing->second;
    }
    auto rs2=p+c1+1, rs3=p+c2+1, rs4=p+40+c3+1, pb=p+40;
    auto ts=ts_careful<true>(rs2,c2-c1-1,cx.cache);
    if (!ts) ts=cold_field(cx,rs2,p+c2,k_ref::parse_ts);
    if (!ts) return slow();
    auto raw3=std::size_t((pb+c3)-rs3);
    auto row3=20+2*(shr(h.q,c2+1)&1);
    auto ing=raw3==row3?ts_careful<false>(rs3,row3,cx.cache):std::nullopt;
    if (!ing) ing=cold_field(cx,rs3,pb+c3,k_ref::parse_f1);
    if (!ing) return slow();
    const auto &kr=T.kind[c4-c3-1];
    auto which=rs4[3]&1;
    u8 kind=u8(kr.k[which]);
    if (nz(_mm_and_si128(_mm_xor_si128(ld(rs4),ld(kr.cand[which].data())),ld(kr.maskv.data())))) {
        auto cold=cold_field(cx,rs4,pb+c4,k_ref::parse_kind);
        if (!cold) return slow();
        kind=*cold;
    }
    auto [product_vec,bad_prod]=decimal<false>(ld(pb+c5-16),T.prod[2*(c5-c4-1)+(shr(tail.q,c4+1)&1)]);
    u32 product=u32(_mm_cvtsi128_si32(product_vec));
    if (nz(bad_prod)) {
        auto cold=cold_field(cx,pb+c4+1,pb+c5,k_ref::parse_product);
        if (!cold) return slow();
        product=*cold;
    }
    u32 value=0;
    if (kind==KIND_PURCHASE) {
        auto v=price(pb+m-qb,length-2*qb);
        if (!v) return slow();
        value=*v;
    }
    event(cx.sink,{eid,uid,*ts,*ing,kind,product,value});
    w=cx.b->n;
    return pb+m+1;
}
[[gnu::cold,gnu::noinline]] P ck_cold(Cx<CkBatch> &cx,P p,std::size_t &w) {
    cx.b->n=w;
    auto slow=[&]{auto next=scan(cx,p);w=cx.b->n;return next;};
    auto k=masks(p);auto c0=tz(k.c),c1=tz(blsr(k.c));
    auto rem=blsr(blsr(k.c)),c2=tz(rem),n=tz(k.n);
    if (n>63 || c2>=n || bzhi(blsr(rem),n)) return slow();
    auto [id,id_bad]=ids2v(ld(p+c0-16),T.kid[2*c0+(k.q&1)],
                           ld(p+c1-16),T.uid[2*(c1-c0-1)+(shr(k.q,c0+1)&1)]);
    u64 cid=_mm_cvtsi128_si64(id),uid=_mm_extract_epi64(id,1);
    if (nz(id_bad)) {
        auto differing=cold_ids(cx,p,p+c0,p+c1,'k');
        if (!differing) return slow();
        cid=differing->first;uid=differing->second;
    }
    auto [cv,bad_c]=decimal<false>(ld(p+c2-16),T.camp[2*(c2-c1-1)+(shr(k.q,c1+1)&1)]);
    u32 camp=u32(_mm_cvtsi128_si32(cv));
    if (nz(bad_c)) {
        auto d=cold_field(cx,p+c1+1,p+c2,k_ref::parse_campaign);
        if (!d) return slow();
        camp=*d;
    }
    auto ts=ts_careful<true>(p+c2+1,n-c2-1,cx.cache);
    if (!ts) ts=cold_field(cx,p+c2+1,p+n,k_ref::parse_ts);
    if (!ts) return slow();
    click(cx.sink,{cid,uid,camp,*ts});
    w=cx.b->n;
    return p+n+1;
}

struct EvMasks {
    u64 head,tail,newline;
    P next(P p) const { return p+33+tz(newline); }
    EvMasks here() const {
        auto h=head,t=tail,n=newline;
        asm("" : "+k"(h),"+k"(t),"+k"(n));
        return {h,t,n};
    }
};
inline EvMasks ev_masks(P p) {
    auto head=_mm512_loadu_si512(p), tail=_mm512_loadu_si512(p+32);
    auto comma=_mm512_set1_epi8(',');
    return {_mm512_cmpeq_epi8_mask(head,comma),_mm512_cmpeq_epi8_mask(tail,comma),
            _mm512_cmpeq_epi8_mask(tail,_mm512_set1_epi8('\n'))};
}
struct EvSt {const TjRow *tj; W x,days,bad2,bad;};
inline bool ev_s(Cx<EvBatch> &cx,P p,EvMasks k,std::size_t w,EvSt &st) {
    auto m=tz(k.newline),c0=tz(k.head),c1=tz(blsr(k.head)),c2=tz(blsr(blsr(k.head)));
    auto tb=bzhi(k.tail,m), z=lz(tb);
    auto r6=m+z-64;
    auto bits=shl(tb,z)<<1,r5=lz(bits);
    auto r4=lz(shl(bits,r5)<<1),i4=(z+r5+r4+1)^63;
    auto u3=i4+10-c2;
    if (m>63 || c2>62 || (u3&~u64{2})) return false;
    // A valid ingest position implies three tail separators and a table-sized suffix.
    const auto &tr=T.tail[r5*TAIL_K+r6];
    auto pb=p+32;
    auto quotes=u32(hi32(lu2(pb+m-32),'"')>>32)&tr.m;
    auto qi=_pext_u32(quotes,tr.sel);
    if (quotes!=tr.pat[qi]) return false;
    auto rs4=pb+i4;
    const auto &kr=T.kind2[2*r4+(rs4[3]&1)];
    auto bad_kind=_mm_and_si128(_mm_xor_si128(ld(rs4),ld(kr.cand.data())),ld(kr.maskv.data()));
    cx.b->pp[w]=tr.pp[qi]|kr.pp;

    const auto &j=T.jid[c0][c1-c0];
    auto [ids,id_bad]=ids2j(ld(p+c0-16),ld(p+c1-16),j);
    cx.b->eid[w]=_mm_cvtsi128_si64(_mm256_castsi256_si128(ids));
    cx.b->uid[w]=_mm_cvtsi128_si64(_mm256_extracti128_si256(ids,1));
    auto q3=u3>>1;
    auto &tj=T.tj[2*(c2-c1-1)+q3];auto &f=tj.f;
    auto kp2=p+c1+f.koff1,kp3=p+c2+q3;
    auto date2=ld2(date_entry(cx.cache,kp2,f.pmask)),date3=ld2(date_entry(cx.cache,kp3,f.pmask3));
    auto k2=_mm_and_si128(_mm_xor_si128(ld(kp2),_mm256_castsi256_si128(date2)),ld(tj.d.kmask2.data()));
    auto k3=_mm_and_si128(_mm_xor_si128(ld(kp3),_mm256_castsi256_si128(date3)),ld(tj.d.kmask3.data()));
    auto bad=_mm256_or_si256(id_bad,_mm256_zextsi128_si256(_mm_or_si128(k3,bad_kind)));
    st={&tj,join(ld(p+c2-16),ld(rs4-17)),_mm256_permute2x128_si256(date2,date3,0x31),
        _mm256_or_si256(bad,_mm256_zextsi128_si256(k2)),bad};
    return true;
}
[[gnu::cold,gnu::noinline]] bool ev_purchase(P np,u64 &out) {
    auto end=np-1,pt=end-32;
    auto tw=lu2(pt);auto q=hi32(tw,'"')>>32,tb=hi32(tw,',');
    auto r6=lz(tb),r5=lz(shl(tb,r6)<<1);
    auto i6=32-r6,i5=i6-r5-1;
    auto v=price(end-(shr(q,i6)&1),r6-2*(shr(q,i6)&1));
    if (!v || r5>63) return false;
    auto [prod,bad]=decimal<false>(ld(pt+i6-17),T.prod[2*r5+(shr(q,i5)&1)]);
    if (nz(bad)) return false;
    out=u32(_mm_cvtsi128_si32(prod))|(u64(*v)<<32)|(u64{2}<<56)|(*v?0:PP_INVALID);
    return true;
}

struct Step {P next;bool boundary;};
inline Step ev_d(Cx<EvBatch> &cx,P p,const EvSt &st,P next,std::size_t &w) {
    auto &j=*st.tj;
    auto d=_mm256_sub_epi8(_mm256_shuffle_epi8(st.x,ld2(j.a.ctrl.data())),ld2(j.a.base.data()));
    auto m1=_mm256_maddubs_epi16(d,ld2(j.b.w1.data()));
    auto m2=_mm256_madd_epi16(m1,ld2(j.c.w2.data()));
    auto m3=_mm256_madd_epi16(_mm256_packus_epi32(m2,m2),ld2(j.c.w3.data()));
    auto ba=_mm256_subs_epu8(d,ld2(j.b.lim.data())),bb=_mm256_cmpgt_epi32(m2,ld2(j.d.limv.data()));
    auto t=_mm256_mul_epu32(m3,ld2(j.e.kme.data())),f=_mm256_mul_epi32(_mm256_srli_epi64(m3,32),ld2(j.e.kmf.data()));
    auto ksub=_mm256_testn_epi64_mask(d,ld2(j.f.ksel.data()));
    auto r=_mm256_add_epi64(_mm256_mask_sub_epi64(_mm256_add_epi64(t,f),ksub,t,f),st.days);
    cx.b->ts[w]=_mm_extract_epi64(_mm256_castsi256_si128(r),1);
    auto dst=reinterpret_cast<u8 *>(cx.b)+offsetof(EvBatch,ing)+8*w;
    _mm_storeu_si128(reinterpret_cast<V *>(dst),_mm256_extracti128_si256(r,1));
    if (nz(_mm256_ternarylogic_epi64(ba,bb,st.bad2,0xfe))) {
        auto other=_mm256_or_si256(st.bad,_mm256_and_si256(_mm256_or_si256(ba,bb),_mm256_set_epi64x(-1,-1,0,0)));
        if (nz(other)) return {ev_cold(cx,p,w),true};
        auto c1=blsr(commas(p));
        if (!fix_time(cx,p+tz(c1)+1,p+tz(blsr(c1)),cx.b->ts[w])) return {ev_cold(cx,p,w),true};
    }
    if ((cx.b->pp[w]&(u64{2}<<56)) && !ev_purchase(next,cx.b->pp[w])) return {ev_cold(cx,p,w),true};
    if (++w==NB) {
        cx.b->n=NB;
        cx.sink.ev_flush();
        w=0;
        return {next,true};
    }
    return {next,false};
}
inline P ev_fast(Cx<EvBatch> &cx,P p,EvMasks k,std::size_t &w) {
    EvSt st;
    if (ev_s(cx,p,k,w,st)) return ev_d(cx,p,st,k.next(p),w).next;
    return ev_cold(cx,p,w);
}

struct CkMasks { u64 c,n; };
inline CkMasks ck_masks(P p) {
    auto v=_mm512_loadu_si512(p);
    return {_mm512_cmpeq_epi8_mask(v,_mm512_set1_epi8(',')),_mm512_cmpeq_epi8_mask(v,_mm512_set1_epi8('\n'))};
}
struct ClSt { const TjRow *j; V x,days,bad_k,bad; };
inline bool cl_s(Cx<CkBatch> &cx,P p,CkMasks k,std::size_t w,ClSt &st) {
    auto n=tz(k.n), c0=tz(k.c), c1=tz(blsr(k.c));
    auto tail=blsr(blsr(k.c)),c2=tz(tail);
    if (n>63 || c2>=n || bzhi(blsr(tail),n)) return false;
    auto e2=p+c2;
    auto [cv,bad_c]=decimal<true>(ld(e2-16),T.camp4[2*(c2-c1-1)+(p[c1+1]&1)]);
    cx.b->camp[w]=_mm_cvtsi128_si64(cv);
    auto [id,id_bad]=ids2v(ld(p+c0-16),T.kid1[c0],ld(p+c1-16),T.uid1[c1-c0-1]);
    cx.b->cid[w]=_mm_cvtsi128_si64(id);
    cx.b->uid[w]=_mm_extract_epi64(id,1);
    auto &j=T.tj[2*(n-c2-1)];
    auto kp=e2+j.f.koff1;
    auto ent=date_entry(cx.cache,kp,j.f.pmask);
    auto bad_k=_mm_and_si128(_mm_xor_si128(ld(kp),ld(ent)),ld(j.d.kmask2.data()));
    st={&j,ld(p+n-16),ld(ent+16),bad_k,_mm_or_si128(id_bad,bad_c)};
    return true;
}
inline P cl_d(Cx<CkBatch> &cx,P p,const ClSt &st,P next,std::size_t &w) {
    auto &j=*st.j;
    auto d=_mm_sub_epi8(_mm_shuffle_epi8(st.x,ld(j.a.ctrl.data())),ld(j.a.base.data()));
    auto m1=_mm_maddubs_epi16(d,ld(j.b.w1.data()));
    auto m2=_mm_madd_epi16(m1,ld(j.c.w2.data()));
    auto m3=_mm_madd_epi16(_mm_packus_epi32(m2,m2),ld(j.c.w3.data()));
    auto bad_t=_mm_or_si128(_mm_subs_epu8(d,ld(j.b.lim.data())),_mm_cmpgt_epi32(m2,ld(j.d.limv.data())));
    auto t=_mm_mul_epu32(m3,ld(j.e.kme.data())),f=_mm_mul_epi32(_mm_srli_epi64(m3,32),ld(j.e.kmf.data()));
    auto mask=_mm_testn_epi64_mask(d,ld(j.f.ksel.data()));
    auto r=_mm_add_epi64(_mm_mask_sub_epi64(_mm_add_epi64(t,f),mask,t,f),st.days);
    cx.b->ts[w]=_mm_extract_epi64(r,1);
    if (nz(_mm_or_si128(st.bad,_mm_or_si128(st.bad_k,bad_t)))) {
        if (nz(st.bad)) return ck_cold(cx,p,w);
        auto c2=tz(blsr(blsr(commas(p))));
        if (!fix_time(cx,p+c2+1,next-1,cx.b->ts[w])) return ck_cold(cx,p,w);
    }
    if (++w==NB) {
        cx.b->n=NB;
        cx.sink.ck_flush();
        w=0;
    }
    return next;
}
inline P cl_fast(Cx<CkBatch> &cx,P p,CkMasks k,std::size_t &w) {
    ClSt st;
    if (cl_s(cx,p,k,w,st)) return cl_d(cx,p,st,p+tz(k.n)+1,w);
    return ck_cold(cx,p,w);
}
#if defined(__ELF__)
struct ClickExit {u64 code; P line; i64 *write;};
static_assert(sizeof(ClickExit)==24);
extern "C" void shopping_kernel_click_loop(P,i64 *,void *,ClickExit *);
#endif
}
void events_x86(Bytes buf,Sink<EvBatch> sink,bool chain) {
    if (buf.empty()) return;
    Cx<EvBatch> cx(buf,sink);
    auto p=cx.base;
    // Vectorized ID decoding looks back sixteen bytes from the comma. Chunks
    // can start at a guard page, or at the first byte of a separately owned input.
    while (p<cx.end && p-cx.base<16) p=scan(cx,p);
    auto w=cx.b->n;
    constexpr std::size_t reach=3*(40+64)+2,far=(NB+1)*(40+64+1);
    if (!chain && buf.size()>=reach+far) {
        auto limit=cx.end-reach-far;
        while (p<=limit) {
            auto k=ev_masks(p);auto p1=k.next(p);
            EvSt st;
            if (!ev_s(cx,p,k,w,st)) {p=ev_cold(cx,p,w);continue;}
            auto k1=ev_masks(p1).here();
            for (;;) {
                auto p2=k1.next(p1);
                auto k2=ev_masks(p2).here();
                prefetch(p);
                auto done=ev_d(cx,p,st,p1,w);
                p=done.next;
                if (p!=p1 || (done.boundary && p>limit)) break;
                if (!ev_s(cx,p,k1,w,st)) {p=ev_cold(cx,p,w);break;}
                p1=p2;
                k1=k2;
            }
        }
    }
    constexpr std::size_t lookahead=2*(40+64)+1;
    while (cx.end-p>=std::ptrdiff_t(lookahead)) {
        auto k=ev_masks(p);
        prefetch(p);
        p=ev_fast(cx,p,k,w);
    }
    // The head mask needs 96 bytes; careful repair also reads 64 bytes
    // at offset 40 and its final field can issue a 16-byte date-key load.
    while (cx.end-p>=128) p=ev_fast(cx,p,ev_masks(p),w);
    cx.b->n=w;
    while (p<cx.end) p=scan(cx,p);
}
void clicks_x86(Bytes buf,Sink<CkBatch> sink,bool chain) {
    if (buf.empty()) return;
    Cx<CkBatch> cx(buf,sink);
    auto p=cx.base;
    while (p<cx.end && p-cx.base<16) p=scan(cx,p);
    auto w=cx.b->n;
#if defined(__ELF__)
    constexpr std::size_t reach=(NB+3)*65+64;
    if (!chain && buf.size()>=reach) {
        auto limit=cx.end-reach;
        while (p<=limit) {
            ClickExit result;
            shopping_kernel_click_loop(p,cx.b->ts.data()+w,cx.b->camp.data(),&result);
            p=result.line;
            w=std::size_t(result.write-cx.b->ts.data());
            if (result.code==2) {
                cx.b->n=NB;
                cx.sink.ck_flush();
                w=0;
            } else p=cl_fast(cx,p,ck_masks(p),w);
        }
    }
#endif
    if (chain) {
        while (cx.end-p>=129) {
            auto k=ck_masks(p);
            prefetch(p);
            p=cl_fast(cx,p,k,w);
        }
    }
    while (cx.end-p>=80) p=cl_fast(cx,p,ck_masks(p),w);
    cx.b->n=w;
    while (p<cx.end) p=scan(cx,p);
}
}

#if defined(__clang__)
#pragma clang attribute pop
#else
#pragma GCC pop_options
#endif
#endif
