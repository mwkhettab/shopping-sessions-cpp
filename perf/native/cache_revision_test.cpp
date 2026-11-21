#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

constexpr size_t shards=1;
struct Key {
  uint64_t bits;
  uint64_t hash() const { return bits; }
};
#include "cache_revision_table.hpp"

struct Row { Key key; unsigned identity, stamp; };
struct Rows {
  std::vector<Row> rows;
  size_t size() const { return rows.size(); }
  Key key(size_t index) const { return rows[index].key; }
};
struct Worker { std::array<Rows,1> candidates; };

template<class Ref> void exercise(bool overflow) {
  std::vector<Worker> workers(8);
  std::vector<unsigned> lowest(128,UINT32_MAX),observed(128,UINT32_MAX);
  std::vector<std::vector<bool>> live(workers.size());
  for(unsigned i=0;i<384;++i) {
    // All keys in a band share their home and fingerprint. Distinct
    // identities have to survive that collision even in the same worker.
    unsigned id=(i*79+13)%128,owner=(i/43)%8;
    unsigned band=overflow?(id<16?0:1):id%4;
    unsigned ts=(i*37+71)%257;
    workers[owner].candidates[0].rows.push_back({Key{uint64_t(band)<<8},id,ts});
    live[owner].push_back(true);lowest[id]=std::min(lowest[id],ts);
  }
  auto at=[&](Ref value)->const Row &{
    uint64_t n=value-1;
    return workers[n&63].candidates[0].rows[n>>6];
  };
  unsigned equalities=0,merges=0;
  auto equal=[&](Ref a,Ref b){++equalities;return at(a).identity==at(b).identity;};
  auto merge=[&](Ref a,Ref b){
    ++merges;bool before=at(b).stamp<at(a).stamp;uint64_t dead=(before?a:b)-1;
    if(!live[dead&63][dead>>6])std::abort();
    live[dead&63][dead>>6]=false;return before;
  };
  bool done=resolve_in_cache<8,Ref>(workers,0,384,equal,merge);
  if(overflow){if(done||equalities||merges)std::abort();return;}
  if(!done || equalities<=merges || merges!=256)std::abort();
  for(unsigned owner=0;owner<workers.size();++owner) {
    const auto &rows=workers[owner].candidates[0].rows;
    for(size_t i=0;i<rows.size();++i)if(live[owner][i]){
      const auto &r=rows[i];
      if(observed[r.identity]!=UINT32_MAX)std::abort();
      observed[r.identity]=r.stamp;
    }
  }
  if(observed!=lowest)std::abort();
}
int main(){
  exercise<uint32_t>(false);exercise<uint32_t>(true);
  exercise<uint64_t>(false);exercise<uint64_t>(true);
  puts("Complete-key ties and late-band fallback matched for both reference widths.");
}
