#pragma once
#include "complex.hpp"
#include "pqw_data.hpp"
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <string>
#include "pqw.hpp"

namespace pipeline::out {
using Path=std::filesystem::path;
using Range=std::pair<std::size_t,std::size_t>;
struct Piece {std::size_t tid,a,b;};
using Totals=std::array<i64,4>;
inline std::atomic_bool REGION_UNMAP{true};

[[noreturn]] inline void die(const char* why) {std::fprintf(stderr,"[pipeline] write error: %s\n",why);::_exit(1);}
struct View {
    const u32* keys;const i32 *day,*ses,*evs,*rev;
    const u8 *cat,*names;std::size_t nnames;const u32* noff;std::size_t nnoff,rows;
};
inline View view(const core::Core& c) {
    return {c.keys.as_ptr(),c.day.as_ptr(),c.ses.as_ptr(),c.evs.as_ptr(),c.rev.as_ptr(),
        c.cat.as_ptr(),c.names.as_ptr(),c.names.len(),c.names_off.as_ptr(),c.names_off.len(),c.day.len()};
}
inline void write_rows(const View& v,std::size_t a,std::size_t b,const Path& path,pqw::StrCol cats) {
    using namespace pqw;
    const auto n=b-a;
    const StrCol names{n?std::span(v.noff,v.nnoff):std::span<const u32>{},{v.names,v.nnames}};
    const std::array cols{
        Column{"user_id",Data::dict(names,Keys::u32off({v.keys+a,n},0),false,false)},
        Column{"day",Data::date({v.day+a,n},IntEnc::Dense)},
        Column{"sessions",Data::i32({v.ses+a,n},IntEnc::Dense)},
        Column{"events",Data::i32({v.evs+a,n},IntEnc::Auto)},
        Column{"revenue_cents",Data::i32({v.rev+a,n},IntEnc::Plain)},
        Column{"top_category",Data::dict(cats,Keys::u8({v.cat+a,n}),true,false)}};
    try {write_file(path,cols,{},Method::stream(1<<20));}
    catch (const std::exception& e) {die(e.what());}
}
struct Strings {
    std::vector<u32> offsets{0};
    std::vector<u8> data;
    void add(std::span<const u8> s) {
        data.insert(data.end(),s.begin(),s.end());offsets.push_back(u32(data.size()));
    }
    pqw::StrCol column() const {return {offsets,data};}
};
inline Path part(const Path& dir,std::size_t i) {
    return dir/("part-"+std::to_string(i)+".parquet");
}
template<class G> Totals write_campaign_part(const G& g,const Path& path,std::size_t lo,std::size_t hi) {
    using namespace pqw;
    const auto nd=g.nd,n=(hi-lo)*nd;
    std::vector<std::array<u64,4>> agg(n);
    for (std::size_t tid=0;tid<g.nt;++tid) {
        const auto& c=g.t(tid).core;
        for (std::size_t i=0;i<n;++i) {
            const auto j=lo*nd+i;auto& a=agg[i];
            a[0]+=c.agg[j][0];a[3]+=c.agg[j][1];a[1]+=c.aggp[j][0];a[2]+=c.aggp[j][1];
        }
    }
    Strings ids,channels;
    std::vector<i32> day;
    std::array<std::vector<i64>,4> values;
    i64 organic=0;
    constexpr std::array<u8,7> org={'o','r','g','a','n','i','c'};
    for(std::size_t cn=lo;cn<hi;++cn) for(std::size_t d=0;d<nd;++d) {
        const auto& a=agg[(cn-lo)*nd+d];
        if (!a[0] && !a[3]) continue;
        if (cn==g.nc) {ids.add(org);channels.add(org);organic+=i64(a[0]);}
        else {
            std::array<u8,24> buf;auto i=buf.size(),x=cn;
            do {buf[--i]=u8('0'+x%10);x/=10;} while(x);
            buf[--i]='_';buf[--i]='c';ids.add(std::span(buf).subspan(i));channels.add(g.channels[g.camp[cn]]);
        }
        day.push_back(g.day0+i32(d));
        for(std::size_t k=0;k<4;++k) values[k].push_back(i64(a[k]));
    }
    if (day.empty()) {ids.offsets.clear();channels.offsets.clear();}
    const std::array cols{
        Column{"campaign_id",Data::str(ids.column())},Column{"channel",Data::str(channels.column())},
        Column{"day",Data::date(day,IntEnc::Plain)},Column{"sessions",Data::i64(values[0],IntEnc::Plain)},
        Column{"purchasing_sessions",Data::i64(values[1],IntEnc::Plain)},Column{"revenue_cents",Data::i64(values[2],IntEnc::Plain)},
        Column{"assisted_sessions",Data::i64(values[3],IntEnc::Plain)}};
    if (!day.empty() || !lo) {
        try {write_file(path,cols,{},Method::whole());}
        catch(const std::exception& e){die(e.what());}
    }
    auto sum=[&](std::size_t k){return std::accumulate(values[k].begin(),values[k].end(),i64{});};
    return {sum(0),sum(2),sum(3),organic};
}
template<class G> void check_totals(const G& g,Totals totals) {
    i64 us=0,ur=0;
    for(std::size_t tid=0;tid<g.nt;++tid) {
        const auto& c=g.t(tid).core;
        auto sessions=c.ses.as_slice(),revenue=c.rev.as_slice();
        us=std::accumulate(sessions.begin(),sessions.end(),us);
        ur=std::accumulate(revenue.begin(),revenue.end(),ur);
    }
    const auto [cs,cr,ca,organic]=totals;
    std::fprintf(stderr,"[pipeline] sessions user=%lld campaign=%lld revenue user=%lld campaign=%lld assisted=%lld credited_non_organic=%lld\n",
        static_cast<long long>(us),static_cast<long long>(cs),static_cast<long long>(ur),static_cast<long long>(cr),
        static_cast<long long>(ca),static_cast<long long>(cs-organic));
    if (us!=cs || ur!=cr || ca<cs-organic) {std::fputs("[pipeline] output check failed\n",stderr);::_exit(5);}
}
inline std::pair<Path,Path> folders(const std::string& dir) {
    Path ud=Path(dir)/"user_daily",cd=Path(dir)/"campaign_daily";
    std::error_code err;std::filesystem::create_directories(ud,err);
    if(err) die("cannot create the output folders");
    std::filesystem::create_directories(cd,err);if(err) die("cannot create the output folders");
    return {ud,cd};
}
inline std::vector<Piece> cut(std::span<const Range> rows,std::size_t np) {
    std::vector<std::size_t> base(rows.size()+1);
    for(std::size_t t=0;t<rows.size();++t) base[t+1]=base[t]+rows[t].second-rows[t].first;
    const auto total=base.back();np=std::max<std::size_t>(1,std::min(np,total));
    std::vector<Piece> v;
    for(std::size_t p=0;p<np;++p) {
        const auto ga=total*p/np,gb=total*(p+1)/np;
        for(std::size_t t=0;t<rows.size();++t) {
            const auto a=std::max(ga,base[t]),b=std::min(gb,base[t+1]);
            if (a<b) v.push_back({t,rows[t].first+a-base[t],rows[t].first+b-base[t]});
        }
    }
    return v;
}
inline std::vector<Piece> cut_guided(std::span<const Range> rows,std::size_t nt) {
    std::size_t rem=0;for (auto [a,b]:rows) rem+=b-a;
    const auto min=std::max<std::size_t>(rem/(8*nt),4096);
    std::vector<Piece> v;
    std::size_t t=0,at=rows.empty()?0:rows[0].first;
    while(rem) {
        auto want=std::min(rem,std::max(rem/(2*nt),min));rem-=want;
        while(want) {
            while(at>=rows[t].second) {++t;at=rows[t].first;}
            const auto take=std::min(want,rows[t].second-at);
            v.push_back({t,at,at+take});at+=take;want-=take;
        }
    }
    return v;
}
template<class G> void stage5(G& g,const std::string& dir) {
    const auto [ud,cd]=folders(dir);Strings cats;
    for(const auto& c:g.cats) cats.add(c);
    std::vector<Range> rows;
    for(std::size_t t=0;t<g.nt;++t)rows.emplace_back(0,g.t(t).core.day.len());
    auto pieces=cut(rows,2*g.nt);if(pieces.empty()) pieces.push_back({0,0,0});
    std::atomic<std::size_t> next{};
    sys::run_par(g.nt,[&](std::size_t){
        for (;;) {
            const auto i=next.fetch_add(1,std::memory_order_relaxed);
            if (!i) {
                auto s=write_campaign_part(g,part(cd,0),0,g.nc+1);if (g.check) check_totals(g,s);
            } else if (i<=pieces.size()) {
                const auto [t,a,b]=pieces[i-1];
                write_rows(view(g.t(t).core),a,b,part(ud,i-1),cats.column());
            } else return;
        }
    });
}
template<class G> void stage45(G& g,const std::string& dir) {
    complex::stage4_pre(g);
    const auto [ud,cd]=folders(dir);Strings cats;
    for(const auto& c:g.cats) cats.add(c);
    const auto nt=g.nt;
    std::vector<View> before;std::vector<Range> rows;
    before.reserve(nt);rows.reserve(nt);
    for(std::size_t t=0;t<nt;++t) {before.push_back(view(g.t(t).core));rows.emplace_back(0,before.back().rows);}
    const auto pieces=cut_guided(rows,nt);const auto order=complex::shard_order(g);
    const auto ns=order.size(),np=pieces.size(),nf=2*nt,ncp=g.nc+1;
    constexpr auto relaxed=std::memory_order_relaxed,acquire=std::memory_order_acquire,acqrel=std::memory_order_acq_rel;
    g.next.store(0,relaxed);
    std::atomic<std::size_t> ca_done{},sh_next{},sh_done{},pq{},fq{};
    std::vector<Totals> csum(nt);
    const auto rg_len=g.files.empty()?0:g.files.back().off+g.files.back().len;
    const auto* mapped_input=g.region;
    std::atomic_bool rg_todo{REGION_UNMAP.load(relaxed) && rg_len && mapped_input};
    sys::run_par(nt,[&](std::size_t tid){
        complex::clicks_a(g,tid);ca_done.fetch_add(1,acqrel);
        complex::PScratch sc;
        for (;;) {
            if (ca_done.load(acquire)==nt && sh_next.load(relaxed)<ns) {
                const auto i=sh_next.fetch_add(1,relaxed);
                if (i<ns) {complex::person_shard(g,tid,order[i],sc);sh_done.fetch_add(1,acqrel);continue;}
            }
            const bool done=ca_done.load(acquire)==nt && sh_done.load(acquire)==ns;
            if (!done || fq.load(relaxed)>=nf) {
                if (rg_todo.load(relaxed) && rg_todo.exchange(false,acqrel)) {
                    // Clear the ownership pointer once. A later release pass must not
                    // advise a stale address that the OS may already have reused.
                    if (!::munmap(const_cast<u8*>(mapped_input),rg_len)) g.region=nullptr;
                    continue;
                }
                if (pq.load(relaxed)<np) {
                    const auto i=pq.fetch_add(1,relaxed);
                    if (i<np) {
                        const auto [t,a,b]=pieces[i];write_rows(before[t],a,b,part(ud,i),cats.column());continue;
                    }
                }
                if (!done) {sys::spin_loop();continue;}
            }
            const auto i=fq.fetch_add(1,relaxed);
            if (i>=nf) {if (pq.load(relaxed)<np) continue;break;}
            if (i<nt) csum[i]=write_campaign_part(g,part(cd,i),ncp*i/nt,ncp*(i+1)/nt);
            else {
                const auto t=i-nt;const auto v=view(g.t(t).core);
                if(v.rows>before[t].rows || (!np && !t)) write_rows(v,before[t].rows,v.rows,part(ud,np+t),cats.column());
            }
        }
    });
    if(g.check) {
        Totals total{};for(const auto& x:csum) for(unsigned k=0;k<4;++k) total[k]+=x[k];
        check_totals(g,total);
    }
}
}
