#include "input.hpp"
#include <cassert>
#include <span>
#define SHOP_REUSE_REGION 1
#define SHOP_LEND_REGION 1
#include <timeline_reuse.hpp>

struct Cell { uint64_t first,second; };
int main(){
  alignas(64) std::array<std::array<std::byte,4096>,4> backing;
  std::array<TimelineRegion,8> regions;
  for(unsigned i=0;i<4;++i){regions[i].base=backing[i].data();regions[i].bytes=backing[i].size();}
  constexpr unsigned per_thread=301;
  std::array<std::array<TimelineArray<Cell>,per_thread>,8> arrays;
  std::vector<std::jthread> threads;
  for(unsigned t=0;t<arrays.size();++t)threads.emplace_back([&,t]{
    for(unsigned i=0;i<per_thread;++i){
      size_t n=i==per_thread-1?300:(i+t)%18;
      auto &row=arrays[t][i];row.prepare(n,regions,t);
      for(unsigned j=0;j<n;++j)row[j]={t*per_thread+i,j};
    }
  });
  threads.clear();
  size_t borrowed=0,fallback=0;
  for(unsigned t=0;t<arrays.size();++t)for(unsigned i=0;i<per_thread;++i){
    auto &row=arrays[t][i];unsigned j=0;
    for(auto &cell:row.view()){assert(cell.first==t*per_thread+i && cell.second==j);++j;}
    if(row.owned)fallback+=j;else borrowed+=j;
  }
  assert(borrowed && fallback);
  std::printf("%zu resident and %zu fallback cells retained every value across eight allocating threads.\n",borrowed,fallback);
}
