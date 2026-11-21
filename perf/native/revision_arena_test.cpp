#include "input.hpp"
#include <partition_slab.hpp>
#include <cstdio>
#include <cstdlib>
#include <random>
struct RevisionIndex{Key id;uint32_t ingest,location;};
#include <revision_arena.hpp>
#define SHOP_REUSE_REGION 1
#define SHOP_LEND_REGION 1
#include <timeline_reuse.hpp>
template<unsigned Width,unsigned Band,bool Dynamic> void check(size_t reserve,unsigned shape) {
  ArenaRevisionColumns<Width,Band,Dynamic> data;data.reserve(reserve);
  std::array<std::vector<RevisionIndex>,shards> ref;
  std::mt19937_64 rng(7129+shape);
  for(size_t i=0;i<77000;++i){
    size_t b=shape?(i%7?42:rng()%shards):rng()%shards;
    RevisionIndex row{Key{uintptr_t(rng())&~uintptr_t(1)},uint32_t(rng()),uint32_t(rng())};
    data[b].push_back(row);ref[b].push_back(row);
    if(i%21==0){size_t n=rng()%ref[b].size();if(data[b].key(n).bits!=ref[b][n].id.bits)std::abort();}
  }
  for(size_t b=0;b<shards;++b){auto &src=data[b];if(src.size()!=ref[b].size())std::abort();
    for(size_t j=0;j<src.size();++j){
      auto row=src[j],expected=ref[b][j];if(row.id.bits!=expected.id.bits || row.location!=expected.location || row.ingest!=expected.ingest)std::abort();
    }src.release();
  }
  std::array<TimelineRegion,1> pool;auto &r=data.slab;
  if(r.region!=MAP_FAILED){pool[0].base=static_cast<std::byte*>(r.region);pool[0].bytes=Dynamic?r.used:r.bytes;}
  std::vector<TimelineArray<uint64_t>> vectors(139);
  for(size_t i=0;i<vectors.size();++i){auto &v=vectors[i];v.prepare(i*i/4,pool,0);size_t j=0;for(auto &entry:v.view()){entry=uint64_t(i)<<32|j;++j;}}
  for(size_t i=0;i<vectors.size();++i){size_t j=0;for(auto &entry:vectors[i].view()){if(entry!=(uint64_t(i)<<32|j))std::abort();++j;}}
}
int main(){
  for(size_t reserve:{0u,1u,37u,1024u})for(unsigned shape:{0u,1u}){
    check<64,1,true>(reserve,shape);check<256,1,true>(reserve,shape);check<1024,1,true>(reserve,shape);
    check<64,1,false>(reserve,shape);check<256,1,false>(reserve,shape);check<256,4,false>(reserve,shape);
  }
  std::puts("3696000 revisions and all timeline values survived empty reservations, uneven shard growth, overflow blocks and repurposed pages.");
}
