#include "../../cpp/pipeline/pqw.hpp"
#include <fstream>
using namespace pipeline;
using namespace pipeline::pqw;
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    const auto folder=std::filesystem::path(argv[1]);const auto n=std::stoul(argv[2]);
    std::vector<u8> bytes,select8,all_null(n,255);
    std::vector<u16> select16;
    std::vector<u32> off{0},str_off{0},select32,global;
    std::vector<i32> dense,spread,epoch;
    std::vector<i64> hash,extreme;
    for(u32 j=0;j<1031;++j) {
        const std::string s=j%7==0?std::string{}:"s"+std::to_string(j)+std::string(j%23,'x');
        bytes.insert(bytes.end(),s.begin(),s.end());off.push_back(u32(bytes.size()));
    }
    std::vector<u8> str;
    for(std::size_t i=0;i<n;++i) {
        select8.push_back(i%5?u8(i%21):255);
        select16.push_back(i%12?u16((i*577)%1031):65535);
        select32.push_back(i%7?u32(i%127):UINT32_MAX);
        global.push_back(97+u32(i%37));
        dense.push_back(i32(i%97)-40);spread.push_back(i32((i*10007)&INT32_MAX));
        epoch.push_back(20000+i32(i%14));hash.push_back(i64((i*577)%1021)*1000000001-4101);
        extreme.push_back(i%2?INT64_MAX:INT64_MIN);
        for(std::size_t j=0;j<i%18;++j) str.push_back(u8(97+(i+j)%26));
        str_off.push_back(u32(str.size()));
    }
    StrCol dict{off,bytes};
    const Column c[]={
        {"dense",Data::i32(dense,IntEnc::Dense)},{"spread",Data::i32(spread,IntEnc::Auto)},
        {"hash",Data::i64(hash,IntEnc::Hash)},{"extreme",Data::i64(extreme,IntEnc::Auto)},
        {"day",Data::date(epoch,IntEnc::Dense)},{"str",Data::str({str_off,str})},
        {"small",Data::dict(dict,Keys::u8(select8),true,false)},
        {"wide",Data::dict(dict,Keys::u16(select16),true,false)},
        {"plain",Data::dict(dict,Keys::u32(select32),true,true)},
        {"global",Data::dict(dict,Keys::u32off(global,91),false,false)},
        {"nil",Data::dict(dict,Keys::u8(all_null),true,false)}};
    const Method methods[]={Method::whole(),Method::stream(73),Method::mmap(false)};
    for(std::size_t j=0;j<3;++j)
        write_file(folder/(std::to_string(j)+".parquet"),c,Opts{8197},methods[j]);
    Out o;
    for(u32 w=1;w<=32;++w) for(auto count:{1,7,8,9,8191,8192,8193}) {
        const u32 mask=UINT32_MAX>>(32-w);
        emit_packed(o,std::size_t(count),w,[&](std::size_t i){return u32(i*97813+21)&mask;});
    }
    std::ofstream out(folder/"packed.bin",std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),std::streamsize(o.size()));
}
