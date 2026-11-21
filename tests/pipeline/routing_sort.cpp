#include "links_sort.hpp"
#include <cassert>
#include <cstdio>
#include <random>
#include <string>
using namespace pipeline;
struct SortModel {
    u64 clen=310000000;
    void id_str(u64 t,u8 role,std::vector<u8>& b)const {
        b.clear();b.push_back(role);if(t&RAW){const auto s=std::to_string(t&(RAW-1)); b.push_back('-');b.insert(b.end(),s.begin(),s.end());}
        else {const auto [v,n]=enc36(t);for(std::size_t i=0;i<n;++i)b.push_back(u8(v>>(56-8*i)));}
    }
    links::Click* csr{};
};
int main(){
    SortModel g;links::Scratch sc;std::mt19937_64 rng(298337);
    std::vector<u32> lens{0,1,2,2,3,4,5,7,8,9,12,16,17,22,32,33,37,63,64,65,89,127,128,129,233,412,511,512,513,1000,2028,2047,2048,2049,3742};
    std::vector<u32> offsets{0};std::vector<links::Click> in,baseline;
    for(auto n:lens){for(u32 i=0;i<n;++i){u64 ts=rng()%std::max<u32>(2,n/4);if(n%2)ts*=0x4321;u64 token=(i+13);if(n%3==0&&n>3&&i%17==0)token|=RAW;const auto camp=(i+1)%16000;in.push_back({(ts<<32)|(u64(camp)<<4)|(i%13),token});}offsets.push_back(u32(in.size()));}
    baseline=in;
    for(std::size_t i=0;i<lens.size();++i){auto begin=baseline.begin()+offsets[i],end=baseline.begin()+offsets[i+1];std::stable_sort(begin,end,[&](const auto&a,const auto&b){if((a[0]>>32)!=(b[0]>>32))return (a[0]>>32)<(b[0]>>32);return events::cid_cmp(g,a[1],b[1])>0;});}
    auto selected=in;g.csr=selected.data();links::sort_users(g,offsets.data(),0,lens.size(),sc);assert(selected==baseline);
    std::vector<links::Click> out(in.size());for(std::size_t i=0;i<lens.size();++i){links::sort_from(g,in.data()+offsets[i],std::span(out).subspan(offsets[i],lens[i]),sc);}assert(out==baseline);
    std::puts("click networks, comparison ties, recursive splits and four-digit radix outputs passed");
}
