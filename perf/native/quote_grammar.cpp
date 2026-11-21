#include "input.hpp"
#include <cassert>
#include <random>
#ifdef SHOPPING_X86_CSV
bool grammar(Str s) {
  size_t start=0;
  for(size_t i=0;i<=s.size();++i)if(i==s.size() || s[i]==',' || s[i]=='\n'){
    auto f=s.substr(start,i-start);
    if(f.size()>=2 && f.front()=='"' && f.back()=='"')f=f.substr(1,f.size()-2);
    if(f.find('"')!=Str::npos)return false;
    start=i+1;
  }
  return true;
}
__attribute__((target("avx2,bmi,popcnt,pclmul"))) void verify(Str s) {
  bool expected=grammar(s);
  if(certify_quotes(s.data(),s.size())!=expected)std::abort();
#if __has_include("certified_masks.hpp")
  CertifiedCsvTile<512> test;
  if(s.size()<=512){
    if(test.scan(s)!=expected)std::abort();
    size_t index=0;
    for(size_t i=0;i<s.size();++i){
      if(s[i]=='\n' && (index>=test.lines || test.ends[index++]!=i))std::abort();
      if(bool((test.commas[i/64]>>(i%64))&1)!=(s[i]==','))std::abort();
    }
    if(index!=test.lines)std::abort();
  }
#endif
}
#endif
int main() {
#ifdef SHOPPING_X86_CSV
  if(!wide_csv_supported() || !__builtin_cpu_supports("pclmul"))return 77;
  const char alphabet[]={'a','"',',','\n','\0'};
  for(unsigned offset:{0u,1u,30u,31u,32u,62u,63u,64u,65u,95u,127u}){
    std::string prefix(offset,'u');prefix.push_back(',');
    for(unsigned i=0;i<15625;++i){
      auto v=i;auto s=prefix;
      for(unsigned j=0;j<6;++j){s.push_back(alphabet[v%5]);v/=5;}
      s+="\n";verify(s);
    }
  }
  std::mt19937 rng(40648);
  for(unsigned n=0;n<12000;++n){
    std::string s;
    for(unsigned j=0,k=rng()%45;j<k;++j){
      bool q=rng()%3==0;if(q)s+='"';s.append(rng()%47,'X');if(q)s+='"';s+=rng()%7?',':'\n';
    }
    if(!s.empty() && n%3)s[rng()%s.size()]=alphabet[rng()%5];
    s+='\n';verify(s);
  }
  std::puts("Whole-block quote syntax, every delimiter and line offset matched the independent grammar.");
#endif
}
