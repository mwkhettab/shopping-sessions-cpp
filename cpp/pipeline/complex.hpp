#pragma once
#include "events_runs.hpp"
#include "links_uf.hpp"
#include <tuple>

namespace pipeline::complex {
using Click = std::array<u64,3>;
template<class G,class T> void resolve_all(const G& g,T& t) {
    auto raw=std::move(t.cx_raw);t.cx_raw.clear();
    u64 last=UINT64_MAX;
    std::array<u64,16> cache;cache.fill(UINT64_MAX);
    for (auto r:raw) {
        u64 pk;
        if (r.w1&ODDU) {
            const auto ref=r.w1&(ODDU-1);
            pk=links::resolve_token(g,g.t(ref>>22).odd_users[ref&((1u<<22)-1)]);
        } else {
            if (r.w1!=last) {last=r.w1;cache.fill(UINT64_MAX);}
            auto& c=cache[r.w0&15];
            if (c==UINT64_MAX) c=links::resolve_normal(g,u32(r.w1),u32(r.w0&15));
            pk=c;
        }
        events::cpush(t,pk,r.w0);
    }
}
template<class G> void clicks_a(const G& g,std::size_t tid) {
    auto& t=g.t(tid);
    resolve_all(g,t);
    for (;;) {
        const auto i0=g.next.fetch_add(64,std::memory_order_relaxed);
        if (i0>=g.cxu.size()) break;
        for (auto i=i0;i<std::min(i0+64,g.cxu.size());++i) {
            auto u=g.cxu[i];
            std::array<u64,16> cache;cache.fill(UINT64_MAX);
            for (auto c:g.csr_range(u)) {
                u32 a=c[0]&15;
                if (cache[a]==UINT64_MAX) cache[a]=links::resolve_normal(g,u,a);
                t.cx_ck[shard(cache[a])].push_back({cache[a],c[0],c[1]});
            }
        }
    }
    auto odd=std::move(t.odd_clicks);
    for (auto c:odd) {auto pk=links::resolve_token(g,c[0]);t.cx_ck[shard(pk)].push_back({pk,c[1],c[2]});}
    t.odd_clicks=std::move(odd);
}
struct PScratch {
    std::vector<Rec> evs;
    std::vector<Click> cks;
    struct Decorated {unsigned __int128 rank;u64 id;Click row;};
    std::vector<Decorated> dec;
    std::vector<u8> name;
};
template<class G> std::vector<std::size_t> shard_order(const G& g) {
    std::array<std::pair<std::size_t,std::size_t>,NSHARD> work{};
    for (std::size_t s=0;s<NSHARD;++s) {
        work[s].second=s;
        for(std::size_t th=0;th<g.nt;++th) work[s].first+=g.t(th).cx_ev[s].size()+g.t(th).cx_ck[s].size();
    }
    std::sort(work.begin(),work.end(),std::greater{});
    std::vector<std::size_t> order;order.reserve(NSHARD);
    for (auto item:work) order.push_back(item.second);
    return order;
}
template<class G> void person_shard(const G& g,std::size_t tid,std::size_t s,PScratch& sc) {
    auto& t=g.t(tid);
    auto& [evs,cks,dec,name]=sc;
    evs.clear();cks.clear();
    for (std::size_t th=0;th<g.nt;++th) {
        const auto& a=g.t(th).cx_ev[s]; const auto& b=g.t(th).cx_ck[s];
        evs.insert(evs.end(),a.begin(),a.end());cks.insert(cks.end(),b.begin(),b.end());
    }
    if (evs.empty()) return;
    std::sort(evs.begin(),evs.end(),[](const Rec& a,const Rec& b){return std::tie(a.w1,a.w0)<std::tie(b.w1,b.w0);});
    if (std::all_of(cks.begin(),cks.end(),[](auto c){return !(c[2]&RAW);})) {
        dec.clear();dec.reserve(cks.size());
        for (auto c:cks)
            dec.push_back({(static_cast<unsigned __int128>(c[0])<<64)|(c[1]>>32),~enc36(c[2]).first,c});
        std::sort(dec.begin(),dec.end(),[](const auto& a,const auto& b){return std::tie(a.rank,a.id)<std::tie(b.rank,b.id);});
        for (std::size_t i=0;i<dec.size();++i) cks[i]=dec[i].row;
    } else {
        std::sort(cks.begin(),cks.end(),[&](const Click& a,const Click& b){
            const auto ac=std::pair{a[0],a[1]>>32},bc=std::pair{b[0],b[1]>>32};
            return ac!=bc ? ac<bc : events::cid_cmp(g,b[2],a[2])<0;
        });
    }
    std::size_t i=0,j=0;
    while (i<evs.size()) {
        const auto pk=evs[i].w1;auto end=i+1;
        while (end<evs.size() && evs[end].w1==pk) ++end;
        while (j<cks.size() && cks[j][0]<pk) ++j;
        t.ck.clear();
        while (j<cks.size() && cks[j][0]==pk) {t.ck.push_back({cks[j][1],cks[j][2]});++j;}
        links::name_of(g,pk,name);
        t.core.person(g,std::span(evs).subspan(i,end-i),t.ck,core::Name::Bytes(name));
        i=end;
    }
}
template<class G> void persons_b(const G& g,std::size_t tid,std::span<const std::size_t> order) {
    PScratch sc;
    for (;;) {
        auto i=g.next.fetch_add(1,std::memory_order_relaxed);
        if (i>=order.size()) return;
        person_shard(g,tid,order[i],sc);
    }
}
template<class G> void stage4_pre(G& g) {
    for (auto& [u,words]:g.extra) {
        auto& t=g.t(0);
        if (g.utab[u]&COMPLEX) {
            for (auto w:words) {t.cx_raw.push_back({w,u});++t.n_odd;}
        } else if (!(g.claim[u>>6].load(std::memory_order_relaxed)>>(u&63)&1)) {
            events::process_run(g,t,u,{});
        }
    }
    for (auto oe:g.odd_kept) {auto& t=g.t(0);t.cx_raw.push_back({oe.w0,oe.user});++t.n_odd;}
    std::vector<u32> users;
    for(std::size_t tid=0;tid<g.nt;++tid) {
        const auto& t=g.t(tid);
        users.insert(users.end(),t.cx_users.begin(),t.cx_users.end());
        users.insert(users.end(),t.mixed_users.begin(),t.mixed_users.end());
    }
    g.cxu=std::move(users);sys::lap("c.pre");
}
template<class G> void stage4(G& g) {
    stage4_pre(g);
    g.next.store(0,std::memory_order_relaxed);
    sys::run_par(g.nt,[&](std::size_t tid){clicks_a(g,tid);});
    sys::lap("c.a");
    const auto order=shard_order(g);
    g.next.store(0,std::memory_order_relaxed);
    sys::run_par(g.nt,[&](std::size_t tid){persons_b(g,tid,order);});
}
}
