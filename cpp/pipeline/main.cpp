#include "setup.hpp"
#include "events.hpp"
#include "out.hpp"
#include "generic/mod.hpp"
#include <exception>

namespace pipeline {
bool toggle(const char* name,bool fallback) {
    const char* value=std::getenv(name);
    return value?std::string_view(value)=="1":fallback;
}
std::string directory(const char* name,const char* fallback) {
    const char* path=std::getenv(name);
    return path?path:fallback;
}
[[noreturn]] void run_generic(const std::string& data,const std::string& out,std::size_t n) {
    try {
        generic::run(data,out,n);
        ::_exit(0);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"[generic] %s\n",e.what());
        ::_exit(1);
    }
}
void check_base36() {
    u64 x=0x9e3779b9;
    std::vector<u8> buf;
    for(u64 i=0;i<2000000;++i) {
        x=x*6364136223846793005ULL+1442695040888963407ULL;
        const u64 y=i<100000?i:((x>>20)%2821109907456ULL)>>(i%41);
        buf.clear();keyrec::base36_encode(y,buf);
        const auto [v,n]=enc36(y);
        const auto word=__builtin_bswap64(v);
        if(n!=buf.size() || std::memcmp(buf.data(),&word,n) || (n<8 && (word>>(n*8))%256))
            sys::fallback("base36 conversion");
    }
}
void release_final(const G& g,bool force) {
    using Range=std::pair<const u8*,std::size_t>;
    std::array<std::array<Range,11>,16> ranges{};
    auto range=[]<class T>(const T *p,std::size_t n) {return Range{reinterpret_cast<const u8*>(p),n*sizeof(T)};};
    auto piece=[&](const u8* p,std::size_t len,std::size_t t) {
        if(!p||!len) return Range{};
        const auto a=len*t/g.nt,b=len*(t+1)/g.nt;
        return Range{p+a,b-a};
    };
    const auto total=g.files.empty()?0:g.files.back().off+g.files.back().len;
    for(std::size_t tid=0;tid<g.nt;++tid) {
        const auto& t=g.t(tid);const auto& c=t.core;
        ranges[tid]={range(c.names.as_ptr(),c.names.len()),range(c.names_off.as_ptr(),c.names_off.len()),
            range(c.keys.as_ptr(),c.keys.len()),range(c.day.as_ptr(),c.day.len()),range(c.ses.as_ptr(),c.ses.len()),
            range(c.evs.as_ptr(),c.evs.len()),range(c.rev.as_ptr(),c.rev.len()),range(c.cat.as_ptr(),c.cat.len()),
            range(t.edge.as_ptr(),t.edge.len()),piece(reinterpret_cast<const u8*>(g.csr),g.ncsr*16,tid),
            force?piece(g.region,total,tid):Range{}};
    }
    sys::run_par(g.nt,[&](std::size_t tid){
        for(auto [p,n]:ranges[tid]) if(n) sys::release(p,n);
        sys::release_heap_part(tid,g.nt);
    });
}
void report(const G& g,sys::Instant::time_point start) {
    for(auto& [stage,ms]:sys::laps) std::fprintf(stderr,"%s=%.1f ",stage.c_str(),ms);
    std::string line,hwm;
    std::ifstream status("/proc/self/status");
    while(std::getline(status,line)) if(line.starts_with("VmHWM:")) {hwm=line.substr(6);break;}
    std::array<u64,8> prof{};
    u64 ev=0,ck=0,lk=0,simple=0,cx=0,od=0,runs=0,rows=0,sessions=0;
    for(std::size_t i=0;i<g.nt;++i) {
        const auto& t=g.t(i);ev+=t.n_ev_rows;ck+=t.n_ck_rows;lk+=t.n_lk_rows;
        simple+=t.n_simple;cx+=t.n_complex;od+=t.n_odd;runs+=t.n_runs;rows+=t.core.day.len();sessions+=t.core.n_sess;
        for(std::size_t j=0;j<prof.size();++j) if(j==4) prof[j]=std::max(prof[j],t.prof[j]);else prof[j]+=t.prof[j];
    }
    std::fprintf(stderr,"total=%.1f ms peak_rss=%s\n", std::chrono::duration<double,std::milli>(sys::Instant::now()-start).count(),hwm.c_str());
    std::fprintf(stderr,"[pipeline] events=%llu clicks=%llu links=%llu simple=%llu complex=%llu odd=%llu runs=%llu sessions=%llu user_rows=%llu\n",
        (unsigned long long)ev,(unsigned long long)ck,(unsigned long long)lk,(unsigned long long)simple,(unsigned long long)cx,
        (unsigned long long)od,(unsigned long long)runs,(unsigned long long)sessions,(unsigned long long)rows);
    std::fprintf(stderr,"[pipeline] thread-ms scatter=%.1f compact=%.1f runs=%.1f links=%.1f max_bucket=%.1f\n",
        prof[0]/1e6,prof[1]/1e6,prof[2]/1e6,prof[3]/1e6,prof[4]/1e6);
}
[[noreturn]] void entry(int argc,char** argv) {
    const auto start=sys::Instant::now();sys::last_lap=start;
    const bool generic=argc>1 && std::string_view(argv[1])=="--generic";
    const bool paths=argc>=(generic?4:3);
    const auto data=paths?std::string(argv[argc-2]):directory("DATA_DIR","/data");
    const auto output=paths?std::string(argv[argc-1]):directory("OUTPUT_DIR","/output");
    auto cpus=events::env_number("NCPU").value_or(8);if(!cpus) cpus=8;
    if(generic) run_generic(data,output,cpus);
    sys::exec_init(argc,argv,data,output,cpus,&run_generic);
    std::set_terminate([]{sys::fallback("worker exception");});
    const auto nt=std::min<std::size_t>(16,cpus);
    const auto check=toggle("XL_CHECK",false),timing=toggle("XL_TIMING",false);
    sys::HUGE_PAGES.store(!toggle("XL_NOHUGE",false));sys::detect_pages();
    if(toggle("XL_ALLOC",true)) {sys::alloc_enable();std::pmr::set_default_resource(&sys::region_resource);}
    sys::FIRST_SMALL.store(toggle("XL_FIRST4K",false));sys::PREFAULT.store(toggle("XL_PREFAULT",false));
    sys::ANON_REGION.store(toggle("XL_ANONR",true));sys::CORE_NOTOUCH.store(toggle("XL_NOTOUCH",true));
    sys::SPIN_TICKS.store(events::env_number("XL_SPIN").value_or(SPIN_US_DEFAULT)*2000);
    sys::PIN_AT_START.store(parse::pin_mode()==2 && toggle("XL_PINSTART",false));
    sys::SETUP_KP.store(events::env_number("XL_KP").value_or(0));
    out::REGION_UNMAP.store(toggle("XL_RGREL",true));sys::SETUP_PARTS.store(toggle("XL_PPARTS",true));
    if(sys::PIN_AT_START.load()) sys::worker_pool().cpus=parse::cpu_order();
    if(toggle("XL_POOL",true)) sys::pool_start(nt);
    parse::NT=toggle("XL_NT",true);parse::RELEASE=toggle("XL_REL",true);parse::PREFETCH=toggle("XL_PF",false);
    sys::RELEASE.store(toggle("XL_FREE",true));
    if(check) check_base36();
    auto state=setup::setup(data,nt,check);auto& g=*state;
    sys::lap("setup");sys::run_par(nt,[&](std::size_t tid){parse::stage1(g,tid);});
    sys::lap("parse");links::stage2(g);sys::lap("st2sort");events::stage3(g);sys::lap("ev3b");
    const char* overlap=std::getenv("XL_OVL");
    if(!overlap||std::string_view(overlap)!="0") out::stage45(g,output);
    else {complex::stage4(g);sys::lap("complex");out::stage5(g,output);}
    sys::lap("write");if(timing||check) report(g,start);
    const char *end=std::getenv("XL_ENDREL");
    const bool force=end && std::string_view(end)=="2";
    if(force || (sys::SMALLPAGES.load() && toggle("XL_ENDREL",true))) release_final(g,force);
    if(timing) std::fprintf(stderr,"[pipeline] exit %.1f ms\n",std::chrono::duration<double,std::milli>(sys::Instant::now()-start).count());
    ::_exit(0);
}
}
int main(int argc,char** argv) {
    try {pipeline::entry(argc,argv);}
    catch(const std::exception& e) {pipeline::sys::fallback(e.what());}
}
