#pragma once
#include "iface.hpp"
#include <array>
#include <string_view>
#include <utility>

namespace pipeline::k_free::detail {
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
template<typename T, std::size_t N> using A = std::array<T,N>;

struct alignas(64) IdShape {
    A<u8,16> ctrl{}, subv{}, lz{};
    A<u64,2> orv{};
};
struct alignas(64) JidRow {
    A<u8,32> ctrl{}, subv{}, lz{};
    A<u64,4> orv{};
};
struct alignas(64) DecShape {
    A<u8,16> ctrl{}, subv{}, lz{}, pad{};
};
constexpr A<IdShape,128> id_shapes(u8 prefix, unsigned dig, unsigned chk, bool allow_empty) {
    A<IdShape,128> rows{};
    for (unsigned raw=0;raw<64;++raw) for (unsigned q=0;q<2;++q) {
        auto &s=rows[raw*2+q];
        s.ctrl.fill(0x80);
        const unsigned plus=1+2*q;
        if (raw>=plus+1 && raw<=plus+8) {
            const auto nd=raw-plus;
            for (unsigned j=8-nd;j<8;++j) {
                s.ctrl[dig+j]=u8(8-q+j);
                s.subv[dig+j]='0';
            }
            s.ctrl[chk]=u8(16-raw+q);
            s.subv[chk]=prefix;
            if (q) { s.ctrl[chk+1]=15; s.subv[chk+1]='"'; }
            if (nd>1) s.lz[dig+8-nd]=1;
        } else if (allow_empty && raw==2*q) {
            if (q) { s.ctrl[chk+1]=15; s.subv[chk+1]='"'; }
            s.orv[1]=UINT64_MAX;
        } else s.subv[chk]=1;
    }
    return rows;
}
constexpr A<IdShape,64> id_shapes_raw(u8 prefix, unsigned dig, unsigned chk, bool allow_empty) {
    auto both=id_shapes(prefix,dig,chk,allow_empty);
    A<IdShape,64> rows{};
    for (unsigned raw=0;raw<64;++raw) {
        const unsigned q=(raw==10 || raw==11 || (raw==2&&allow_empty));
        rows[raw]=both[raw*2+q];
        if (q) { rows[raw].ctrl[chk+2]=u8(16-raw); rows[raw].subv[chk+2]='"'; }
    }
    return rows;
}

inline constexpr auto event_raw=id_shapes_raw('e',0,8,false);
inline constexpr auto user_joint=id_shapes_raw('u',0,8,true);
// Evaluate the 64 independent rows separately to fit default constexpr limits.
template<unsigned B> inline constexpr auto joint_block = []{
    A<JidRow,64> rows{};
    auto invalid=user_joint[0]; invalid.subv[8]=1; invalid.orv={};
    for (unsigned d=0;d<64;++d) {
        const auto &a=event_raw[B], &b=d==0?invalid:user_joint[d-1];
        auto &r=rows[d];
        for (unsigned j=0;j<16;++j) {
            r.ctrl[j]=a.ctrl[j]; r.ctrl[16+j]=b.ctrl[j];
            r.subv[j]=a.subv[j]; r.subv[16+j]=b.subv[j];
            r.lz[j]=a.lz[j]; r.lz[16+j]=b.lz[j];
        }
        r.orv={a.orv[1],0,b.orv[1],0};
    }
    return rows;
}();
template<std::size_t... B> constexpr auto jid_rows(std::index_sequence<B...>) {
    return A<A<JidRow,64>,64>{joint_block<B>...};
}

constexpr A<DecShape,128> dec_shapes(std::string_view prefix, bool camp4=false) {
    A<DecShape,128> rows{};
    const unsigned pl=unsigned(prefix.size()), digits=camp4?4:8;
    for (unsigned raw=0;raw<64;++raw) for (unsigned q=0;q<2;++q) {
        auto &s=rows[raw*2+(camp4?1-q:q)]; s.ctrl.fill(0x80);
        unsigned plus=pl+2*q;
        if (raw<plus+1 || raw>plus+digits) {s.subv[8]=1; continue;}
        unsigned nd=raw-plus;
        for (unsigned j=digits-nd;j<digits;++j) {
            s.ctrl[j]=u8(16-digits-q+j);
            s.subv[j]='0';
        }
        for (unsigned j=0;j<pl;++j) {
            s.ctrl[8+j]=u8(16-raw+q+j);
            s.subv[8+j]=u8(prefix[j]);
        }
        if (q) {
            s.ctrl[8+pl]=15; s.subv[8+pl]='"';
            if (camp4) {s.ctrl[9+pl]=u8(16-raw); s.subv[9+pl]='"';}
        }
        if (nd>1) s.lz[digits-nd]=1;
    }
    return rows;
}
struct TsA { A<u8,16> ctrl{},base{},lim{}; A<i8,16> w1{}; };
struct TsB { A<i16,8> w2{},w3{}; A<u8,16> kmaskv{}; A<i32,4> limv{}; };
struct alignas(64) TsC { i8 koff{},soff{}; i16 dmask{}; i32 me{}; A<i32,2> mf{}; };
struct TsRow { TsA a; TsB b; TsC c; };
inline constexpr auto ts_invalid = []{
    TsRow row{};
    for (unsigned i=0;i<16;++i) row.a.ctrl[i]=u8(i);
    row.a.lim.fill(255);
    row.b.limv={-1,INT32_MAX,INT32_MAX,INT32_MAX};
    row.c.koff=-1; row.c.soff=-1;
    return row;
}();
constexpr TsRow ts_f12(bool f2, bool q) {
    TsRow r=ts_invalid;
    auto &[a,b,c]=r;
    unsigned raw=20+5*f2+2*q, off=11+q;
    for (unsigned i=0;i<16;++i) a.ctrl[i]=off+i<raw?u8(off+i+16-raw):0x80;
    constexpr A<u8,8> limits={2,9,0,5,9,0,5,9};
    constexpr A<i8,8> mul={10,1,0,10,1,0,10,1};
    for (unsigned i=0;i<8;++i) {a.base[i]="00:00:00"[i];a.lim[i]=limits[i];a.w1[i]=mul[i];}
    if (f2) {
        constexpr A<u8,6> lim2={2,2,9,0,5,9};
        constexpr A<i8,6> mul2={0,10,1,0,10,1};
        for (unsigned i=0;i<6;++i) {a.base[8+i]="+00:00"[i];a.lim[8+i]=lim2[i];a.w1[8+i]=mul2[i];}
        c.mf={60000,-60000}; c.soff=i8(int(off+8)-int(raw));
    } else {a.base[8]='Z';a.lim[8]=0;}
    if (q) {unsigned next=f2?14:9;a.base[next]='"';a.lim[next]=0;}
    b.w2={60,1,60,1,1,1,1,0}; b.w3={60,1,60,1,0,0,0,0};
    for (unsigned i=q?0:1;i<12;++i) b.kmaskv[i]=255;
    b.limv={1430,INT32_MAX,23,INT32_MAX};
    c.koff=i8(q)-1; c.dmask=-1; c.me=1000;
    return r;
}
inline constexpr auto ts_rows = []{
    A<TsRow,64> rows{}; rows.fill(ts_invalid);
    for (unsigned n=1;n<=16;++n) {
        auto &[a,b,c]=rows[n];
        if (n==15) {
            a.base[1]=a.base[15]='"';a.lim[1]=a.lim[15]=0;
            for (unsigned i=2;i<15;++i) {
                a.base[i]='0';a.lim[i]=9;a.w1[i]=(i==14 || (i&1))?1:10;
            }
            b.w2={0,1,100,1,100,1,10,1};b.w3={10000,1,1000,1,0,0,0,0};
            c.me=10000000;
        } else {
            for (unsigned i=16-n;i<16;++i) {a.base[i]='0';a.lim[i]=9;a.w1[i]=(i&1)?1:10;}
            b.w2={100,1,100,1,100,1,100,1};b.w3={10000,1,10000,1,0,0,0,0};
            c.me=100000000;
        }
        b.limv.fill(INT32_MAX);c.mf={1,1};
    }
    for (unsigned q=0;q<2;++q) for (unsigned f2=0;f2<2;++f2)
        rows[20+5*f2+2*q]=ts_f12(f2,q);
    return rows;
}();
inline constexpr u64 DATE_PEXT=0x0f03;
struct alignas(64) TjA { A<u8,32> ctrl{},base{}; };
struct alignas(64) TjB { A<u8,32> lim{}; A<i8,32>w1{}; };
struct alignas(64) TjC { A<i16,16>w2{},w3{}; };
struct alignas(64) TjD { A<i32,8> limv{}; A<u8,16> kmask2{},kmask3{}; };
struct alignas(64) TjE { A<i64,4> kme{},kmf{}; };
struct alignas(64) TjF { A<u8,32> ksel{}; i64 koff1{}; u64 pmask{},pmask3{}; i64 pad{}; };
struct alignas(64) TjRow { TjA a; TjB b; TjC c; TjD d; TjE e; TjF f; A<u64,16> pad{}; };
inline constexpr auto tj_table=[]{
    A<TjRow,128> rows{};
    for (unsigned j=0;j<128;++j) {
        const TsRow &ev=ts_rows[j/2], &ing=ts_rows[20+2*(j%2)];
        auto &r=rows[j];
        for (unsigned lane=0;lane<2;++lane) {
            const auto &v=lane?ing:ev;
            for (unsigned i=0;i<16;++i) {
                r.a.ctrl[lane*16+i]=v.a.ctrl[i];r.a.base[lane*16+i]=v.a.base[i];
                r.b.lim[lane*16+i]=v.a.lim[i];r.b.w1[lane*16+i]=v.a.w1[i];
            }
            for (unsigned i=0;i<8;++i) r.c.w2[lane*8+i]=v.b.w2[i];
            for (unsigned i=0;i<4;++i) r.d.limv[lane*4+i]=v.b.limv[i];
        }
        for (unsigned i=0;i<4;++i) r.c.w3[4+i]=ev.b.w3[i];
        for (unsigned i=0;i<2;++i) r.c.w3[8+i]=ing.b.w3[i];
        r.d.kmask2=ev.b.kmaskv;r.d.kmask3=ing.b.kmaskv;
        bool f2=ev.c.mf[0]==60000;
        r.e.kme={0,ev.c.me,1000,0};r.e.kmf={0,f2?60000:-i64(ev.c.mf[0]),0,0};
        if (f2) r.f.ksel[8]=2;
        r.f.koff1=ev.c.koff+1; r.f.pmask=ev.c.dmask?DATE_PEXT:0; r.f.pmask3=DATE_PEXT;
    }
    return rows;
}();
struct alignas(64) KindRow { A<A<u8,16>,2> cand{}; A<u8,16> maskv{}; A<u32,2> k{}; };
struct alignas(64) KindRow2 { A<u8,16>cand{},maskv{}; u64 pp{}; A<u64,3> pad{}; };
constexpr KindRow kind_row(std::string_view first, u32 x, std::string_view second, u32 y) {
    KindRow r{};r.k={x,y};
    for (unsigned i=0;i<first.size();++i) {
        r.cand[0][i]=u8(first[i]);r.cand[1][i]=u8(second[i]);r.maskv[i]=255;
    }
    return r;
}
inline constexpr auto kind_rows=[]{
    A<KindRow,64> rows{};
    for (auto &r:rows) {
        r.cand[0].fill('\n');r.cand[1].fill('\n');r.maskv.fill(255);r.k={3,3};
    }
    rows[4]=kind_row("cart",1,"view",0);rows[6]=kind_row("\"cart\"",1,"\"view\"",0);
    rows[8]=kind_row("purchase",2,"purchase",2);rows[10]=kind_row("\"purchase\"",2,"\"purchase\"",2);
    return rows;
}();
inline constexpr auto kind_rows2=[]{
    A<KindRow2,128> rows{};
    for (unsigned i=0;i<rows.size();++i) {
        auto &r=rows[i];auto &k=kind_rows[i/2];
        r.cand=k.cand[i%2];r.maskv=k.maskv;r.pp=u64(k.k[i%2])<<56;
    }
    return rows;
}();
struct alignas(64) TailRow { u32 sel{},m{}; A<u32,4> pat{}; A<u64,4> pp{}; u64 pad{}; };
inline constexpr unsigned TAIL_K=72, TAIL_ROWS=63*TAIL_K+64;
inline constexpr auto tail_rows=[]{
    A<TailRow,TAIL_ROWS> rows{};
    TailRow unused{};unused.pat.fill(UINT32_MAX);unused.pp.fill(PP_SOME);
    rows.fill(unused);
    for (unsigned r5=0;r5<31;++r5) for (unsigned r6=0;r5+1+r6<=31;++r6) {
        const unsigned n=r5+1+r6, sh=32-n;
        const u32 p5=r5>=2?(1u|(1u<<(r5-1))):UINT32_MAX;
        const u32 p6=r6>=2?(1u|(1u<<(r6-1)))<<(r5+1):UINT32_MAX;
        auto up=[sh](u32 p){return p==UINT32_MAX?p:p<<sh;};
        const u64 e0=r5==0?PP_NONE:PP_SOME, e1=r5==2?PP_NONE:PP_SOME;
        rows[r5*TAIL_K+r6]={(1u|(1u<<(r5+1)))<<sh, ((1u<<n)-1u)<<sh,
                            {0,up(p5),up(p6),up(p5|p6)}, {e0,e1,e0,e1},0};
    }
    return rows;
}();

struct alignas(64) Tables {
    A<TsA,64> tsa; A<TsB,64> tsb; A<TsC,64> tsc;
    A<TjRow,128> tj;
    A<KindRow,64> kind; A<KindRow2,128> kind2;
    A<TailRow,TAIL_ROWS> tail;
    A<IdShape,128> eid,kid,uid;
    A<IdShape,64> eid1;
    A<A<JidRow,64>,64> jid;
    A<IdShape,64> kid1,uid1;
    A<DecShape,128> prod,camp,camp4;
};
template<typename V, typename Member>
constexpr auto separate(const V &rows,Member m) {
    using Element=std::remove_cvref_t<decltype(rows[0].*m)>;
    A<Element,std::tuple_size_v<V>> out{};
    for (unsigned i=0;i<out.size();++i) out[i]=rows[i].*m;
    return out;
}
#if defined(__ELF__) && defined(__x86_64__)
#define K_FREE_ASM_NAME(name) __asm__(name) __attribute__((used,visibility("hidden")))
#else
#define K_FREE_ASM_NAME(name)
#endif
inline constexpr Tables T K_FREE_ASM_NAME("shopping_kernel_tables"){
    separate(ts_rows,&TsRow::a),separate(ts_rows,&TsRow::b),separate(ts_rows,&TsRow::c),
    tj_table,kind_rows,kind_rows2,tail_rows,
    id_shapes('e',0,8,false),id_shapes('k',0,8,false),id_shapes('u',8,0,true),
    event_raw,jid_rows(std::make_index_sequence<64>{}),
    id_shapes_raw('k',0,8,false),id_shapes_raw('u',8,0,true),
    dec_shapes("p"),dec_shapes("c_"),dec_shapes("c_",true)
};

struct alignas(64) CtsRow {
    A<u8,16>ctrl{},base{},lim{}; A<i8,16>w1{}; A<i16,8>w2{},w3{};
    A<i32,4>limv{}; A<u8,16>kmask2{}; A<i64,2>kme{},kmf{}; A<u8,16>ksel{};
    i64 koff1{}; u64 pmask{}; A<u64,8> pad{};
};
inline constexpr auto CTS K_FREE_ASM_NAME("shopping_kernel_cts")=[]{
    A<CtsRow,65> rows{};
    for (unsigned n=0;n<65;++n) {
        auto &r=rows[n];const auto &j=tj_table[2*(n?n-1:63)];
        for (unsigned i=0;i<16;++i) {
            r.ctrl[i]=j.a.ctrl[i];r.base[i]=j.a.base[i];r.lim[i]=j.b.lim[i];
            r.w1[i]=j.b.w1[i];r.kmask2[i]=j.d.kmask2[i];r.ksel[i]=j.f.ksel[i];
        }
        for (unsigned i=0;i<8;++i) {r.w2[i]=j.c.w2[i];r.w3[i]=j.c.w3[i];}
        for (unsigned i=0;i<4;++i) r.limv[i]=j.d.limv[i];
        r.kme={j.e.kme[0],j.e.kme[1]}; r.kmf={j.e.kmf[0],j.e.kmf[1]};
        r.koff1=j.f.koff1;r.pmask=j.f.pmask;
    }
    for (int q=0;q<=1;++q) {
        int raw=25+2*q, off=16-raw+11+q;
        auto &r=rows[raw+1];
        r.ctrl.fill(0x80);r.base.fill(0);r.lim.fill(255);r.w1.fill(0);
        constexpr A<u8,8> lim={2,9,0,5,9,0,5,9};constexpr A<i8,8> mul={10,1,0,10,1,0,10,1};
        for (int i=0;i<8;++i) {
            r.ctrl[i]=u8(off+i);r.base[i]="00:00:00"[i];r.lim[i]=lim[i];r.w1[i]=mul[i];
        }
        constexpr int order[5]={9,10,12,13,11}, max[5]={2,9,5,9,0}, weight[5]={10,1,10,1,0};
        for (int i=0;i<5;++i) {
            r.ctrl[8+i]=u8(off+order[i]);r.base[8+i]="0000:"[i];r.lim[8+i]=u8(max[i]);r.w1[8+i]=i8(weight[i]);
        }
        if (q) {r.ctrl[13]=15;r.base[13]='"';r.lim[13]=0;}
        r.ctrl[14]=r.ctrl[15]=u8(off+8);r.base[14]='+';r.lim[14]=2;r.w1[14]=-1;
        r.base[15]=',';r.lim[15]=255;r.w1[15]=-1;
        r.w2={60,1,60,1,60,1,0,1};r.limv={1430,INT32_MAX,1439,-2};
        r.w3={0,0,0,0,60,1,1,0};r.ksel.fill(0);r.ksel[14]=2;
    }
    return rows;
}();
inline constexpr auto CAMP5R K_FREE_ASM_NAME("shopping_kernel_camp")=[]{
    A<DecShape,128> shapes{};
    for (auto &s:shapes) {s.ctrl.fill(0x80);s.subv[8]=1;}
    for (unsigned q=0;q<=1;++q) for (unsigned nd=1;nd<=4;++nd) {
        unsigned raw=2+nd+2*q, fs=15-raw, ds=15-q-nd;
        DecShape s{};s.ctrl.fill(0x80);
        for (unsigned j=0;j<nd;++j) {s.ctrl[4-nd+j]=u8(ds+j);s.subv[4-nd+j]='0';}
        s.ctrl[8]=u8(fs+q);s.subv[8]='c';s.ctrl[9]=u8(fs+q+1);s.subv[9]='_';
        s.ctrl[10]=15;s.subv[10]=',';
        if (q) {s.ctrl[11]=14;s.subv[11]='"';s.ctrl[12]=u8(fs);s.subv[12]='"';}
        if (nd>1) s.lz[4-nd]=1;
        shapes[(raw+1)*2+(1-q)]=s;
    }
    A<DecShape,66> rows{};
    for (unsigned r=0;r<=32;++r) {
        rows[64-2*r]=shapes[2*r];rows[65-2*r]=shapes[2*r+1];
    }
    return rows;
}();
#undef K_FREE_ASM_NAME
static_assert(offsetof(Tables,kid1)==937472 && offsetof(Tables,uid1)-64==941504);
static_assert(offsetof(CkBatch,camp)==512 && offsetof(CkBatch,cid)==1024 && offsetof(CkBatch,uid)==1536);
static_assert(sizeof(JidRow)==128 && sizeof(IdShape)==64 && sizeof(DecShape)==64);
static_assert(sizeof(TsA)==64 && sizeof(TsB)==64 && sizeof(TsC)==64 && sizeof(TjRow)==512);
static_assert(sizeof(TailRow)==64 && sizeof(KindRow)==64 && sizeof(CtsRow)==256);
static_assert(offsetof(CtsRow,base)==16 && offsetof(CtsRow,lim)==32 && offsetof(CtsRow,w1)==48);
static_assert(offsetof(CtsRow,w2)==64 && offsetof(CtsRow,w3)==80 && offsetof(CtsRow,limv)==96);
static_assert(offsetof(CtsRow,kmask2)==112 && offsetof(CtsRow,kme)==128 && offsetof(CtsRow,kmf)==144);
static_assert(offsetof(CtsRow,ksel)==160 && offsetof(CtsRow,koff1)==176 && offsetof(CtsRow,pmask)==184);
}
