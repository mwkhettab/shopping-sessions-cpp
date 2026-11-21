#include "event_layout.hpp"
#include <span>
#include <cstdio>
#include <random>
#include <tuple>
constexpr Time session_gap=1800000;
#include "small_event_network.hpp"

struct Trace {
  std::vector<Time> starts;
  std::vector<std::pair<int,int>> purchases;
  std::vector<std::tuple<Time,size_t,int64_t>> sessions;
  bool operator==(const Trace &) const=default;
};
int main(){
  std::mt19937_64 gen(8051703);
  WordLayout layout{-2500000000LL};
  size_t checks=0,fast=0;
  auto verify=[&](std::span<const uint64_t> input){
    std::array<uint64_t,68> buffer{};
    buffer.fill(0x1a2b3c4dULL);
    auto values=std::span(buffer).subspan(2,input.size());
    std::copy(input.begin(),input.end(),values.begin());
    auto order=std::vector<uint64_t>(input.begin(),input.end());
    std::sort(order.begin(),order.end());
    if(short_event_sort<64>(values))++fast;
    else std::sort(values.begin(),values.end());
    if(!std::equal(values.begin(),values.end(),order.begin(),order.end()))std::abort();
    if(buffer[0]!=0x1a2b3c4d || buffer[1]!=0x1a2b3c4d || buffer[2+values.size()]!=0x1a2b3c4d)std::abort();
    Trace expected;
    size_t start=0;int64_t money=0;
    for(size_t i=0;i<order.size();++i){
      auto e=layout.unpack(order[i]);
      if(!i || e.ts-layout.time(order[i-1])>session_gap){
        if(i)expected.sessions.emplace_back(layout.time(order[start]),i-start,money);
        expected.starts.push_back(e.ts);start=i;money=0;
      }
      if(e.price){expected.purchases.emplace_back(e.category,e.price);money+=e.price;}
    }
    expected.sessions.emplace_back(layout.time(order[start]),order.size()-start,money);
    auto test=[&]<bool Vectors>(){
      Trace got;
      scan_marked_events<Vectors>(values,layout,[&](Time t){got.starts.push_back(t);},
        [&](int cat,int price){got.purchases.emplace_back(cat,price);},
        [&](Time t,size_t n,int64_t rev){got.sessions.emplace_back(t,n,rev);});
      if(!(got==expected))std::abort();
    };
    test.template operator()<false>();test.template operator()<true>();
    ++checks;
  };
  std::array<uint64_t,64> numbers{};
  for(unsigned pattern=0;pattern<65536;++pattern){
    for(unsigned i=0;i<16;++i)numbers[i]=(pattern&(1u<<i))?UINT64_MAX:0;
    verify(std::span(numbers).first(16));
  }
  for(unsigned k=0;k<50000;++k){
    unsigned n=k%64+1;
    uint64_t time=gen()%2000;
    for(unsigned i=0;i<n;++i){
      time+=std::array<uint64_t,6>{0,1,1800000,1800001,1799999,13000000}[gen()%6];
      uint64_t pay=(gen()%3==0)?(1+gen()%10000000)*256+(gen()&255):0;
      numbers[i]=k%5==0?gen():((time<<32)|pay);
    }
    std::shuffle(numbers.begin(),numbers.begin()+n,gen);
    verify(std::span(numbers).first(n));
  }
  std::printf("%zu sorting and session traces checked, %zu arrays used native AVX-512F sorting\n",checks,fast);
}
