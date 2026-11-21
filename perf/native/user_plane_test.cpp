#include "input.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>

int main(){
  std::mt19937_64 random(7514321);
  size_t encoded=0,total=0;
  for(unsigned width=0;width<12;++width)for(unsigned last=0;last<256;++last)for(unsigned case_id=0;case_id<51;++case_id){
    std::string a(1,'u'),b(1,'u');
    for(unsigned i=0;i<width;++i){
      unsigned value=unsigned(random())&255;
      if(case_id%5)value=(value&0x5f)|0x20;
      a+=char(i+1==width?last:value);
    }
    b=a;
    if(case_id%3==0 && !b.empty())b.resize(random()%(b.size()+1));
    else if(case_id%3==1)b+=char(random()%256);
    else b[random()%b.size()]=char(random()%256);
    if(case_id%7==0)a[0]='U';
    KeyPool pa,pb;
    Key x=pa.encode(a,'u'),same=pb.encode(a,'u'),y=pb.encode(b,'u');
    std::array<char,9> left,right;
    if(x.user_text(left)!=a || y.user_text(right)!=b || !(x==same) || x.hash()!=same.hash() ||
       x.user_less(y)!=(Str(a)<Str(b)) || (x==y)!=(a==b))std::abort();
    if(!x.indirect())++encoded;
    std::string k='k'+a;
    auto old=pa.encode(k,'k');
    if(old.user_text(left,'k')!=k)std::abort();
    ++total;
  }
  std::printf("%zu byte spellings and lexical comparisons matched including every possible final byte, %zu reversible short aliases.\n",total,encoded);
}
