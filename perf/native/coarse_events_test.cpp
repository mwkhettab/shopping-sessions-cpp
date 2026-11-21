#include "input.hpp"
#include "partition_slab.hpp"
#include "coarse_events.hpp"
#include <cassert>

template<size_t Parts,bool Indirect>
size_t exercise(size_t hint){
  CoarseEvents<uint64_t,Parts,Indirect> source;
  source.reserve(hint);
  struct Position {size_t band,index;};
  std::vector<Position> all;
  std::array<std::vector<std::pair<uintptr_t,uint64_t>>,shards> expected;
  for(size_t i=0;i<140003;++i){
    uint64_t seed=i%3?0x1234567:19*i+31;
    Key u{uintptr_t(2*(1+permute_key(seed)%((uint64_t(1)<<62)-1)))};
    size_t band=u.hash()%Parts;
    auto &rows=source[band];
    all.push_back({band,rows.size()});
    rows.emplace_back(u,371*i+77);
  }
  for(size_t i=0;i<all.size();++i){
    auto where=all[i];auto &row=source[where.band][where.index];
    assert(row.second==371*i+77);
    if(i%5==0)row.first.bits=0;
    else expected[row.first.hash()%shards].emplace_back(row.first.bits,row.second);
  }
  source.finish();
  parallel(shards,8,[&](size_t b,unsigned){
    assert(source.population(b)==expected[b].size());
    size_t i=0;
    source.each(b,[&](auto &row){
      assert(row.first.bits==expected[b][i].first && row.second==expected[b][i].second);
      row.first.bits=2*i++;
    });
    i=0;
    source.each(b,[&](auto &row){assert(row.first.bits==2*i && row.second==expected[b][i].second);++i;});
    assert(i==expected[b].size());source.release_bin(b);
  });
  source.release();
  return all.size();
}
int main(){
  size_t count=0;
  for(auto hint:{0,2,300}){
    count+=exercise<1,false>(hint)+exercise<1,true>(hint);
    count+=exercise<16,false>(hint)+exercise<16,true>(hint);
    count+=exercise<64,false>(hint)+exercise<64,true>(hint);
  }
  std::cout<<count<<" raw event locators and two concurrent passes per destination verified\n";
}
