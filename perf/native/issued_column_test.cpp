#include "input.hpp"
#include "partition_slab.hpp"
#include <cassert>
#include <cstdio>
struct RevisionIndex {Key id;uint32_t ingest,location;};
#include <rev_columns.hpp>

RevisionIndex expected(unsigned index,unsigned shard) {
  return {{uintptr_t(2*(index+1u)+2*shard)},index ^ (shard<<16),uint32_t(index*19+shard)};
}
template<size_t Width> size_t verify(unsigned hint) {
  RevisionColumns<Width> columns;
  columns.reserve(hint);
  using Slot=typename RevisionColumns<Width>::Bin::Slot;
  struct Delayed {Slot slot{};RevisionIndex value{};void finish(){if(slot)slot.put(value);}};
  std::array<Delayed,32> pending{};
  size_t cursor=0;
  constexpr unsigned tested_shards=17;
  size_t rows=columns[0].limit+3*Width+55;
  for(unsigned row=0;row<rows;++row)for(unsigned shard=0;shard<tested_shards;++shard){
    auto &bin=columns[shard];auto &d=pending[cursor++%pending.size()];
    d.finish();auto value=expected(row,shard);
    d.slot=bin.claim_initial_slot();
    assert(bool(d.slot)==(row<bin.limit));
    if(d.slot)d.value=value;else bin.push_back(value);
  }
  for(auto &d:pending)d.finish();
  for(unsigned shard=0;shard<tested_shards;++shard){
    auto &bin=columns[shard];assert(bin.size()==rows);
    for(unsigned row=0;row<rows;++row){
      auto value=bin[row],want=expected(row,shard);
      assert(bin.key(row).bits==want.id.bits && value.id.bits==want.id.bits);
      assert(value.ingest==want.ingest && value.location==want.location);
    }
  }
  return cursor;
}
int main(){
  auto count=verify<1>(0)+verify<1>(30)+verify<16>(1)+verify<16>(666)+verify<256>(256)+verify<256>(992);
  std::printf("%zu delayed revision slots matched before and after growth, including unmapped initial storage.\n",count);
}
