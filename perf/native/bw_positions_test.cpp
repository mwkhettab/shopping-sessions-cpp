#include "input.hpp"
#include <cstdio>
#include <random>

#ifdef SHOPPING_X86_CSV
__attribute__((target("avx2,bmi,bmi2,popcnt,pclmul,avx512f,avx512bw"))) size_t test_positions(){
  std::mt19937_64 rng(4223811);
  size_t tests=0;
  auto check=[&](uint64_t mask,unsigned origin) __attribute__((target("avx2,bmi,bmi2,popcnt,pclmul,avx512f,avx512bw"))) {
    std::array<uint8_t,144> expected,found;
    expected.fill(231);
    unsigned n=0;
    for(unsigned j=0;j<64;++j)if(mask&(uint64_t(1)<<j))expected[8+n++]=uint8_t(origin+j);
    for(unsigned mode=0;mode<3;++mode){
      found.fill(231);auto *p=found.data()+8;
      if(!mode)bw_four_groups(p,origin,mask);
      else if(mode==1)bw_nibbles(p,origin,mask);
      else bw_eight_bytes(p,origin,mask);
      if(std::memcmp(expected.data(),found.data(),8+n)!=0)std::abort();
      for(unsigned j=8+n+16;j<found.size();++j)if(found[j]!=231)std::abort();
      ++tests;
    }
  };
  for(unsigned q=0;q<65536;++q)for(unsigned band=0;band<4;++band)check(uint64_t(q)<<(16*band),64*((q+band)%4));
  for(unsigned i=0;i<160000;++i){
    uint64_t m=rng();
    if(i%3==1)m&=rng()&rng()&rng();
    if(i%3==2)m=~m;
    check(m,(i%4)*64);
  }
  return tests;
}
#endif
int main(){
#ifdef SHOPPING_X86_CSV
  if(wide_csv_supported() && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") &&
     __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("pclmul")){
    std::printf("%zu ordered separator comparisons executed using only AVX-512F/BW, AVX2 and BMI2/PCLMUL\n",test_positions());
  }else std::puts("The current CPU does not advertise AVX-512F/BW, exercising this path on native Intel is pending.");
#endif
}
