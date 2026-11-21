#include "input.hpp"
#include <cstdio>
#include <random>
int main(){
  std::mt19937_64 gen(501198);
  std::array<char,13> buf{};
  size_t checks=0;
  auto check=[&] {
    Time want=0;
    for(unsigned char c:buf) {
      if(c<'0' || c>'9'){want=bad;break;}
      want=10*want+c-'0';
    }
    for(auto got:{epoch_with_prefix<5,32,true>(buf.data()),epoch_with_prefix<4,4,false>(buf.data()),
                  epoch_with_prefix<4,1,true>(buf.data()),epoch_with_prefix<5,64,false>(buf.data())})
      if(got!=want)std::abort();
    ++checks;
  };
  check();
  for(unsigned i=0;i<400000;++i){
    for(unsigned j=0;j<13;++j)buf[j]=char('0'+gen()%10);
    if(i%9==0)std::fill_n(buf.begin(),5,'0');
    if(i%3==0)buf[i%13]=char(gen()%256);
    check();
    if(i%8==0){
      buf[12]=char('0'+gen()%10);check();
      buf[4]=char('0'+gen()%10);check();
    }
  }
  for(unsigned pos=0;pos<13;++pos)for(unsigned c=0;c<256;++c){
    buf.fill('9');buf[pos]=static_cast<char>(c);check();
  }
  std::printf("%zu thirteen-byte clocks matched the scalar parser under every cache policy.\n",checks);
}
