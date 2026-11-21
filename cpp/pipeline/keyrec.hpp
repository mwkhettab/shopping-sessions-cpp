#pragma once
#include "k_ref.hpp"
#include <bit>
#include <cstring>
#include <span>
#include <string>
#include <unordered_set>

namespace pipeline::keyrec {
using Bytes = std::string_view;
inline constexpr u64 MASK=(u64{1}<<40)-1, A=0x9e3779b97f4bULL, B=0xd6e8feb86659ULL;
inline constexpr u64 ALIAS_SLOTS=512, KIOSK_BASE=u64{1}<<39;
inline constexpr u64 inverse(u64 a) {
    u64 x=a;
    for (unsigned i=0;i<6;++i) x*=2-a*x;
    return x&MASK;
}
inline constexpr u64 AINV=inverse(A), BINV=inverse(B);
inline constexpr u64 scramble(u64 value,u64 key) {
    u64 m=(((value&MASK)*A)^key)&MASK;
    m^=m>>17;
    return (m*B)&MASK;
}
inline constexpr u64 unshuffle(u64 y) {
    u64 x=(y*BINV)&MASK;
    return x^(x>>17)^(x>>34);
}
inline constexpr u64 unkey(u64 x,u64 key) {return (((x^key)&MASK)*AINV)&MASK;}
inline constexpr u64 unscramble(u64 y,u64 key) {return unkey(unshuffle(y),key);}
inline void base36_encode(u64 value,std::vector<u8> &out) {
    constexpr char alphabet[]="0123456789abcdefghijklmnopqrstuvwxyz";
    std::array<u8,13> buf;
    auto i=buf.size();
    do {buf[--i]=u8(alphabet[value%36]); value/=36;} while(value);
    out.insert(out.end(),buf.begin()+i,buf.end());
}
inline std::optional<u64> base36_decode(Bytes s) {
    if (s.empty() || s.size()>8 || (s.size()>1 && s[0]=='0')) return {};
    u64 v=0;
    for (char c:s) {
        if ('0'<=c && c<='9') v=v*36+u64(c-'0');
        else if ('a'<=c && c<='z') v=v*36+u64(c-'a'+10);
        else return {};
    }
    if (v>MASK) return {};
    return v;
}
inline constexpr u64 ONES=0x0101010101010101ULL, HI=ONES*0x80;
inline constexpr std::pair<u64,bool> base36_word(u64 w,u32 n) {
    const auto sh=8*(8-n);
    const u64 w2=(w<<sh)|(((ONES*0x30)>>1)>>(63-sh));
    const u64 ge0=(w2+ONES*0x50)&HI, gt9=(w2+ONES*0x46)&HI;
    const u64 gea=(w2+ONES*0x1f)&HI, gtz=(w2+ONES*0x05)&HI;
    const u64 good=~w2&HI&ge0&(~gt9|(gea&~gtz));
    const u64 d=w2-ONES*0x30-(gea>>7)*39;
    const u64 p=(d&0x00ff00ff00ff00ffULL)*36+((d>>8)&0x00ff00ff00ff00ffULL);
    const u64 q=(p&0x0000ffff0000ffffULL)*1296+((p>>16)&0x0000ffff0000ffffULL);
    const u64 r=(q&UINT32_MAX)*1679616+(q>>32);
    return {r,good==HI && (n==1 || (w&255)!='0') && r<=MASK};
}
inline std::optional<u64> base36_decode_at(Bytes buf,std::size_t start,std::size_t len) {
    if (!len || len>8 || start>buf.size() || len>buf.size()-start) return {};
    u64 w=0;
    std::memcpy(&w,buf.data()+start,buf.size()-start>=8 ? 8 : len);
    if constexpr(std::endian::native==std::endian::big) w=__builtin_bswap64(w);
    auto [v,ok]=base36_word(w,u32(len));
    if (!ok) return {};
    return v;
}
inline std::optional<u64> base36_decode_swar(Bytes s) {return base36_decode_at(s,0,s.size());}
inline std::size_t base36_batch(std::span<const u64> words,std::span<const u8> lens,std::span<u64> out) {
    const auto count=std::min({words.size(),lens.size(),out.size()});
    std::size_t bad=0;
    for (std::size_t i=0;i<count;++i) {
        auto [v,ok]=base36_word(words[i],std::clamp<u32>(lens[i],1,8));
        out[i]=v; bad+=!ok;
    }
    return bad;
}
inline std::optional<u64> id_x(Bytes s,char prefix) {
    if (s.size()<2 || s[0]!=prefix) return {};
    auto v=base36_decode_at(s,1,s.size()-1);
    if (v) return unshuffle(*v);
    return {};
}
inline bool split_line(Bytes line,std::span<Bytes> out) {
    if (line.empty()) return false;
    std::size_t k=0;
    for (;;) {
        if (k==out.size()) return false;
        const auto off=line.find(',');
        auto f=line.substr(0,off);
        if (f.starts_with('"')) {
            if (f.size()<2 || !f.ends_with('"')) return false;
            f=f.substr(1,f.size()-2);
        }
        if (f.find('"')!=Bytes::npos) return false;
        out[k++]=f;
        if (off==Bytes::npos) break;
        line.remove_prefix(off+1);
    }
    return k==out.size();
}
inline std::optional<i64> parse_ts(Bytes s) {
    auto v=k_ref::parse_ts(s);
    if (v==TS_BAD) return {};
    return v;
}
struct Keys {u64 user,event,click; friend bool operator==(const Keys&,const Keys&)=default;};
struct EventRow {u64 id_x; std::optional<u64> user_x; std::optional<i64> ts;};
struct ClickRow {u64 id_x; std::optional<u64> user_x;};
struct Sample {
    std::vector<EventRow> events;
    std::vector<ClickRow> clicks;
    void add_event_lines(Bytes bytes,std::size_t max) {
        std::array<Bytes,7> fields;
        for (;;) {
            if (events.size()>=max) return;
            auto end=bytes.find('\n');
            if (split_line(bytes.substr(0,end),fields))
                if (auto id=id_x(fields[0],'e')) events.push_back({*id,id_x(fields[1],'u'),parse_ts(fields[2])});
            if (end==Bytes::npos) return;
            bytes.remove_prefix(end+1);
        }
    }
    void add_click_lines(Bytes bytes,std::size_t max) {
        std::array<Bytes,4> fields;
        for (;;) {
            if (clicks.size()>=max) return;
            auto end=bytes.find('\n');
            if (split_line(bytes.substr(0,end),fields))
                if (auto id=id_x(fields[0],'k')) clicks.push_back({*id,id_x(fields[1],'u')});
            if (end==Bytes::npos) return;
            bytes.remove_prefix(end+1);
        }
    }
};
inline constexpr u64 USER_INDEX_LIMIT=u64{1}<<23, ROW_INDEX_LIMIT=u64{1}<<28;
inline constexpr std::size_t MIN_DISTINCT=20;
struct Stats {u64 user_candidates_tried{},event_steps{},click_steps{};};
inline std::vector<u64> sorted_unique(std::vector<u64> v) {
    std::sort(v.begin(),v.end()); v.erase(std::unique(v.begin(),v.end()),v.end()); return v;
}
inline bool user_ok(u64 v) {return (v>>9)<USER_INDEX_LIMIT || (v>=KIOSK_BASE && v<KIOSK_BASE+256);}
template<class Fn> bool verify(std::span<const u64> xs,u64 key,Fn ok,std::size_t slack) {
    std::size_t missed=0;
    for (u64 x:xs) if (!ok(unkey(x,key)) && ++missed>slack) return false;
    return true;
}
inline std::optional<u64> recover_user_key(const Sample &s,Stats &st) {
    std::vector<u64> all;
    for (const auto &row:s.events) if (row.user_x) all.push_back(*row.user_x);
    for (const auto &row:s.clicks) if (row.user_x) all.push_back(*row.user_x);
    std::sort(all.begin(),all.end());
    std::vector<std::pair<u32,u64>> runs;
    for (u64 x:all) {
        if (!runs.empty() && runs.back().second==x) ++runs.back().first;
        else runs.emplace_back(1,x);
    }
    if (runs.size()<MIN_DISTINCT) return {};
    std::vector<u64> distinct;
    for (auto r:runs) distinct.push_back(r.second);
    const auto top=std::min<std::size_t>(runs.size(),16);
    auto frequent=[](auto a,auto b){return a.first>b.first;};
    std::nth_element(runs.begin(),runs.begin()+top-1,runs.end(),frequent);
    std::sort(runs.begin(),runs.begin()+top,frequent);
    std::array<u64,3> probe{distinct.front(),distinct[distinct.size()/2],distinct.back()};
    std::optional<u64> found;
    for (std::size_t j=0;j<top;++j) for (u64 i=0;i<64;++i) {
        ++st.user_candidates_tried;
        const u64 key=(runs[j].second^((i*ALIAS_SLOTS)*A))&MASK;
        if (!std::all_of(probe.begin(),probe.end(),[&](auto p){return user_ok(unkey(p,key));})) continue;
        if (verify(distinct,key,user_ok,0)) {
            if (found && *found!=key) return {};
            found=key;
        }
    }
    return found;
}
inline std::optional<u64> search_key(u64 x1,u64 limit,u64 x2,u64 x3,std::span<const u64> values,u64 &steps) {
    const auto slack=values.size()/1000;
    for (u64 base=0;base<limit;base+=16) {
        u32 hit=0;
        for (unsigned j=0;j<16;++j) {
            const u64 key=x1^(((base+j)*A)&MASK);
            const u64 a=((x2^key)*AINV)&MASK,b=((x3^key)*AINV)&MASK;
            hit|=u32((a<ROW_INDEX_LIMIT)&(b<ROW_INDEX_LIMIT))<<j;
        }
        while (hit) {
            unsigned j=std::countr_zero(hit); hit&=hit-1;
            const u64 key=x1^(((base+j)*A)&MASK);
            if (verify(values,key,[](u64 x){return x<ROW_INDEX_LIMIT;},slack)) {
                steps+=base+j+1; return key;
            }
        }
    }
    steps+=limit; return {};
}
inline std::optional<std::pair<u64,u64>> probes(std::span<const u64> distinct,u64 exclude) {
    std::optional<u64> a,b;
    for (u64 x:distinct) if (x!=exclude) {if (!a) a=x; else b=x;}
    if (a && b) return std::pair{*a,*b};
    return {};
}
inline std::optional<u64> recover_event_key(const Sample &s,u64 key,std::optional<i64> ws,Stats &st) {
    std::vector<u64> ids;
    std::vector<std::pair<i64,u64>> rows;
    for (auto &r:s.events) {
        ids.push_back(r.id_x);
        if (r.user_x && (unkey(*r.user_x,key)>>9)==0)
            rows.emplace_back(r.ts && (!ws || *r.ts>=*ws) ? *r.ts : INT64_MAX,r.id_x);
    }
    ids=sorted_unique(std::move(ids));
    if (ids.size()<MIN_DISTINCT || rows.empty()) return {};
    std::sort(rows.begin(),rows.end());
    std::unordered_set<u64> seen;
    std::erase_if(rows,[&](auto &r){return !seen.insert(r.second).second;});
    for (auto [take,limit]:{std::pair{4u,u64{1}<<16},std::pair{2u,u64{1}<<27}}) {
        for (std::size_t i=0;i<std::min<std::size_t>(rows.size(),take);++i) {
            const u64 x1=rows[i].second;
            auto p=probes(ids,x1); if (!p) return {};
            if (auto k=search_key(x1,limit,p->first,p->second,ids,st.event_steps)) return k;
        }
    }
    return {};
}
inline std::optional<u64> recover_click_key_lim(const Sample &s,u64 key,u64 limit,Stats &st) {
    std::vector<u64> ids,rows;
    std::unordered_set<u64> seen;
    for (auto &r:s.clicks) {
        ids.push_back(r.id_x);
        if (r.user_x && (unkey(*r.user_x,key)>>9)==0 && seen.insert(r.id_x).second) rows.push_back(r.id_x);
    }
    ids=sorted_unique(std::move(ids));
    if (ids.size()<MIN_DISTINCT) return {};
    for (std::size_t i=0;i<std::min<std::size_t>(2,rows.size());++i) {
        auto p=probes(ids,rows[i]); if (!p) return {};
        if (auto k=search_key(rows[i],std::min<u64>(limit,u64{1}<<27),p->first,p->second,ids,st.click_steps)) return k;
    }
    return {};
}
inline std::optional<u64> recover_click_key(const Sample &s,u64 key,Stats &st) {
    return recover_click_key_lim(s,key,u64{1}<<27,st);
}
inline std::optional<Keys> recover(const Sample &s,std::optional<i64> ws,Stats &st) {
    auto u=recover_user_key(s,st); if (!u) return {};
    auto e=recover_event_key(s,*u,ws,st); if (!e) return {};
    auto c=recover_click_key(s,*u,st); if (!c) return {};
    return Keys{*u,*e,*c};
}
}
