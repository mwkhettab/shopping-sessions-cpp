#include "input.hpp"
#include <partition_slab.hpp>
#include <packed_event_id.hpp>
#include <packed_revision12.hpp>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <unordered_map>

uint64_t reference_projection(uint64_t data,uint64_t mask){
  uint64_t result=0;
  for(unsigned from=0,to=0;from<64;++from)if((mask>>from)&1)result|=((data>>from)&1)<<to++;
  return result;
}

template<unsigned Bits> void check(){
  using Fast=EventIdPlane<Bits,true>;
  using Soft=EventIdPlane<Bits,false>;
  using Revision=PlaneRevision<Bits>;
  std::mt19937_64 rng(804461+Bits);
  std::unordered_map<uintptr_t,std::string> spellings;
  size_t elapsed=0,compact=0,escaped=0;
  std::vector<FullPlaneRevision> overflow;
  KeyPool a,b;
  for(unsigned round=0;round<220000;++round){
    uint64_t raw=rng();
    if(Soft::project(raw)!=reference_projection(raw,Soft::selected) || Fast::project(raw)!=Soft::project(raw))std::abort();
    unsigned width=round%12;
    std::string id(1,round%19?'e':'E');
    for(unsigned j=0;j<width;++j){
      uint8_t c=uint8_t(rng());
      if(round%5)c=uint8_t((c&uint8_t(Soft::selected))|uint8_t(Soft::fixed));
      id+=char(c);
    }
    if(width && round%17==0)id.back()=Bits==6?' ':'\0';
    Key x=Fast::encode(id,a),y=Soft::encode(id,b);
    bool qualifies=id.front()=='e' && width>=1 && width<=8 && (uint8_t(id.back())&uint8_t(Soft::selected));
    for(unsigned j=1;j<id.size();++j)qualifies&=((uint8_t(id[j])^uint8_t(Soft::fixed))&uint8_t(~Soft::selected))==0;
    if(x.indirect()==qualifies || !(x==y) || x.hash()!=y.hash())std::abort();
    if(!x.indirect()){
      auto [it,added]=spellings.try_emplace(x.bits,id);
      if(!x.bits || (!added && it->second!=id))std::abort();
      ++compact;
    }else if(x.view()!=id)std::abort();
    int64_t anchor=round%2 ? 1707000000:-132000000;
    int64_t delta=std::array<int64_t,8>{-(1<<23)-1,-(1<<23),-(1<<23)+1,-1,0,(1<<23)-2,(1<<23)-1,1<<23}[round%8];
    uint64_t pos=std::array<uint64_t,7>{0,1,Revision::raw_limit-1,Revision::raw_limit,Revision::raw_limit+1,uint32_t(rng()),UINT64_MAX}[round%7];
    auto r=Revision::make(x,anchor+delta,pos,anchor,overflow);
    bool expect_ext=x.indirect() || delta<-(1<<23) || delta>=(1<<23) || pos>=Revision::raw_limit;
    if(r.external()!=expect_ext)std::abort();
    if(expect_ext){
      auto &full=overflow[r.extension()];
      if(!(full.id==x) || full.seconds!=anchor+delta || full.location!=pos)std::abort();
      overflow.clear();++escaped;
    }else if(r.code(x.hash()%shards)!=x.hash() || r.location()!=pos || int64_t(r.time_bits())-(1<<23)!=delta)std::abort();
    a.other.clear();b.other.clear();
    ++elapsed;
  }
  // Fields may be physically unaligned because each record has three words.
  for(size_t hint:{0u,1u,1024u}){
    PlaneColumns<Revision,64> columns;
    columns.reserve(hint);
    std::array<std::vector<Revision>,shards> ref;
    for(unsigned n=0;n<145000;++n){
      size_t slot=n%3 ? n%11:rng()%shards;
      Revision r{uint32_t(rng()),uint32_t(rng()),uint32_t(rng())};
      columns[slot].push_back(r);ref[slot].push_back(r);
    }
    for(size_t b=0;b<shards;++b)for(size_t i=0;i<ref[b].size();++i){
      auto got=columns[b][i];const auto &want=ref[b][i];
      if(got.low!=want.low || got.middle!=want.middle || got.last!=want.last)std::abort();
    }
  }
  std::printf("%u-bit symbols: %zu independent byte projections and ID spellings, %zu compact, %zu escaped revision boundaries and 435000 column locations verified.\n",Bits,elapsed,compact,escaped);
}
int main(){check<6>();check<7>();}
