#include "input.hpp"
#include <flat_alias_groups.hpp>
#include <cstdio>
#include <cstdlib>
#include <random>
template<unsigned Probes,bool Remember,class Index> void check(size_t cap){
  FlatAliasGroups<Index,Probes,Remember> groups(cap);KeyPool pool;
  std::deque<std::string> spellings;std::vector<Key> series;
  std::unordered_map<Key,std::array<size_t,3>,KeyHash> expected;
  std::mt19937_64 rng(17145+Probes);
  for(size_t i=0;i<12000;++i){
    uint64_t quotient=1+rng()%7000;
    uint64_t code=((quotient*cap+cap-1)*shards+29)&key_mask;
    if(i%4==0)code=((1+rng()%65530)*shards+29)&key_mask;
    Key key{uintptr_t(code<<1)};
    if(i%11==0){
      auto &text=spellings.emplace_back("arbitrary alias "+std::to_string(i%169));
      key=pool.encode(text,'u');
    }
    series.push_back(key);series.push_back(series[rng()%series.size()]);
  }
  for(size_t i=0;i<series.size();++i){
    auto k=series[i];auto [slot,g]=groups.intern(k);
    auto [it,added]=expected.try_emplace(k,std::array<size_t,3>{slot,0,0});
    if(it->second[0]!=slot || &groups[slot]!=g || !(g->key==k))std::abort();
    if(i%3){++g->event_end;++it->second[1];}else{++g->click_end;++it->second[2];}
  }
  size_t visited=0;
  groups.visit([&](auto &g){
    const auto &v=expected.at(g.key);
    if(g.event_end!=v[1] || g.click_end!=v[2] || g.vertex++)std::abort();
    ++visited;
  });
  if(visited!=expected.size())std::abort();
  for(auto &[k,v]:expected)if(groups[v[0]].vertex!=1)std::abort();
}
int main(){
  for(size_t cap:{1u,4u,32u,512u,4096u}){
    check<4,true,uint32_t>(cap);check<4,false,uint32_t>(cap);check<8,false,uint32_t>(cap);
    check<4,true,size_t>(cap);check<4,false,size_t>(cap);check<8,false,size_t>(cap);
  }
  std::puts("720000 running counts and stable lookup addresses matched independent maps through collisions, growth and table wraparound.");
}
