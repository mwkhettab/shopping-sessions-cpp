#include "input.hpp"
#include <cstdio>
#include <random>

#ifdef SHOPPING_X86_CSV
__attribute__((target("avx2,bmi,bmi2,popcnt,pclmul,avx512f,avx512bw")))
void check(uint64_t sep,uint64_t close,unsigned base) {
  std::array<uint16_t,64> wanted{};
  unsigned count=0;
  for(unsigned j=0;j<64;++j)
    if(sep>>j&1)wanted[count++]=uint16_t(2*(base+j+1)+((close>>j)&1));
  auto exercise=[&](auto emit){
    std::array<uint16_t,129> got;got.fill(0xdead);
    emit(got.data()+1,base,sep,close);
    if(!std::equal(wanted.begin(),wanted.begin()+count,got.begin()+1) ||
       got.front()!=0xdead || got.back()!=0xdead)std::abort();
  };
  exercise(marked_dwords);exercise(marked_by_rank<false>);
  exercise(marked_by_rank<true>);exercise(marked_eight);
}
#endif
int main(){
#ifdef SHOPPING_X86_CSV
  if(!wide_csv_supported() || !__builtin_cpu_supports("bmi2") || !__builtin_cpu_supports("pclmul") ||
     !__builtin_cpu_supports("avx512f") || !__builtin_cpu_supports("avx512bw"))return 0;
  std::mt19937_64 rng(867421);
  for(unsigned i=0;i<65536;++i) {
    for(unsigned half=0;half<4;++half)check(uint64_t(i)<<(16*half),rng(),64*(rng()%64));
  }
  for(unsigned i=0;i<180000;++i) {
    uint64_t mash=rng();unsigned n=i%9;
    for(unsigned j=0;j<n;++j)mash&=rng();
    if(i%7==0)mash|=~uint64_t{}<<(i%64);
    check(mash,rng(),64*(rng()%64));
  }
  std::puts("442144 flagged delimiter masks compared with scalar ordered positions under all four kernels");
#endif
}
