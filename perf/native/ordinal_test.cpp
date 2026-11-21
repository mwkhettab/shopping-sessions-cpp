#include "input.hpp"
#include <cstdio>
#include <random>

#ifdef SHOPPING_X86_CSV
void field_check(Str text) {
  std::vector<std::array<Str,7>> expected;
  auto add=[&](const auto &r){expected.push_back(r);};
  scalar_rows<7>(text,add);
  for(unsigned mode=0;mode<3;++mode){
    size_t index=0;
    auto compare=[&](const auto &r){
      if(index>=expected.size())std::abort();
      for(unsigned f=0;f<7;++f)if(r[f]!=expected[index][f])std::abort();
      ++index;
    };
    if(mode==0)ordinal_rows<7,false>(text,compare);
    if(mode==1)ordinal_rows<7,true>(text,compare);
    if(mode==2){
      if(!__builtin_cpu_supports("avx512f")||!__builtin_cpu_supports("avx512bw")||!__builtin_cpu_supports("avx512vbmi2"))continue;
      ordinal_wide_rows<7,false>(text,compare);
    }
    if(index!=expected.size())std::abort();
  }
}
#endif

int main(){
#ifdef SHOPPING_X86_CSV
  if(!wide_csv_supported() || !__builtin_cpu_supports("bmi2") || !__builtin_cpu_supports("pclmul"))return 77;
  std::mt19937 rng(527381);
  for(unsigned offset=0;offset<=260;++offset){
    std::string prefix(offset,'x');prefix+=",,,,,,\n";
    for(unsigned sample=0;sample<32;++sample){
      auto text=prefix;
      for(unsigned r=0;r<46;++r){
        unsigned n=rng()%9==0?rng()%12:7;
        for(unsigned f=0;f<n;++f){
          if(f)text+=',';
          bool quoted=rng()%3==0;
          if(quoted)text+='"';
          unsigned width=(rng()%13==0?rng()%260:rng()%12);
          char c=std::array<char,5>{'e','Z','\0','\r',char(0xff)}[rng()%5];
          text.append(width,c);
          if(quoted)text+='"';
        }
        if(r<45||sample%2)text+='\n';
      }
      if(sample%3==0)text[rng()%text.size()]='"';
      if(sample%5==0)text.insert(text.size()/2,std::string(256+sample,','));
      if(sample%7==0)text.insert(text.size()/3,std::string(256+sample,'\n'));
      field_check(text);
    }
  }
  std::puts("8,352 independent tile-boundary and sparse/dense delimiter comparisons passed for all available ordinal decoders.");
#endif
}
