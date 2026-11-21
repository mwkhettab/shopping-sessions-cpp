#include "input.hpp"
#include <span>
#define SHOP_REUSE_REGION 1
#define SHOP_LEND_REGION 1
#include "timeline_reuse.hpp"
#include "alias_chain.hpp"
#include <cstdio>
#include <random>

template<unsigned Block,class T> size_t exercise(size_t groups,size_t count,bool resident) {
  using Chain=AliasChain<T,Block>;
  std::vector<Chain> chains(groups);
  std::vector<std::vector<T>> expected(groups);
  std::vector<std::pair<Key,T>> rows(count);
  std::vector<TimelineRegion> regions(4);
  std::vector<uint64_t> backing(resident ? 4096 : 0);
  if(resident){
    regions[2].base=reinterpret_cast<std::byte*>(backing.data());
    regions[2].bytes=backing.size()*sizeof(uint64_t);
  }
  AliasChainWriter<T,Block> writer;
  std::mt19937 rng(897215);
  for(size_t i=0;i<count;++i){
    const size_t group=i%7 ? rng()%groups : 0;
    T value{};
    if constexpr(std::is_integral_v<T>)value=T((uint64_t(rng())<<32)|rng());
    else {value[0]=i;value[1]=rng();value[2]=~uint64_t(value[0]);}
    rows[i]={Key{(group+1)*2},value};
    writer.append(chains[group],rows[i],regions,0);
    expected[group].push_back(value);
  }
  std::vector<T> scratch, joined(count);
  size_t sum=0;
  for(size_t g=0;g<groups;++g){
    auto &chain=chains[g];
    if(chain.count!=expected[g].size())std::abort();
    auto flat=chain.materialize(scratch);
    if(!std::equal(flat.begin(),flat.end(),expected[g].begin(),expected[g].end()))std::abort();
    auto part=std::span(joined).subspan(sum,chain.count);
    chain.copy_to(part);
    if(!std::equal(part.begin(),part.end(),expected[g].begin(),expected[g].end()))std::abort();
    sum+=chain.count;
  }
  if(sum!=count)std::abort();
  return sum;
}
int main(){
  size_t rows=0;
  auto suite=[&]<unsigned B>(){
    for(bool resident:{false,true})for(size_t groups:{size_t(1),size_t(13),size_t(12011)}){
      rows+=exercise<B,uint64_t>(groups,65003,resident);
      rows+=exercise<B,std::array<uint64_t,3>>(groups,11567,resident);
    }
  };
  suite.template operator()<0>();suite.template operator()<4>();
  suite.template operator()<8>();suite.template operator()<16>();
  std::printf("%zu raw and chunked payloads checked in input order with borrowed and owned storage\n",rows);
}
