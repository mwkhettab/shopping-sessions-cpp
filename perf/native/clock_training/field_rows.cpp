#define main unused_kernel_oracle
#include "kernel_rows.cpp"
#undef main
#include "k_free_tables.hpp"
#include <fstream>
#include <sstream>
#ifdef TRACE_SHARED_DATE
extern "C" std::uint64_t event_clock_checks[5];
#endif
using namespace pipeline::k_free::detail;
static std::string row(std::string ev,std::string ing,unsigned pattern) {
    if(pattern&1) ev='"'+ev+'"';
    if(pattern&2) ing='"'+ing+'"';
    return "eab705c83,u14b28a21,"+ev+','+ing+",view,p359,\n";
}
int main(int argc,char **argv) {
#ifdef REQUIRE_NATIVE_EVENT_KERNEL
    if(!pipeline::k_free::detail::available()) {std::puts("native wide event kernel unavailable");return 77;}
#endif
    cases<true>();
    for(unsigned i=0;i<128;++i) {
        const auto &r=T.tj[i];const auto calendar=r.f.pmask==DATE_PEXT;
        if(!((r.e.kme[1]==1000)==calendar && r.f.pmask3==DATE_PEXT && r.e.kme[0]==0 &&
             r.e.kme[2]==1000 && r.e.kme[3]==0))return 2;
        for(unsigned j=1;j<=11;++j) if(r.d.kmask2[j]!=(calendar?255:0)||r.d.kmask3[j]!=255)return 3;
        if(!calendar && std::any_of(r.d.kmask2.begin(),r.d.kmask2.end(),[](auto x){return x!=0;}))return 4;
    }
    auto index=[](unsigned dd){return (dd/10)|((dd%10)<<2);};
    for(unsigned day=1;day<=31;++day)if(!index(day))return 5;
    EvBatch batch;if(batch.scratch[2]||batch.scratch[3])return 6;
    const std::array<std::string,18> times={"0000-02-29T00:00:00Z","0001-01-01T00:00:00Z","1969-12-31T23:59:59Z",
        "1970-01-01T00:00:00Z","1900-02-29T23:58:00Z","2000-02-29T23:59:59Z","2024-04-31T00:00:00Z",
        "9999-12-31T23:59:59Z","2024-05-02T01:02:03Z","2025-05-02T01:02:03Z","2025-05-03T01:02:03Z",
        "2025-05-02T01:02:03-23:59","2025-05-02T01:02:03+23:59","2025-05-02T01:02:03+24:00",
        "1234567890123","0000000000000001","9223372036854775807","9223372036854775808"};
    std::string data;
    for(unsigned rep=0;rep<3;++rep)for(const auto& ev:times)for(const auto& ing:times)
        for(unsigned quoted=0;quoted<4;++quoted)data+=row(ev,ing,quoted);
    guarded<true>(data);
    data.clear();
    const std::string a="2024-05-02T23:58:59Z",b="2025-05-02T01:02:03Z";
    for(unsigned field=0;field<2;++field)for(unsigned j=0;j<12;++j)for(unsigned value=0;value<256;++value){
        auto ev=a,ing=b;(field?ing:ev)[j]=char(value);
        data+=row(ev,ing,value%4);
        if((value&3)==0) data+=row(a,b,value/4);
    }
    guarded<true>(data);
    for(unsigned offset=0;offset<64;++offset)guarded<true>(std::string_view(data).substr(offset,18010+offset*127));
    for(int file=1;file<argc;++file){
        std::ifstream f(argv[file],std::ios::binary);std::ostringstream out;out<<f.rdbuf();if(!f)return 7;
        auto sample=out.str();check<true>(sample,"sampled data windows");
#ifdef TRACE_SHARED_DATE
        std::fill_n(event_clock_checks,5,0);Rows single;collect<true,k_free::Free>(sample,single);
        auto *v=event_clock_checks;
        std::printf("sample %s: primary %llu secondary %llu numeric/shapeless %llu distinct-entry hits %llu ingest misses %llu total-output %zu\n",
            argv[file],(unsigned long long)v[0],(unsigned long long)v[1],(unsigned long long)v[2],(unsigned long long)v[3],(unsigned long long)v[4],single.rows.size());
#endif
    }
    std::puts("all 128 shape-mask/multiplier invariants; reference rows and flushes matched on UTC/offset/epoch, date collisions, invalid dates, every byte mutation at both date keys, guard pages, and all 64 alignments");
    return 0;
}
