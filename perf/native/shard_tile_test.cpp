#include "input.hpp"
#include "event_layout.hpp"
#include <cassert>
#include <tuple>
#include <cstdio>
struct RevisionIndex {Key id;uint32_t ingest,location;};
#include <shard_tile.hpp>

template<class E> struct TileWorker {
  std::array<std::vector<std::pair<Key,E>>,shards> events;
  std::array<std::vector<RevisionIndex>,shards> candidates;
  struct Extra {Time seconds;uint64_t location;};
  std::vector<Extra> extended;
};

template<unsigned Capacity,unsigned Bands,bool Counts,class Layout>
size_t verify(Layout layout){
  using E=typename Layout::E;
  using Row=std::tuple<uint64_t,Time,uint64_t,Time,int32_t,int32_t>;
  ShardTile<E,Capacity,Bands,Counts> tile;
  TileWorker<E> worker;
  std::vector<Row> values,expected;
  constexpr Time anchor=2208988800LL;
  for(unsigned i=0;i<17003;++i){
    Key id{uintptr_t(2+2*(i%91))},owner{i%17?uintptr_t(2+2*((i*911u)%683)):0};
    Event e{anchor*1000+(i*1789u)%82411,int32_t(i%359),int32_t(i%5)};
    Time seconds=i%11?(anchor+i/2):(i&1?-(Time(1)<<34):(Time(1)<<34));
    tile.add(worker,layout,id,owner,e,seconds*1000,anchor);
    expected.emplace_back(id.bits,seconds,owner.bits,owner.bits?e.ts:0,owner.bits?e.price:0,owner.bits?e.category:0);
    if(i%301==44 || i%623==0)tile.flush(worker,anchor);
  }
  tile.flush(worker,anchor);
  std::array<std::vector<char>,shards> seen;
  for(size_t i=0;i<shards;++i)seen[i].resize(worker.events[i].size());
  for(size_t b=0;b<shards;++b)for(auto &r:worker.candidates[b]){
    assert(b==r.id.hash()%shards);
    Time sec;uint64_t loc=r.location;
    if(r.ingest&0x80000000u){auto x=worker.extended.at(loc);sec=x.seconds;loc=x.location;}
    else sec=anchor+Time(r.ingest)-(Time(1)<<30);
    if(!loc){values.emplace_back(r.id.bits,sec,0,0,0,0);continue;}
    --loc;auto s=loc%shards,n=loc/shards;
    assert(n<seen[s].size() && !seen[s][n]);seen[s][n]=1;
    auto [u,payload]=worker.events[s][n];auto e=layout.unpack(payload);
    assert(s==u.hash()%shards);
    values.emplace_back(r.id.bits,sec,u.bits,e.ts,e.price,e.category);
  }
  for(auto &bucket:seen)assert(std::ranges::find(bucket,0)==bucket.end());
  std::sort(expected.begin(),expected.end());std::sort(values.begin(),values.end());
  assert(expected==values);
  return values.size();
}
int main(){
  auto n=verify<7,1,false>(WideLayout{})+verify<512,8,false>(WordLayout{2208988800000})+
    verify<2048,16,false>(WideLayout{})+verify<4096,32,false>(WordLayout{2208988800000})+
    verify<2048,16,true>(WideLayout{})+verify<4096,512,true>(WordLayout{2208988800000});
  std::printf("%zu revisions and their raw locations matched under all bounded partition orders.\n",n);
}
