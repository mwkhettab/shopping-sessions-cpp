#include "input.hpp"
#include <cstdio>
#include <random>

void check(Str data){
  std::vector<std::array<Str,4>> expected;
  scalar_rows<4>(data,[&](auto &r){expected.push_back(r);});
  size_t index=0;
  rows<4>(data,[&](auto &r){
    if(index>=expected.size())std::abort();
    for(size_t i=0;i<4;++i)if(r[i]!=expected[index][i])std::abort();
    ++index;
  });
  if(index!=expected.size())std::abort();
}

int main(){
  std::mt19937 random(406889);
  std::string input;
  const std::string payloads[]={"","k12345678","u1234567","c_0","c_01","2030-02-28T00:00:00Z",std::string("ab\0cd",5),"a\r","é"};
  for(unsigned n=0;n<180000;++n){
    std::string line;
    for(unsigned i=0;i<4;++i){
      if(i)line+=',';
      bool q=(n>>i)&1;
      if(q)line+='"';
      if(n%5==0 && i==(n/5)%4)line.append((n/32)%263,'x');
      else line+=payloads[random()%std::size(payloads)];
      if(q)line+='"';
    }
    if(n%3==0){size_t pos=random()%line.size();line[pos]=std::array<char,6>{'"',',','\n','\r','\0',char(0xfe)}[random()%6];}
    if(n%7==0)line.insert(random()%line.size(),1,'"');
    if(n%11==0)line+=',';
    input+=line;if(n%97)input+='\n';
    if(n%359==358){check(input);input.clear();}
  }
  check(input);
  std::puts("180,000 click rows matched scalar field bytes across every quote mask, malformed input, wide fields and missing terminal LF.");
}
