#pragma once
#include "core.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <variant>

namespace pipeline::generic {
inline constexpr std::size_t ES=256,PS=256;
struct DatesAndDimensions {
    std::vector<B> cat_names,camp_ids,channels;
    Map<B,u32> products,campaigns;
    u32 unknown_cat{},organic{};
};
inline DatesAndDimensions read_dimensions(B prod,B camp) {
    DatesAndDimensions dim;
    std::vector<std::array<B,2>> products;
    for(auto b:chunks({&prod,1},SIZE_MAX/2)) for_lines(b,[&](B line) {
        if(auto f=split<2>(line)) products.push_back(*f);
    });
    for(auto [id,c]:products) dim.cat_names.push_back(c);
    dim.cat_names.push_back("unknown");
    std::sort(dim.cat_names.begin(),dim.cat_names.end());
    dim.cat_names.erase(std::unique(dim.cat_names.begin(),dim.cat_names.end()),dim.cat_names.end());
    Map<B,u32> rank;
    rank.reserve(dim.cat_names.size());
    for(u32 i=0;i<dim.cat_names.size();++i) rank.emplace(dim.cat_names[i],i);
    dim.products.reserve(products.size());
    for(auto [id,c]:products) dim.products.try_emplace(id,rank.at(c));
    dim.unknown_cat=rank.at("unknown");
    dim.campaigns.reserve(1024);
    for(auto b:chunks({&camp,1},SIZE_MAX/2)) for_lines(b,[&](B line) {
        if(auto f=split<2>(line)) {
            auto [id,ch]=*f;
            if(dim.campaigns.try_emplace(id,u32(dim.camp_ids.size())).second) {
                dim.camp_ids.push_back(id);dim.channels.push_back(ch);
            }
        }
    });
    dim.organic=u32(dim.camp_ids.size());
    dim.camp_ids.push_back("organic");dim.channels.push_back("organic");
    return dim;
}
inline std::vector<B> file_chunks(const std::vector<Mapped> &mapped,std::size_t ncpu) {
    std::vector<B> files;
    files.reserve(mapped.size());
    for(const auto &f:mapped) files.push_back(f.bytes());
    return chunks(files,chunk_target(files,ncpu));
}
struct P1Result {LinkMap links;std::vector<Parts<Ev>> ev_parts;std::vector<std::vector<Ck0>> ck_parts;};
inline P1Result parse_and_link(std::span<const B> lc,std::span<const B> ec,std::span<const B> cc,
                               const EvCtx &ectx,const Map<B,u32> &campaigns,std::size_t ncpu) {
    using Reply=std::variant<std::monostate,Parts<Ev>,std::vector<Ck0>>;
    std::optional<LinkMap> links;
    std::vector<LinkPart> link_parts(lc.size());
    std::atomic_size_t link_done{};
    auto result=par_run(lc.size()+ec.size()+cc.size(),ncpu,[]{return std::vector<std::pair<Ev,u32>>{};},
        [&](auto &tmp,std::size_t i)->Reply {
            if(i<lc.size()) {
                link_parts[i]=parse_links(lc[i]);
                // Each part has one producer. The last release/acquire observes every completed part.
                if(link_done.fetch_add(1,std::memory_order_acq_rel)+1==lc.size()) links=build_links(link_parts);
                return {};
            }
            i-=lc.size();
            if(i<ec.size()) return parse_events(ec[i],ectx,ES,tmp);
            return parse_clicks(cc[i-ec.size()],ectx.ws,ectx.we,campaigns);
        }).first;
    P1Result out{links?std::move(*links):build_links({}),{},{}};
    out.ev_parts.reserve(ec.size());out.ck_parts.reserve(cc.size());
    for(auto &r:result) {
        if(auto p=std::get_if<1>(&r)) out.ev_parts.push_back(std::move(*p));
        else if(auto c=std::get_if<2>(&r)) out.ck_parts.push_back(std::move(*c));
    }
    return out;
}
struct P2Bufs {DedupBufs dedup;std::vector<std::pair<Ck,u32>> ck;};
struct P2Result {std::vector<Parts<Rec>> recs;std::vector<Parts<Ck>> cks;};
inline P2Result dedup_and_route(const P1Result &input,std::size_t ncpu) {
    using Reply=std::variant<Parts<Rec>,Parts<Ck>>;
    auto result=par_run(ES+input.ck_parts.size(),ncpu,[]{return P2Bufs{};},[&](auto &bf,std::size_t i)->Reply {
        if(i<ES) return dedup_route(input.ev_parts,i,input.links,PS,bf.dedup);
        return route_clicks(input.ck_parts[i-ES],input.links,PS,bf.ck);
    }).first;
    P2Result out;
    out.recs.reserve(ES);out.cks.reserve(input.ck_parts.size());
    for(auto &r:result) {
        if(auto p=std::get_if<0>(&r)) out.recs.push_back(std::move(*p));
        else out.cks.push_back(std::move(std::get<1>(r)));
    }
    return out;
}
using UserShards=std::vector<std::vector<UserRow>>;
inline std::pair<UserShards,Acc> sessions(const P2Result &parts,const DatesAndDimensions &dim,std::size_t ncpu) {
    auto [rows,states]=par_run(PS,ncpu,[&]{return std::tuple{Scratch(dim.cat_names.size()),ShardBufs{},Acc{}};},
        [&](auto &st,std::size_t p) {
            auto &[scratch,buffers,acc]=st;
            return run_shard(parts.recs,parts.cks,p,dim.organic,scratch,buffers,acc);
        });
    Acc acc;
    for(auto &state:states) {
        auto &part=std::get<2>(state);
        if(acc.empty()) {acc=std::move(part);continue;}
        for(const auto &[k,v]:part) {
            auto &e=acc[k];
            for(unsigned i=0;i<4;++i) e[i]+=v[i];
        }
    }
    return {std::move(rows),std::move(acc)};
}
template<class F> inline void run_tables(const fs::path &data,std::size_t ncpu,F output) {
    ncpu=std::max<std::size_t>(ncpu,1);
    bool trace=std::getenv("V1ND_LOG")!=nullptr;
    using Clock=std::chrono::steady_clock;
    auto first=Clock::now(),last=first;
    auto lap=[&](const char *label) {
        if(trace) {
            auto now=Clock::now();
            using Ms=std::chrono::duration<double,std::milli>;
            std::fprintf(stderr,"[v1nd] %-12s %8.1f ms (total %8.1f ms)\n",label,Ms(now-last).count(),Ms(now-first).count());
            last=now;
        }
    };
    Mapped man(data/"manifest.json"),prod(data/"products.csv"),camp(data/"campaigns.csv");
    auto evm=map_dir(data/"events"),ckm=map_dir(data/"clicks"),lkm=map_dir(data/"links");
    auto [ws,we]=manifest(man.bytes());
    auto dim=read_dimensions(prod.bytes(),camp.bytes());
    auto ev=file_chunks(evm,ncpu),ck=file_chunks(ckm,ncpu),lk=file_chunks(lkm,ncpu);
    lap("setup");
    auto result=[&] {
        auto p=[&]{
            auto input=parse_and_link(lk,ev,ck,{ws,we,dim.products,dim.unknown_cat},dim.campaigns,ncpu);
            lap("parse+links");
            return dedup_and_route(input,ncpu);
        }();
        lap("dedup+route");
        return sessions(p,dim,ncpu);
    }();
    lap("sessions");
    output(result.first,result.second,dim,ncpu);
    lap("write");
    result={};
    lap("free");
}
}
