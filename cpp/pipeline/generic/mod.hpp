#pragma once
#include "driver.hpp"
#include "../pqw.hpp"

namespace pipeline::generic {
struct StrBuf {
    std::vector<u32> off;
    std::vector<u8> data;
    StrBuf(std::size_t n,std::size_t bytes) {off.reserve(n+1);off.push_back(0);data.reserve(bytes);}
    void push(B s) {
        if(s.size()>UINT32_MAX-data.size()) throw std::length_error("string column exceeds 32-bit offsets");
        if(!s.empty()) {
            auto *p=reinterpret_cast<const u8 *>(s.data());
            data.insert(data.end(),p,p+s.size());
        }
        off.push_back(u32(data.size()));
    }
    pqw::StrCol col() const {return {off,data};}
};
inline void write_users(const fs::path &path,std::span<const std::vector<UserRow>> shards,std::span<const B> cat_names) {
    using namespace pqw;
    std::size_t n=0;
    for(const auto &s:shards) n+=s.size();
    StrBuf names(n/2,n*12),cats(cat_names.size(),0);
    std::vector<u32> ukeys,top;
    std::vector<i32> day;
    std::vector<i64> sessions,events,rev;
    ukeys.reserve(n);top.reserve(n);day.reserve(n);sessions.reserve(n);events.reserve(n);rev.reserve(n);
    std::optional<B> prev;
    for(const auto &shard:shards) for(auto &r:shard) {
        if(!prev || *prev!=r.name) {names.push(r.name);prev=r.name;}
        ukeys.push_back(u32(names.off.size()-2));day.push_back(r.day);
        sessions.push_back(i64(r.sessions));events.push_back(i64(r.events));rev.push_back(i64(r.revenue));
        top.push_back(r.top.value_or(UINT32_MAX));
    }
    for(B c:cat_names) cats.push(c);
    std::vector<u8> top8;
    std::vector<u16> top16;
    Keys tkeys=Keys::u32(top);
    if(cat_names.size()<UINT8_MAX) {
        top8.reserve(n);
        for(auto t:top) top8.push_back(u8(t));
        tkeys=Keys::u8(top8);
    } else if(cat_names.size()<UINT16_MAX) {
        top16.reserve(n);
        for(auto t:top) top16.push_back(u16(t));
        tkeys=Keys::u16(top16);
    }
    const Column cols[]={
        {"user_id",Data::dict(names.col(),Keys::u32(ukeys),false,false)},
        {"day",Data::date(day,IntEnc::Dense)},
        {"sessions",Data::i64(sessions,IntEnc::Auto)},
        {"events",Data::i64(events,IntEnc::Auto)},
        {"revenue_cents",Data::i64(rev,IntEnc::Plain)},
        {"top_category",Data::dict(cats.col(),tkeys,true,false)}
    };
    write_file(path,cols,Opts{},Method::stream(1<<20));
}
inline void write_campaigns(const fs::path &path,const Acc &acc,std::span<const B> ids,std::span<const B> channels) {
    using namespace pqw;
    std::size_t n=acc.size();
    StrBuf cid(n,n*8),channel(n,n*8);
    std::vector<i32> day;
    std::vector<i64> sessions,purch,rev,assists;
    day.reserve(n);sessions.reserve(n);purch.reserve(n);rev.reserve(n);assists.reserve(n);
    for(auto &[k,v]:acc) {
        auto [c,d]=unkey(k);
        cid.push(ids[c]);channel.push(channels[c]);day.push_back(d);
        sessions.push_back(i64(v[0]));purch.push_back(i64(v[1]));rev.push_back(i64(v[2]));assists.push_back(i64(v[3]));
    }
    const Column cols[]={
        {"campaign_id",Data::str(cid.col())},
        {"channel",Data::str(channel.col())},
        {"day",Data::date(day,IntEnc::Dense)},
        {"sessions",Data::i64(sessions,IntEnc::Auto)},
        {"purchasing_sessions",Data::i64(purch,IntEnc::Auto)},
        {"revenue_cents",Data::i64(rev,IntEnc::Plain)},
        {"assisted_sessions",Data::i64(assists,IntEnc::Auto)}
    };
    write_file(path,cols,Opts{},Method::stream(1<<20));
}
inline void clear_parquet(const fs::path &path) {
    for(const auto &e:fs::directory_iterator(path))
        if(e.symlink_status().type()==fs::file_type::regular && e.path().extension()==".parquet")
            fs::remove(e.path());
}
inline void run(const fs::path &data,const fs::path &out,std::size_t ncpu) {
    run_tables(data,ncpu,[&](const UserShards &rows,const Acc &acc,const DatesAndDimensions &dim,std::size_t nt) {
        auto ud=out/"user_daily",cd=out/"campaign_daily";
        fs::create_directories(ud);
        fs::create_directories(cd);
        clear_parquet(ud);clear_parquet(cd);
        auto n=std::clamp<std::size_t>(nt,1,PS),per=(PS+n-1)/n;
        par_run(n+1,nt,[]{return 0;},[&](int &,std::size_t k) {
            if(k==n) write_campaigns(cd/"part-0.parquet",acc,dim.camp_ids,dim.channels);
            else {
                auto a=std::min(k*per,PS),b=std::min((k+1)*per,PS);
                write_users(ud/("part-"+std::to_string(k)+".parquet"),
                            std::span<const std::vector<UserRow>>(rows).subspan(a,b-a),dim.cat_names);
            }
            return true;
        });
    });
}
}
