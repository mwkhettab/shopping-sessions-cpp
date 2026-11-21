#include "input.hpp"
#include "epoch_prefix_cache.hpp"
#include <cstdio>
#include <fstream>
#include <random>
#include <span>
using ClockSample=std::array<char,13>;
template <unsigned Case> uint64_t pass(std::span<const ClockSample> samples){
  uint64_t sum=0;
  for(auto &bytes:samples){
    auto p=bytes.data();Time v;
    if constexpr(Case==0)v=timestamp({p,13});
    if constexpr(Case==1)v=epoch_with_prefix<5,32,true>(p);
    if constexpr(Case==2)v=epoch_with_prefix<4,4,false>(p);
    if constexpr(Case==3)v=epoch_with_prefix<4,1,true>(p);
    if constexpr(Case==4)v=epoch_with_prefix<5,64,false>(p);
    sum+=uint64_t(v);
  }
  return sum;
}
int main(int argc,char **argv){
  if(argc!=2)return 2;
  std::ifstream stream(argv[1],std::ios::binary|std::ios::ate);
  if(!stream || stream.tellg()<13)return 2;
  size_t len=size_t(stream.tellg());
  if(len%13)return 2;
  std::vector<ClockSample> samples(len/13);
  stream.seekg(0);stream.read(samples[0].data(),len);
  if(!stream)return 2;
  std::array func{pass<0>,pass<1>,pass<2>,pass<3>,pass<4>};
  auto expected=pass<0>(samples);
  std::array<std::vector<double>,5> times;
  for(unsigned round=0;round<9;++round)for(unsigned j=0;j<5;++j){
    unsigned k=(round%2 ? round+5-j : round+j)%5;
    auto begin=std::chrono::steady_clock::now();
    auto value=func[k](samples);
    auto end=std::chrono::steady_clock::now();
    if(value!=expected)std::abort();
    if(round>=2)times[k].push_back(std::chrono::duration<double,std::nano>(end-begin).count()/samples.size());
  }
  std::printf("%zu clocks, checksum %llu\n",samples.size(),(unsigned long long)expected);
  for(size_t k=0;k<func.size();++k){
    std::sort(times[k].begin(),times[k].end());
    std::printf("decoder %zu median %.3f ns/clock, range %.3f to %.3f\n",k,times[k][3],times[k][0],times[k].back());
  }
}
