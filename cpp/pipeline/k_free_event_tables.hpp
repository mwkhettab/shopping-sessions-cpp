#pragma once
#include "k_free_tables.hpp"
#include <limits>

namespace pipeline::k_free::detail {
#if defined(__ELF__) && defined(__x86_64__)
#define EVENT_TABLE(name) __asm__(name) __attribute__((used,visibility("hidden")))
#else
#define EVENT_TABLE(name)
#endif

struct alignas(128) EventIdLayout { A<u8,32> ctrl{},base{}; A<u8,64> pad{}; };

constexpr void event_id_lane(EventIdLayout &out,unsigned offset,const IdShape &id,int shift) {
    for(unsigned i=0;i<16;++i) {
        out.ctrl[offset+i]=id.ctrl[i]==0x80?0x80:u8(int(id.ctrl[i])+shift);
        out.base[offset+i]=id.subv[i];
    }
    for(unsigned i=0;i<16;++i) if(id.lz[i]) {
        out.ctrl[offset+7]=out.ctrl[offset+i];
        out.base[offset+7]='1';
    }
}
inline constexpr auto event_first_ids=id_shapes_raw('e',8,0,false);
inline constexpr auto event_second_ids=id_shapes_raw('u',8,0,true);
template<unsigned First> inline constexpr auto event_id_block=[]{
    A<EventIdLayout,64> rows{};
    for(unsigned second=0;second<64;++second) {
        auto &r=rows[second];r.ctrl.fill(0x80);
        if(second<=First) {r.base[0]=r.base[16]=1;continue;}
        unsigned raw=second-First-1;
        event_id_lane(r,0,event_first_ids[First],0);
        if(raw<=15 && !event_second_ids[raw].orv[1]) event_id_lane(r,16,event_second_ids[raw],int(raw)-15);
        else r.base[16]=1;
    }
    return rows;
}();
template<std::size_t... I> constexpr auto event_ids(std::index_sequence<I...>) {
    return A<A<EventIdLayout,64>,64>{event_id_block<I>...};
}
inline constexpr auto event_id_rows EVENT_TABLE("shopping_kernel_event_id_rows")=event_ids(std::make_index_sequence<64>{});

struct alignas(64) EventKindLayout {
    A<u8,16> text{},mask{};u64 pp{};
    i64 regular=std::numeric_limits<i64>::min(),purchase=std::numeric_limits<i64>::min(),pad{};
};
inline constexpr auto event_kinds EVENT_TABLE("shopping_kernel_event_kinds")=[]{
    A<EventKindLayout,256> rows{};
    auto add=[&](std::string_view s,u64 kind) {
        auto &r=rows[u8(s[1])];r.text[0]=r.text[s.size()+1]=',';
        for(unsigned i=0;i<s.size();++i)r.text[i+1]=u8(s[i]);
        for(unsigned i=0;i<s.size()+2;++i)r.mask[i]=255;
        r.pp=kind<<56;(kind==2?r.purchase:r.regular)=i64(s.size())-8;
    };
    add("view",0);add("cart",1);add("purchase",2);
    add("\"view\"",0);add("\"cart\"",1);add("\"purchase\"",2);
    return rows;
}();

inline constexpr auto event_tails EVENT_TABLE("shopping_kernel_event_tails")=[]{
    auto rows=tail_rows;
    for(unsigned i=0;i<rows.size();++i)rows[i].pad=u64(i64(i/TAIL_K+i%TAIL_K)-32);
    return rows;
}();

constexpr TsRow event_offset_clock(bool q) {
    auto r=ts_f12(true,q);r.a={};r.a.ctrl.fill(0x80);
    auto &a=r.a;unsigned len=25+2*q,offset=11+q;
    auto index=[len](unsigned k){return u8(k+16-len);};
    constexpr A<u8,8> limits={2,9,0,5,9,0,5,9};
    constexpr A<i8,8> weights={10,1,0,10,1,0,10,1};
    for(unsigned i=0;i<8;++i) {
        a.ctrl[i]=index(offset+i);a.base[i]="00:00:00"[i];a.lim[i]=limits[i];a.w1[i]=weights[i];
    }
    a.ctrl[8]=index(offset+8);a.base[8]='+';a.lim[8]=2;
    a.ctrl[9]=index(offset+9);a.base[9]='0';a.lim[9]=2;a.w1[9]=10;
    a.ctrl[10]=index(offset+10);a.base[10]='0';a.lim[10]=9;a.w1[10]=1;
    a.ctrl[11]=index(offset+8);a.base[11]='-';a.lim[11]=0xfe;
    a.ctrl[12]=index(offset+12);a.base[12]='0';a.lim[12]=5;a.w1[12]=10;
    a.ctrl[13]=index(offset+13);a.base[13]='0';a.lim[13]=9;a.w1[13]=1;
    if(q){a.ctrl[14]=index(offset+14);a.base[14]='"';}
    a.ctrl[15]=index(offset+11);a.base[15]=':';
    return r;
}
constexpr TsRow event_epoch_clock(unsigned n) {
    auto r=ts_invalid;r.a={};r.a.ctrl.fill(0x80);
    auto &a=r.a;auto &b=r.b;auto &c=r.c;
    a.base[8]=255;a.lim[8]=1;
    if(n==15) {
        a.ctrl[0]=1;a.base[0]='"';a.ctrl[15]=15;a.base[15]='"';
        constexpr A<i8,16> weights={0,1,10,1,10,1,10,1,0,1,10,1,10,1,1,0};
        for(unsigned i=1;i<15;++i)if(i!=8){a.ctrl[i]=i<8?i+1:i;a.base[i]='0';a.lim[i]=9;a.w1[i]=weights[i];}
        b.w2={100,1,100,1,100,1,10,1};b.w3={10000,1,1000,1,0,0,0,0};c.me=1000000;
    } else {
        unsigned k=n<7?n:7;
        for(unsigned t=0;t<n;++t){unsigned i=t<k?15-t:7-(t-k);a.ctrl[i]=15-t;a.base[i]='0';a.lim[i]=9;a.w1[i]=(i&1)?1:10;}
        b.w2={100,1,100,1,100,1,100,1};b.w3={10000,1,10000,1,0,0,0,0};c.me=10000000;
    }
    b.limv.fill(INT32_MAX);c.mf={1,1};return r;
}
inline constexpr auto event_clocks EVENT_TABLE("shopping_kernel_event_clocks")=[]{
    A<TjRow,130> rows{};
    for(unsigned j=0;j<128;++j){
        unsigned raw=j/2;
        auto e=raw>=1 && raw<=15?event_epoch_clock(raw):raw==25 || raw==27?event_offset_clock(raw==27):
            raw==20 || raw==22?ts_f12(false,raw==22):ts_invalid;
        auto &r=rows[2+j];r=tj_table[j];
        for(unsigned i=0;i<16;++i){r.a.ctrl[i]=e.a.ctrl[i];r.a.base[i]=e.a.base[i];r.b.lim[i]=e.a.lim[i];r.b.w1[i]=e.a.w1[i];}
        for(unsigned i=0;i<8;++i)r.c.w2[i]=e.b.w2[i];
        for(unsigned i=0;i<4;++i){r.c.w3[4+i]=e.b.w3[i];r.d.limv[i]=e.b.limv[i];}
        r.d.kmask2=e.b.kmaskv;r.e.kme[1]=e.c.me;r.e.kmf[1]=e.c.mf[0];
        r.f.ksel={};if(e.c.mf[0]==60000)r.f.ksel[8]=2;
        r.f.koff1=e.c.koff+1;r.f.pmask=e.c.dmask?DATE_PEXT:0;r.f.pad=i64(e.c.koff)-raw;
    }
    rows[0]=rows[1]=rows[2];return rows;
}();

struct alignas(64) EventVectors {
    A<u8,32> letters{},digit_limit{},letter_limit{},minus39{};
    A<u16,16> digit_pairs{}; A<u32,8> groups{}; A<u64,4> base36{};
    A<u8,32> comma{},newline{},quote{}; A<u64,4> outputs{},halves{};
};
inline constexpr auto event_vectors EVENT_TABLE("shopping_kernel_event_vectors")=[]{
    EventVectors c;
    c.minus39.fill(217);c.digit_pairs.fill(0x0124);c.groups.fill(0x00010510);c.base36.fill(1679616);
    c.comma.fill(',');c.newline.fill('\n');c.quote.fill('"');c.outputs={1,5,2,6};c.halves={0,1,6,7};
    for(unsigned i=0;i<32;++i) {
        auto k=i%16;
        if(k>=7){c.letters[i]=k==7?208:207;c.digit_limit[i]=k==7?8:9;c.letter_limit[i]=25;}
    }
    return c;
}();
static_assert(sizeof(EventIdLayout)==128 && sizeof(EventKindLayout)==64 && offsetof(EventKindLayout,regular)==40);
static_assert(offsetof(EventVectors,halves)==352 && offsetof(TailRow,pad)==56);
static_assert(offsetof(TjRow,f)+offsetof(TjF,pad)==376 && sizeof(TjRow)==512);
static_assert(offsetof(EvBatch,eid)==1024 && offsetof(EvBatch,uid)==1536 && offsetof(EvBatch,scratch)-512==2112 && offsetof(EvBatch,n)-512==4160);
#undef EVENT_TABLE
} // namespace pipeline::k_free::detail
