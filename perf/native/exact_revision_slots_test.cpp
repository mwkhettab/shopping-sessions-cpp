#include "input.hpp"
#include <grouped_control.hpp>
#include <exact_revision_slots.hpp>
#include <cstdio>
#include <random>
#include <unordered_map>

template<unsigned Bits,unsigned Style> void check(){
  using Index=ExactRevisionSlots<Bits,Style>;
  std::mt19937_64 rng(44113+Bits);
  size_t visited=0;
  for(unsigned size:{16u,64u,1024u,16384u}){
    Index slots(size);std::unordered_map<uint64_t,uint64_t> expected;
    std::vector<uint64_t> keys;
    for(size_t i=0;i<9*size/16;++i){
      uint64_t q=i==0?0:((rng()&((uint64_t{1}<<(Bits*8-9))-1))&~uint64_t(31))|(i%5);
      uint64_t hash=q*shards+443;
      auto [at,found]=slots.find(hash);
      if(found!=expected.contains(hash))std::abort();
      if(!found)keys.push_back(hash);
      uint64_t value=(rng()%(Index::location_mask-1))+1;
      slots.entries[at]=(q<<Index::location_bits)|value;expected[hash]=value;
    }
    for(unsigned n=0;n<40000;++n){
      uint64_t hash=keys[rng()%keys.size()];
      auto [i,found]=slots.find(hash);
      if(!found || (slots.entries[i]&Index::location_mask)!=expected[hash])std::abort();
      if(n%7==0){
        auto newvalue=Index::location_mask-n%401;
        slots.entries[i]=((hash/shards)<<Index::location_bits)|newvalue;
        expected[hash]=newvalue;
      }
      ++visited;
    }
  }
  std::printf("Complete %u-bit-symbol keys, style %u, %zu independent lookups and updates passed.\n",Bits,Style,visited);
}
int main(){check<6,0>();check<6,1>();check<6,2>();check<7,0>();check<7,1>();check<7,2>();}
