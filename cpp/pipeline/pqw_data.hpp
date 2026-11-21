#pragma once
#include "model_types.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace pipeline::pqw {
struct StrCol {
    std::span<const u32> offsets;
    std::span<const u8> data;
    std::size_t len() const {return offsets.empty()?0:offsets.size()-1;}
    std::size_t bytes_in(std::size_t a,std::size_t b) const {return a<b?offsets[b]-offsets[a]:0;}
    std::span<const u8> get(std::size_t i) const {return data.subspan(offsets[i],bytes_in(i,i+1));}
    StrCol slice(std::size_t a,std::size_t b) const {
        return {offsets.empty()?offsets:offsets.subspan(a,b-a+1),data};
    }
};
struct Keys {
    enum Form {K8,K16,K32,Off} form;
    const void *ptr;
    std::size_t n;
    pipeline::u32 base;
    static Keys u8(std::span<const pipeline::u8> v) {return {K8,v.data(),v.size(),0};}
    static Keys u16(std::span<const pipeline::u16> v) {return {K16,v.data(),v.size(),0};}
    static Keys u32(std::span<const pipeline::u32> v) {return {K32,v.data(),v.size(),0};}
    static Keys u32off(std::span<const pipeline::u32> v,pipeline::u32 base) {return {Off,v.data(),v.size(),base};}
    template<class F> decltype(auto) visit(F&& f) const {
        if (form==K8) return f(std::span(static_cast<const pipeline::u8*>(ptr),n));
        if (form==K16) return f(std::span(static_cast<const pipeline::u16*>(ptr),n));
        return f(std::span(static_cast<const pipeline::u32*>(ptr),n));
    }
    Keys slice(std::size_t a,std::size_t b) const {
        return visit([&](auto v){return Keys{form,v.subspan(a,b-a).data(),b-a,base};});
    }
};
enum class IntEnc {Plain,Dense,Hash,Auto};
struct Data {
    enum Form {I32,I64,Date,Str,Dict} form;
    IntEnc enc{};
    std::span<const pipeline::i32> a{};
    std::span<const pipeline::i64> b{};
    StrCol strings{};
    Keys keys{};
    bool nullable{},plain{};
    static Data i32(std::span<const pipeline::i32> v,IntEnc e) {return {I32,e,v};}
    static Data i64(std::span<const pipeline::i64> v,IntEnc e) {return {I64,e,{},v};}
    static Data date(std::span<const pipeline::i32> v,IntEnc e) {return {Date,e,v};}
    static Data str(StrCol s) {Data d{Str};d.strings=s;return d;}
    static Data dict(StrCol s,Keys k,bool opt,bool p) {return {Dict,{}, {},{},s,k,opt,p};}
    std::size_t rows() const {
        switch(form) {
            case I64:return b.size();case Str:return strings.len();case Dict:return keys.n;
            default:return a.size();
        }
    }
    int ptype() const {return form==I64?2:form==Str||form==Dict?6:1;}
    Data narrow_slice(std::size_t lo,std::size_t hi) const {
        auto d=*this;
        switch(form) {
            case I32: case Date:d.a=a.subspan(lo,hi-lo);break;
            case I64:d.b=b.subspan(lo,hi-lo);break;
            case Str:d.strings=strings.slice(lo,hi);break;
            case Dict:{
                d.keys=keys.slice(lo,hi);
                if (keys.form!=Keys::Off) break;
                if (nullable||plain) throw std::logic_error("offset dictionary encoding");
                if (hi==lo) {d.strings.offsets={};break;}
                const std::span v(static_cast<const pipeline::u32*>(d.keys.ptr),d.keys.n);
                const auto [mn,mx]=std::minmax_element(v.begin(),v.end());
                if (*mn<keys.base || (std::size_t(*mx)-keys.base)>=strings.len())
                    throw std::range_error("dictionary key");
                d.strings=strings.slice(*mn-keys.base,std::size_t(*mx-keys.base)+1);
                d.keys.base=*mn;
                break;
            }
        }
        return d;
    }
};
struct Column {std::string_view name; Data data;};
struct Opts {std::size_t rows_per_group=SIZE_MAX;};
struct Method {
    enum Form {Whole,Stream,Mmap} form;
    std::size_t limit{};
    bool populate{};
    static Method whole(){return {Whole};}
    static Method stream(std::size_t n){return {Stream,n};}
    static Method mmap(bool p){return {Mmap,0,p};}
};
inline u32 bits_for(u64 max) {return std::max(1,64-std::countl_zero(max));}
inline std::size_t varlen(u64 v) {return (bits_for(v)+6)/7;}
inline std::size_t hybrid_len(std::size_t n,u32 w) {
    const auto groups=(n+7)/8;
    return n?varlen((groups<<1)|1)+groups*w:0;
}
struct Plan {
    enum Form {Plain,Dense,Hash,Dict} form=Plain;
    i64 min{};
    std::size_t card{},m{},vbytes{};
    u32 w{};
    std::vector<i64> dict;
    std::vector<u32> keys;
};
template<class T> Plan plan_hash(std::span<const T> v) {
    Plan p;p.form=Plan::Hash;p.keys.reserve(v.size());
    const auto limit=std::max<std::size_t>(v.size()/2,16);
    std::vector<u32> slots(1024,UINT32_MAX);
    unsigned shift=54;
    auto bucket=[&](i64 x){return std::size_t((u64(x)*0x9e3779b97f4a7c15ULL)>>shift);};
    for (const i64 x:v) {
        auto h=bucket(x);
        while(slots[h]!=UINT32_MAX && p.dict[slots[h]]!=x) h=(h+1)&(slots.size()-1);
        if (slots[h]!=UINT32_MAX) {p.keys.push_back(slots[h]);continue;}
        if (p.dict.size()>=limit) return {};
        slots[h]=u32(p.dict.size());p.keys.push_back(slots[h]);p.dict.push_back(x);
        if (p.dict.size()*2<=slots.size()) continue;
        slots.assign(slots.size()*2,UINT32_MAX);--shift;
        for (std::size_t i=0;i<p.dict.size();++i) {
            auto j=bucket(p.dict[i]);
            while(slots[j]!=UINT32_MAX) j=(j+1)&(slots.size()-1);
            slots[j]=u32(i);
        }
    }
    p.card=p.dict.size();p.w=bits_for(p.card?p.card-1:0);return p;
}
template<class T> Plan plan_ints(std::span<const T> v,IntEnc enc) {
    if (v.empty()||enc==IntEnc::Plain) return {};
    if (enc==IntEnc::Hash) return plan_hash(v);
    T lo=std::numeric_limits<T>::max(),hi=std::numeric_limits<T>::min();
    for(auto x:v) {lo=std::min(lo,x);hi=std::max(hi,x);}
    const u64 range=u64(i64(hi))-u64(i64(lo));
    if (range>=(1u<<20)) return {};
    Plan p;p.form=Plan::Dense;p.min=lo;p.card=range+1;p.w=bits_for(range);
    if (enc==IntEnc::Auto && (p.card*sizeof(T)+(v.size()+7)/8*p.w)*10>v.size()*sizeof(T)*8) return {};
    return p;
}
inline Plan plan(const Data &d) {
    if(d.form==Data::I32||d.form==Data::Date) return plan_ints(d.a,d.enc);
    if(d.form==Data::I64) return plan_ints(d.b,d.enc);
    if(d.form==Data::Str) return {};
    Plan p;p.form=Plan::Dict;p.w=bits_for(d.strings.len()?d.strings.len()-1:0);
    if(!d.nullable && !d.plain) p.m=d.keys.n;
    else d.keys.visit([&](auto keys){
        using T=typename decltype(keys)::value_type;
        if(!d.plain) {for(T k:keys) p.m+=k!=std::numeric_limits<T>::max();return;}
        for (T k:keys) if(!d.nullable||k!=std::numeric_limits<T>::max()) {
            ++p.m;p.vbytes+=4+d.strings.bytes_in(k,std::size_t(k)+1);
        }
    });
    return p;
}
inline std::size_t chunk_bound(const Data& d,const Plan& p) {
    const auto n=d.rows();constexpr std::size_t hdr=64;
    if(d.form==Data::Str) return hdr+4*n+d.strings.bytes_in(0,n);
    if(d.form==Data::Dict) {
        const auto def=d.nullable?4+hybrid_len(n,1):0,nd=d.strings.len();
        return d.plain?hdr+def+p.vbytes:2*hdr+4*nd+d.strings.bytes_in(0,nd)+def+1+hybrid_len(p.m,p.w)+16;
    }
    if(p.form==Plan::Plain) return hdr+(d.form==Data::I64?8:4)*n;
    return 2*hdr+8*p.card+1+hybrid_len(n,p.w)+16;
}
}
