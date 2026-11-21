#include "input.hpp"
#include <cstdio>
#include <random>

#ifdef SHOPPING_X86_CSV
void compare(Str input,bool wide) {
  std::vector<std::array<Str,7>> expected;
  scalar_rows<7>(input,[&](const auto &r){expected.push_back(r);});
  auto check=[&]<unsigned Bytes>() {
    size_t index=0;
    auto verify=[&](const auto &row){
      if(index>=expected.size())std::abort();
      for(unsigned col=0;col<7;++col)if(row[col]!=expected[index][col])std::abort();
      ++index;
    };
    if(wide)ordinal16_wide<7,Bytes,false>(input,verify);
    else ordinal16_standard<7,Bytes,false>(input,verify);
    if(index!=expected.size())std::abort();
  };
  check.template operator()<512>();check.template operator()<1024>();check.template operator()<2048>();
}
#endif

int main(){
#ifdef SHOPPING_X86_CSV
  if(!wide_csv_supported() || !__builtin_cpu_supports("bmi2") || !__builtin_cpu_supports("pclmul"))return 77;
  bool wide=__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vbmi2");
  std::mt19937 rng(5881693);
  size_t checked=0;
  for(unsigned k=0;k<6144;++k){
    size_t before=k%2053;
    std::string data=(k%5==0?"\"":"")+std::string(before,'x');
    if(k%5==0)data+='"';
    data+=",,,,,,\n";
    for(unsigned i=0;i<45;++i){
      unsigned fields=rng()%9?7:rng()%13;
      for(unsigned col=0;col<fields;++col){
        if(col)data+=',';
        bool quote=rng()%3==0;
        if(quote)data+='"';
        char c=std::array<char,5>{'0','v','\0','\r',char(0xff)}[rng()%5];
        data.append(rng()%15?unsigned(rng()%15):unsigned(rng()%560),c);
        if(quote)data+='"';
      }
      if(i<44 || k%2)data+='\n';
    }
    if(k%7==0)data.insert(rng()%(data.size()+1),std::string(80+rng()%128,','));
    if(k%9==0)data.insert(rng()%(data.size()+1),std::string(80+rng()%66,'\n'));
    if(k%13==0)data[rng()%data.size()]='"';
    compare(data,false);++checked;
    if(wide){compare(data,true);++checked;}
  }
  std::printf("%zu complete dialect comparisons at 512, 1024 and 2048 bytes, AVX512VBMI2 exercised %d.\n",checked,int(wide));
#endif
}
