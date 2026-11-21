#include "out_user_groups.hpp"
#include "out_clean.hpp"
#include <fstream>
#include <iostream>
using namespace pipeline;
struct V {
    const u32* keys;
    const i32 *day,*ses,*evs,*rev;
    const u8 *cat,*names;std::size_t nnames;
    const u32* noff;std::size_t nnoff;
};
int main(int argc,char** argv) {
    const std::filesystem::path root=argv[1];
    std::filesystem::create_directories(root);
    out::clear_output_files(root);
    const auto dest=root/"part-0.parquet";
    out::WorkerUserOutput writer;
    std::vector<u32> cat_off{0};std::vector<u8> cat_names;
    for(unsigned i=0;i<32;++i) {
        const auto s="分类_"+std::to_string(i)+std::string(i%3*50,'q');
        cat_names.insert(cat_names.end(),s.begin(),s.end());cat_off.push_back(u32(cat_names.size()));
    }
    const pqw::StrCol cats{cat_off,cat_names};
    bool discard=argc>2;
    const std::array<std::size_t,19> sizes={0,1,7,8,15,16,17,63,64,65,4095,4096,4097,6143,6144,6145,10241,12288,13817};
    for(unsigned c=0;c<sizes.size();++c) {
        const auto n=sizes[c];
        std::vector<u32> k(n);std::vector<i32>d(n),s(n),e(n),r(n);std::vector<u8> cat(n);
        std::vector<u32> off{0};std::vector<u8> names;
        for(std::size_t j=0;j<n/67+22;++j) {
            const auto name="客户_"+std::to_string(c)+"_"+std::string(j%21,'0')+std::to_string(j);
            names.insert(names.end(),name.begin(),name.end());off.push_back(u32(names.size()));
        }
        for(std::size_t i=0;i<n;++i) {
            k[i]=u32(19+i/67);d[i]=i32(19000+i%67);
            s[i]=c%3==0?1:i32(1+i%173);
            e[i]=c%4==0?i32(2147483000ULL-i%123):i32(i%153+1);
            const auto mode=c%4;
            const bool nonnull=mode==1 || (mode==2&&i%17<8)||(mode==3&&i%257==0);
            const bool nonzero=mode==1 || (mode==2&&i%17==7)||(mode==3&&i%1037>532);
            cat[i]=nonnull?u8(i%32):UINT8_MAX;
            r[i]=nonzero?i32(2147483627ULL-i%312):0;
        }
        V v{k.data(),d.data(),s.data(),e.data(),r.data(),cat.data(),names.data(),names.size(),off.data(),off.size()};
        writer.add(v,0,n,dest,cats);
        if(discard && c==14) return 0;
    }
    writer.finish();
    std::cout<<"wrote "<<dest<<"\n";
}
