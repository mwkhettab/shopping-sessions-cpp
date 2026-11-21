#include "input.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>

void compare_all(Str input) {
  std::vector<std::array<Str,7>> expected;
  scalar_rows<7>(input,[&](auto &r){expected.push_back(r);});
  size_t seen=0;
  rows<7>(input,[&](const auto &r){
    if(seen>=expected.size())std::abort();
    for(unsigned i=0;i<7;++i)if(expected[seen][i]!=r[i])std::abort();
    ++seen;
  });
  if(seen!=expected.size())std::abort();
}

int main(){
#ifdef SHOPPING_X86_CSV
  bool selected=wide_csv_supported() && __builtin_cpu_supports("bmi2");
#if SHOP_ROW_VBMI
  selected=selected && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vbmi2");
#endif
  std::printf("Packed row decoder selected %d, VBMI2 variant %d, bit-plane variant %d.\n",int(selected),SHOP_ROW_VBMI,SHOP_ROW_PLANES);
#endif
  unsigned tested=0;std::mt19937 gen(6948799);
  for(unsigned which=0;which<7;++which){
    for(unsigned width=0;width<=143;++width){
      std::string batch;
      for(unsigned mask=0;mask<128;++mask){
        std::string line;
        for(unsigned col=0;col<7;++col){
          if(col)line+=',';
          if(mask>>col&1)line+='"';
          unsigned len=which==col?width:(width+col)%4;
          for(unsigned i=0;i<len;++i)line+=std::array<char,5>{'A','\0','\r',char(0xc3),char(0xa9)}[(i+col)%5];
          if(mask>>col&1)line+='"';
        }
        batch+=line;batch+='\n';++tested;
        for(char special:std::array<char,3>{'"',',','\n'}){
          auto mutated=line;size_t at=gen()%(mutated.size()+1);mutated.insert(at,1,special);
          batch+=mutated;batch+='\n';++tested;
        }
      }
      if(width%2)batch.pop_back();
      compare_all(batch);
    }
  }
  std::printf("%u valid and mutated rows, all 128 quote masks, variable fields crossing both 64-byte halves matched the scalar reader.\n",tested);
}
