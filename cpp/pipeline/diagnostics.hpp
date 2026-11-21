#pragma once
#include "links_sort.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <time.h>
#include <vector>

namespace pipeline::diagnostics {
// These three switches use presence, including an empty string or "0".
inline bool memtest_requested() {return std::getenv("XL_MEMTEST")!=nullptr;}
inline bool sortbench_requested() {return std::getenv("XL_SORTBENCH")!=nullptr;}
inline bool debug_requested() {return std::getenv("XL_DBG")!=nullptr;}

[[gnu::noinline]] inline bool mem_test() {
    std::array<u8,4096> src;
    for(std::size_t i=0;i<src.size();++i) src[i]=u8((i*131)%251+1);
    // Load the symbols at the call site so this tests the routines supplied by
    // the linked image, including when building with intrinsic expansion enabled.
    auto *volatile copy=&std::memcpy;
    auto *volatile fill=&std::memset;
    auto *volatile move=&std::memmove;
    bool ok=true;
    for(std::size_t n=0;n<1100;++n) for(std::size_t off=0;off<9;++off) {
        std::array<u8,1216> d{};
        copy(d.data()+off,src.data()+3,n);
        for(std::size_t i=0;i<d.size();++i)
            ok&=d[i]==((i>=off && i<off+n)?src[3+i-off]:0);
        d.fill(7);
        fill(d.data()+off,0x5a,n);
        for(std::size_t i=0;i<d.size();++i)
            ok&=d[i]==((i>=off && i<off+n)?0x5a:7);
        for(unsigned dir=0;dir<2;++dir) {
            std::array<u8,2400> a,expected;
            std::copy_n(src.begin(),a.size(),a.begin());
            expected=a;
            const auto s=dir ? 100+off:100, dest=dir ? 100:100+off;
            move(a.data()+dest,a.data()+s,n);
            for(std::size_t i=0;i<n;++i) expected[dest+i]=src[s+i];
            ok&=a==expected;
        }
    }
    return ok;
}
inline void report_memtest(std::FILE* log=stderr) {
    std::fprintf(log,"[memtest] %s\n",mem_test()?"OK":"FAILED");
}
struct Random {
    u64 state=0x123456789abcdef0ULL;
    u64 next() {state^=state<<13;state^=state>>7;state^=state<<17;return state;}
};
inline constexpr auto CHECK_SIZES=[]{
    constexpr std::array<unsigned,26> more{47,48,49,63,64,65,100,127,128,129,200,255,256,257,300,
        400,511,512,513,600,1500,2047,2048,2049,5000,20000};
    std::array<unsigned,39+more.size()> all{};
    for(unsigned i=0;i<39;++i) all[i]=i+2;
    std::copy(more.begin(),more.end(),all.begin()+39);return all;
}();
inline constexpr std::array<unsigned,31> TIME_SIZES{2,3,4,5,6,7,8,9,12,15,16,17,24,31,32,33,
    40,48,63,64,100,128,200,300,512,600,1500,2048,5000,20000,75000};
inline void normalize_clicks(std::span<links::Click> rows) {
    for(std::size_t i=0;i<rows.size();) {
        auto end=i+1;
        while(end<rows.size() && !((rows[end][0]^rows[i][0])&~(links::IDX_BITS|u64{15}))) ++end;
        std::sort(rows.begin()+i,rows.begin()+end);
        i=end;
    }
}
template<class G> bool sort_case(const G& g,Random& rnd,links::Scratch& sc,std::size_t n,unsigned rep) {
    const std::array<u64,4> tr{4,1000,n,1800000000};
    const u64 cr=(rep%3)?2000:2;
    std::vector<links::Click> got(n);
    for(auto& c:got) {
        const auto ts=rnd.next()%tr[rep%4],campaign=rnd.next()%cr,alias=rnd.next()%14;
        c={(ts<<32)|(campaign<<4)|alias,rnd.next()% (u64{1}<<40)};
    }
    if(rep%97==96) {
        const auto i=rnd.next()%n;
        got[i][0]|=u64{1}<<(18+rnd.next()%14);
    }
    auto expected=got;
    links::slow_from(g,expected.data(),expected);
    links::sort_from(g,got.data(),got,sc);
    normalize_clicks(expected);normalize_clicks(got);
    return got==expected;
}
inline u64 ticks() {
#if defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#else
    using namespace std::chrono;
    return u64(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}
template<class G> void time_sort(const G& g,Random& rnd,links::Scratch& sc,std::size_t n,std::size_t reps,std::FILE* log) {
    std::vector<links::Click> data(n*reps);
    for(auto& c:data) {
        const auto ts=rnd.next()%1000000000,campaign=rnd.next()%2000,alias=rnd.next()%3;
        c={(ts<<32)|(campaign<<4)|alias,rnd.next()%(u64{1}<<40)};
    }
    auto start=ticks();
    std::size_t r=0;
    for(std::size_t i=0;i<reps;++i) {
        auto block=std::span(data).subspan(r*n,n);
        links::sort_from(g,block.data(),block,sc);r=(r+37)%reps;
    }
    const auto elapsed=double(ticks()-start);
#if defined(__x86_64__) || defined(__i386__)
    constexpr const char* unit="cycles";
#else
    constexpr const char* unit="ns";
#endif
    std::fprintf(log,"[sortbench] n=%zu: %.0f %s/call, %.1f per click\n",n,elapsed/reps,unit,elapsed/(reps*n));
}
template<class G> int sort_bench(const G& g,std::FILE* log=stderr) {
    links::Scratch sc;Random rnd;
    for(auto n:CHECK_SIZES) {
        const unsigned checks=n<=64?4000:n<=5000?300:40;
        for(unsigned rep=0;rep<checks;++rep) if(!sort_case(g,rnd,sc,n,rep)) {
            std::fprintf(log,"[sortbench] SORT WRONG n=%u rep=%u\n",n,rep);
            return 9;
        }
    }
    std::fprintf(log,"[sortbench] checks passed for %zu sizes\n",CHECK_SIZES.size());
    for(auto n:TIME_SIZES) time_sort(g,rnd,sc,n,std::max(400000u/n,4u),log);
    return 0;
}

inline void end_memory_report(const std::filesystem::path& proc="/proc/self",std::FILE* log=stderr) {
    std::string line;
    {
        std::ifstream input(proc/"status");
        while(std::getline(input,line))
            if(line.starts_with("Rss") || line.starts_with("VmPTE") || line.starts_with("VmRSS"))
                std::fprintf(log,"[dbg] end %s\n",line.c_str());
    }
    {
        std::ifstream input(proc/"smaps_rollup");
        while(std::getline(input,line)) std::fprintf(log,"[dbg] rollup %s\n",line.c_str());
    }
    {
        std::ifstream input(proc/"maps");
        if(input.is_open()) {
            std::size_t n=0;while(std::getline(input,line)) ++n;
            std::fprintf(log,"[dbg] vmas %zu\n",n);
        }
    }
    std::vector<std::pair<u64,std::string>> ranges;
    std::string header;
    auto word=[](std::string_view& v) {
        constexpr std::string_view whitespace=" \t\v\f\r\n";
        const auto start=v.find_first_not_of(whitespace);
        if(start==v.npos) {v={};return std::string_view{};}
        v.remove_prefix(start);
        const auto end=v.find_first_of(whitespace);
        const auto first=v.substr(0,end);
        v.remove_prefix(first.size());
        return first;
    };
    std::ifstream input(proc/"smaps");
    while(std::getline(input,line)) {
        std::string_view v(line);
        const auto first=word(v);
        if(line.starts_with("Rss:")) {
            const auto number=word(v);
            if(number.empty()) continue;
            u64 kb=0;
            const auto [end,err]=std::from_chars(number.data(),number.data()+number.size(),kb);
            if(err==std::errc{} && end==number.data()+number.size() && kb>1024) ranges.emplace_back(kb,header);
        } else if(line.find(':')==line.npos || first.find('-')!=first.npos) {
            header=line;
        }
    }
    std::sort(ranges.begin(),ranges.end(),std::greater{});
    for(std::size_t i=0;i<std::min<std::size_t>(ranges.size(),12);++i)
        std::fprintf(log,"[dbg] rss %llu kB %s\n",static_cast<unsigned long long>(ranges[i].first),ranges[i].second.c_str());
}

inline void odd_click_without_slot(u32 user,u32 alias,std::FILE* log=stderr) {
    std::fprintf(log,"[dbg] odd click of normal user %u alias %u without a free slot\n",user,alias);
}
inline void odd_click_without_slot(u32 user,u32 alias,std::size_t a,std::size_t b,u64 tok,std::FILE* log=stderr) {
    std::fprintf(log,"[dbg] odd click of normal user %u alias %u without a free slot: range %zu..%zu, tok %llx\n",
        user,alias,a,b,static_cast<unsigned long long>(tok));
}
template<class Plan> void partition_plan(const Plan& p,std::size_t total,std::FILE* log=stderr) {
    std::fprintf(log,"[dbg] heavy buckets [");
    for(std::size_t i=0;i<p.heavy.size();++i) {
        auto ub=p.heavy[i]->ub;
        std::fprintf(log,"%s(%zu, %zu)",i?", ":"",ub,p.base[ub+1]-p.base[ub]);
    }
    std::fprintf(log,"] total %zu\n",total);
}
template<class Plan> void indexed_plan(const Plan& p,std::size_t first,std::size_t end,std::size_t odd,std::FILE* log=stderr) {
    std::fprintf(log,"[dbg] index mode: slots %zu..%zu, odd clicks %zu, fixes %zu, heavy [",first,end,odd,p.fixes.size());
    for(std::size_t i=0;i<p.heavy.size();++i) {
        const auto& h=*p.heavy[i];
        std::fprintf(log,"%s(%zu, %zu, %zu)",i?", ":"",h.ub,h.pieces.size()-1,p.base[h.ub+1]-p.base[h.ub]);
    }
    std::fputs("]\n",log);
}
template<class Plan> void mode_a_plan(const Plan& p,std::span<const std::size_t> counts,std::size_t total,std::FILE* log=stderr) {
    std::size_t candidates=0;for(const auto& c:p.cand) candidates+=c.size();
    std::fprintf(log,"[dbg] mode A: records %zu, odd clicks of normal users %zu, heavy [",total,candidates);
    for(std::size_t i=0;i<p.heavy.size();++i) {
        auto ub=p.heavy[i]->ub;
        std::fprintf(log,"%s(%zu, %zu)",i?", ":"",ub,counts[ub]);
    }
    std::fputs("]\n",log);
}
inline double thread_cpu_ms() {
    timespec t{};
#if defined(CLOCK_THREAD_CPUTIME_ID)
    ::clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);
#endif
    return t.tv_sec*1000.0+t.tv_nsec/1e6;
}
inline void uf_finished(double start,std::size_t nodes,std::FILE* log=stderr) {
    std::fprintf(log,"[dbg] uf part2 cpu %.2f ms, nodes %zu\n",thread_cpu_ms()-start,nodes);
}
template<class G> void remaining_profile(const G& g,std::FILE* log=stderr) {
    u64 p1=0,p2=0,end=0;
    for(std::size_t i=0;i<g.nt;++i) {
        const auto& t=g.t(i);
        p1+=t.prof[5];p2+=t.prof[6];end+=t.prof[3];
    }
    std::fprintf(log,"[pipeline] st2b_pass1=%.1f st2b_pass2=%.1f st2b_sort=%.1f "
        "uf_nodes=%zu cx_users=%zu joined=%zu nb=%zu nub=%zu\n",
        p1/1e6,(p2-p1)/1e6,(end-p2)/1e6,g.uf.len(),g.cxu.size(),g.joined.size(),g.nb,g.nub);
}
}
