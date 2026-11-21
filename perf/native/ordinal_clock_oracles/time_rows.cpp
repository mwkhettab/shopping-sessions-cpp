#define main unused_kernel_suite_main
#include "kernel_rows.cpp"
#undef main
#include <fstream>
#include <sstream>
#ifdef ORACLE_INSTRUMENT
extern "C" std::uint64_t clock_paths[3];
extern "C" bool check_raw_clock(unsigned shape,const void *bytes,const void *days);
#endif

namespace {
std::string event(std::string ev,std::string ing,unsigned quotes=0) {
    if(quotes&1)ev='"'+ev+'"';
    if(quotes&2)ing='"'+ing+'"';
    return "eab705c83,u14b28a21,"+ev+','+ing+",view,p359,\n";
}
void exhaustive_times() {
    const std::string iso="2024-02-29T23:34:21Z",utc="1969-12-31T01:28:38Z";
    constexpr std::array<std::string_view,6> dates={"0000-02-29","0001-01-01","1969-12-31","1900-03-01","2000-02-29","9999-12-31"};
    auto clock=[](unsigned v) {
        std::array<char,9> clock{};std::snprintf(clock.data(),clock.size(),"%02u:%02u:%02u",v/3600,v/60%60,v%60);return std::string(clock.data(),8);
    };
    auto epoch=[](std::uint64_t v) {
        std::array<char,14> buf{};std::snprintf(buf.data(),buf.size(),"%013llu",(unsigned long long)v);return std::string(buf.data(),13);
    };
    std::string data;
    for(unsigned second=0;second<86400;++second) {
        auto rev=86399-second;
        auto a=std::string(dates[second%dates.size()])+'T'+clock(second)+'Z';
        auto b=std::string(dates[rev%dates.size()])+'T'+clock(rev)+'Z';
        data+=event(a,b);
        data+=event(epoch((std::uint64_t(second)*7399825477ull+17)%10000000000000ull),b);
        if((second&2047)==2047 || second==86399) {guarded<true>(data);data.clear();}
    }
    std::size_t mutations=0;
    for(auto begin:{iso,std::string("1700000000123")})for(unsigned column=0;column<2;++column) {
        const auto &field=column?utc:begin;
        for(unsigned pos=0;pos<field.size();++pos) {
            for(unsigned byte=0;byte<256;++byte) {
                auto ev=begin,ing=utc;(column?ing:ev)[pos]=char(byte);
                data+=event(ev,ing);++mutations;
                if((byte&7)==0)data+=event(begin,utc);
                if((byte&15)==0)data+=event(ev,ing,(byte/16)%3+1);
            }
            guarded<true>(data);data.clear();
        }
    }
    for(auto ev:{"-123456789012","+123456789012","0000000000000","9999999999999","9999999999999999",
                 "9223372036854775807","9223372036854775808","-000000000000","2024-02-29T24:00:00Z",
                 "2023-02-29T23:59:59Z","2024-04-31T23:59:59Z","2024-02-29T23:59:59-23:59",
                 "2024-02-29T23:59:59+24:00","2024-02-29T23:60:00Z","17000000000,\"5","\"2024-02-29T23:59:59Z"}) {
        for(unsigned j=0;j<152;++j)data+=event(ev,(j&1)?iso:utc,j%4);
    }
    for(unsigned offset=0;offset<64;++offset)guarded<true>(Bytes(data).substr(offset,12000+offset*127));
    std::printf("Exhausted 86,400 seconds in UTC/UTC and epoch/UTC rows, %zu single-byte field substitutions, 64 guarded offsets.\n",mutations);
}
}

int main(int argc,char **argv) {
#ifdef REQUIRE_NATIVE_EVENT_KERNEL
    if(!k_free::detail::available()){std::puts("SKIP hardware wide event kernel is unavailable");return 77;}
#endif
    cases<true>();cases<false>();exhaustive_times();
#ifdef ORACLE_INSTRUMENT
    for(unsigned j=0;j<500000;++j) {
        std::array<std::uint64_t,4> raw{},days{};
        for(auto &v:raw)v=pick(std::numeric_limits<std::size_t>::max());
        for(auto &v:days)v=pick(std::numeric_limits<std::size_t>::max());
        for(unsigned shape:{26,40})if(!check_raw_clock(shape,raw.data(),days.data())) {
            std::fprintf(stderr,"raw vector mismatch at case %u shape %u\n",j,shape);return 3;
        }
    }
    std::puts("1,000,000 arbitrary 256-bit inputs matched all four raw result lanes and every validation bit.");
#endif
    for(int i=1;i<argc;++i) {
        std::ifstream in(argv[i],std::ios::binary);if(!in)return 2;std::ostringstream os;os<<in.rdbuf();auto slice=os.str();
        check<true>(slice,"byte-stratified published data windows");
#ifdef ORACLE_INSTRUMENT
        std::fill_n(clock_paths,3,0);Rows got;collect<true,k_free::Free>(slice,got);
        std::printf("sample %s: 20/20 %llu, 13/20 %llu, generic %llu, output rows %zu\n",
            argv[i],(unsigned long long)clock_paths[0],(unsigned long long)clock_paths[1],(unsigned long long)clock_paths[2],got.rows.size());
        if(!clock_paths[0]||!clock_paths[1]||!clock_paths[2])return 4;
#endif
    }
    std::puts("All reference rows and flush boundaries matched, with both guard placements.");return 0;
}
