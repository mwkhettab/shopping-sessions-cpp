#ifdef NDEBUG
#undef NDEBUG
#endif
#include "diagnostics.hpp"
#include "links_part.hpp"
#include "model.hpp"
#include <cassert>
#include <unistd.h>

using namespace pipeline;
namespace d=diagnostics;

template<class F> std::string capture(F f) {
    std::FILE* stream=std::tmpfile();
    assert(stream);
    f(stream);
    std::rewind(stream);
    std::string output;
    std::array<char,512> b;
    while(auto n=std::fread(b.data(),1,b.size(),stream)) output.append(b.data(),n);
    assert(!std::ferror(stream));
    std::fclose(stream);
    return output;
}

void test_proc() {
    char dir[]="/tmp/shopping-diagnostics-XXXXXX";
    const char* generated=mkdtemp(dir);
    assert(generated);
    const std::filesystem::path proc(generated);
    assert(capture([&](auto f){d::end_memory_report(proc,f);}).empty());
    std::ofstream(proc/"status")<<"Name: test\nVmSize: 30\nRssFile: 60\nVmRSS: 70\nVmPTE: 80\n";
    std::ofstream(proc/"smaps_rollup")<<"rollup header\nRss: 123 kB\n";
    std::ofstream(proc/"maps")<<"first\nsecond";
    {
        std::ofstream smaps(proc/"smaps");
        for(unsigned i=0;i<15;++i)
            smaps<<"abc-def r--p 00000000 08:01 1 /f"<<i<<"\nSize: 700 kB\nRss: "<<3000+i<<" kB\n";
        smaps<<"abc-def range-b\nRss: 5000 kB\nabc-def range-a\nRss: 5000 kB\n"
               "abc-def small\nRss: 1024 kB\nabc-def malformed\nRss: 9800x kB\nRss:9800 kB\n";
    }
    auto output=capture([&](auto f){d::end_memory_report(proc,f);});
    std::string expected="[dbg] end RssFile: 60\n[dbg] end VmRSS: 70\n[dbg] end VmPTE: 80\n"
                         "[dbg] rollup rollup header\n[dbg] rollup Rss: 123 kB\n[dbg] vmas 2\n"
                         "[dbg] rss 5000 kB abc-def range-b\n[dbg] rss 5000 kB abc-def range-a\n";
    for(int i=14;i>=5;--i) expected+="[dbg] rss "+std::to_string(3000+i)+" kB abc-def r--p 00000000 08:01 1 /f"+std::to_string(i)+"\n";
    assert(output==expected);
    std::filesystem::remove_all(proc);
}

int main(int argc,char** argv) {
    G g;g.clen=310000000;
    if(argc==2 && std::string_view(argv[1])=="--full") return d::sort_bench(g);
    const std::array<std::pair<const char*,bool(*)()>,3> flags{{
        {"XL_MEMTEST",d::memtest_requested},{"XL_SORTBENCH",d::sortbench_requested},{"XL_DBG",d::debug_requested}}};
    for(auto [name,requested]:flags) {
        unsetenv(name);assert(!requested());
        setenv(name,"",1);assert(requested());
        setenv(name,"0",1);assert(requested());
        unsetenv(name);
    }
    assert(capture([](auto f){d::report_memtest(f);})=="[memtest] OK\n");
    static_assert(d::CHECK_SIZES.size()==65 && d::CHECK_SIZES[38]==40 && d::TIME_SIZES.back()==75000);
    d::Random rnd;
    assert(rnd.next()==0xfe800d6569fa1b4dULL);
    assert(rnd.next()==0x0ce7c497a6b09c3bULL);
    assert(rnd.next()==0x41dfb69a11db93c3ULL);
    links::Scratch sc;
    for(unsigned n:{2,9,47,64,128,129,257,2047,2048,2049,5000,20000})
        for(unsigned rep:{0,1,3,95,96}) assert(d::sort_case(g,rnd,sc,n,rep));
    auto timing=capture([&](auto f){d::time_sort(g,rnd,sc,9,13,f);});
    assert(timing.starts_with("[sortbench] n=9: ") && timing.find("/call, ")!=timing.npos && timing.ends_with(" per click\n"));
    test_proc();
    links::Plan plan;plan.base={0,1,98};
    plan.heavy.push_back(std::make_unique<links::Heavy>(1));
    plan.heavy[0]->pieces.resize(4);plan.fixes.resize(2);plan.cand.resize(2);plan.cand[1].resize(4);
    g.nt=1;g.workers.push_back(std::make_unique<TState>());g.t(0).prof={0,0,0,9000000,0,2000000,5000000,0};g.nb=2;g.nub=1;
    assert(capture([&](auto f){
        d::partition_plan(plan,98,f);
        d::indexed_plan(plan,10,200,3,f);
        d::mode_a_plan(plan,std::array<std::size_t,2>{1,97},98,f);
        d::odd_click_without_slot(5,9,f);
        d::odd_click_without_slot(5,9,100,150,0xfee,f);
        d::remaining_profile(g,f);
    })=="[dbg] heavy buckets [(1, 97)] total 98\n"
        "[dbg] index mode: slots 10..200, odd clicks 3, fixes 2, heavy [(1, 3, 97)]\n"
        "[dbg] mode A: records 98, odd clicks of normal users 4, heavy [(1, 97)]\n"
        "[dbg] odd click of normal user 5 alias 9 without a free slot\n"
        "[dbg] odd click of normal user 5 alias 9 without a free slot: range 100..150, tok fee\n"
        "[pipeline] st2b_pass1=2.0 st2b_pass2=3.0 st2b_sort=4.0 uf_nodes=0 cx_users=0 joined=0 nb=2 nub=1\n");
    assert(capture([](auto f){d::uf_finished(d::thread_cpu_ms(),7,f);}).ends_with(" ms, nodes 7\n"));
    std::puts("memory primitives, click sorting, proc reports and link diagnostics passed");
}
