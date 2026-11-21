#include "input.hpp"
#include <cstdio>
#include <random>

#ifdef SHOPPING_X86_CSV
__attribute__((target("avx2,bmi,bmi2,popcnt,pclmul"))) size_t test_sparse(){
  std::mt19937_64 random(530921);
  size_t checks=0;
  auto check=[&](uint64_t marks,unsigned base) __attribute__((target("avx2,bmi,bmi2,popcnt,pclmul"))) {
    std::array<uint8_t,96> expected{},observed{};
    for(auto &c:expected)c=211;
    unsigned n=0;
    for(unsigned bit=0;bit<64;++bit)if(marks&(uint64_t{1}<<bit))expected[4+n++]=uint8_t(base+bit);
    for(unsigned method=0;method<3;++method){
      observed.fill(211);
      if(!method)transpose_positions(observed.data()+4,base,marks);
      else if(method==1)deposited_positions(observed.data()+4,base,marks);
      else shuffled_positions(observed.data()+4,base,marks);
      if(std::memcmp(expected.data(),observed.data(),4+n))std::abort();
      if(!std::all_of(observed.begin()+4+n+16,observed.end(),[](auto c){return c==211;}))std::abort();
      ++checks;
    }
  };
  for(unsigned m=0;m<65536;++m)for(unsigned q=0;q<4;++q)check(uint64_t(m)<<(16*q),((q+m)%4)*64);
  for(unsigned i=0;i<200000;++i){
    uint64_t v=random();
    if(i%5==1)v&=random()&random()&random();
    if(i%5==2){v=0;for(unsigned j=0;j<i%12;++j)v|=uint64_t{1}<<(random()&63);}
    if(i%5==3){v=0;for(unsigned j=0;j<16;++j)if(random()&1)v|=uint64_t{1}<<(j*4+(random()&3));}
    if(i%5==4)v=~v;
    check(v,(i%4)*64);
  }
  return checks;
}
#endif
int main(){
#ifdef SHOPPING_X86_CSV
  if(wide_csv_supported() && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("pclmul"))
    std::printf("%zu exact position and buffer-bound comparisons on the SSE/BMI2 emitters\n",test_sparse());
#endif
}
