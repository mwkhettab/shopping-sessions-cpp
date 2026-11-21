#pragma once
#include "util.hpp"
#include "../k_ref.hpp"

namespace pipeline::generic {
inline constexpr i64 DAY_MS=86400000;
inline std::vector<B> chunks(std::span<const B> files,std::size_t target) {
    target=std::max<std::size_t>(1,target);
    std::vector<B> out;
    for (B file:files) {
        auto p=find_byte(file,'\n');
        if(p==B::npos) continue;
        B body=file.substr(p+1);
        for (std::size_t s=0;s<body.size();) {
            auto e=s+std::min(target,body.size()-s);
            if(e<body.size()) {
                auto nl=find_byte(body.substr(e),'\n');
                e=nl==B::npos?body.size():e+nl+1;
            }
            out.push_back(body.substr(s,e-s));
            s=e;
        }
    }
    return out;
}
inline std::size_t chunk_target(std::span<const B> files,std::size_t ncpu) {
    std::size_t total=0;
    for(auto f:files) total+=f.size();
    return std::clamp<std::size_t>(total/std::max<std::size_t>(1,ncpu*16),1<<20,16<<20);
}
inline std::size_t count_lines(B b) {return count_byte(b,'\n');}
template<class F> inline void for_lines(B chunk,F f) {
    std::size_t start=0,base=0;
    for (;base+64<=chunk.size();base+=64) {
        auto bits=eq_mask64(chunk.data()+base,'\n');
        while(bits) {
            auto p=base+std::countr_zero(bits);
            if(p>start) f(chunk.substr(start,p-start));
            start=p+1;
            bits&=bits-1;
        }
    }
    for(auto p=base;p<chunk.size();++p) if(chunk[p]=='\n') {
        if (p>start) f(chunk.substr(start,p-start));
        start=p+1;
    }
    if(start<chunk.size()) f(chunk.substr(start));
}
inline std::optional<B> unquote(B f,bool quote) {
    if(!quote) return f;
    if(f.size()<2 || f.front()!='"' || f.back()!='"') return {};
    f.remove_prefix(1);f.remove_suffix(1);
    if(f.find('"')!=B::npos) return {};
    return f;
}
template<std::size_t N> inline std::optional<std::array<B,N>> split(B line) {
    std::array<B,N> out;
    std::size_t index=0,start=0;
    bool quote=false;
    for(std::size_t i=0;i<line.size();++i) {
        if(line[i]==',') {
            if(index+1>=N) return {};
            auto f=unquote(line.substr(start,i-start),quote);
            if(!f) return {};
            out[index++]=*f;
            start=i+1;
            quote=false;
        } else if(line[i]=='"') quote=true;
    }
    if(index+1!=N) return {};
    auto last=unquote(line.substr(start),quote);
    if(!last) return {};
    out[index]=*last;
    return out;
}
// These spans are reconstructed only for lines accepted by split.
inline B stripped(const char *p,const char *end) {
    if(p<end && *p=='"') return {p+1,std::size_t(end-p-2)};
    return {p,std::size_t(end-p)};
}
inline B first(const char *line) {
    auto end=line;
    while(*end!=',') ++end;
    return stripped(line,end);
}
inline std::pair<B,B> first_two(const char *line) {
    auto mid=line;
    while(*mid!=',') ++mid;
    auto end=mid+1;
    while(*end!=',') ++end;
    return {stripped(line,mid),stripped(mid+1,end)};
}
inline std::optional<i64> parse_ts(B s) {
    if(s.empty()) return {};
    if(std::all_of(s.begin(),s.end(),k_ref::digit)) {
        auto i=s.find_first_not_of('0');
        if(i==B::npos) return 0;
        s.remove_prefix(i);
        if(s.size()>18) return {};
        i64 n=0;
        for(char c:s) n=n*10+c-'0';
        return n;
    }
    auto ts=k_ref::parse_ts(s);
    if(ts==TS_BAD) return {};
    return ts;
}
inline std::optional<u32> parse_price(B s) {
    auto v=k_ref::parse_price(s);
    if(v) return v;
    return {};
}
inline i32 day_of(i64 t) {return i32(t/DAY_MS-(t%DAY_MS<0));}
inline std::pair<i64,i64> manifest(B bytes) {
    auto get=[&](B key){
        auto k=bytes.find(key);
        if(k==B::npos) throw std::runtime_error("manifest missing "+std::string(key));
        auto rest=bytes.substr(k+key.size());
        for (char c:{':','"'}) {
            auto n=find_byte(rest,c);
            if(n==B::npos) throw std::runtime_error("manifest missing value for "+std::string(key));
            rest.remove_prefix(n+1);
        }
        auto n=find_byte(rest,'"');
        if(n==B::npos) throw std::runtime_error("manifest unterminated value for "+std::string(key));
        auto time=parse_ts(rest.substr(0,n));
        if(!time) throw std::runtime_error("manifest invalid timestamp for "+std::string(key));
        return *time;
    };
    return {get("\"window_start\""),get("\"window_end\"")};
}
}
