#include "event_layout.hpp"
#include "source_clicks.hpp"
#include <climits>
#include <cstdio>
#include <random>

int main(){
  std::array<std::string,4> files;
  struct Row{unsigned file;size_t offset,len;uint32_t ticks;int campaign;};
  std::vector<Row> rows;
  std::mt19937_64 rng(98741636);
  std::array<uint32_t,12> ticks{0,1,999,1000,1001,65536000,2097151000,2097151001,2097152000,UINT32_MAX,1354000,86400000};
  for(unsigned n=0;n<2400;++n){
    unsigned f=n%files.size();
    std::string id;
    if(n%29==0)id="repeat-id";
    else {
      id="k";
      size_t len=n==1168?136000:rng()%35;
      for(size_t i=0;i<len;++i){
        char c=char(rng()%256);
        id+=c==',' || c=='"' || c=='\n' ? 'k' : c;
      }
    }
    auto &file=files[f];
    bool quoted=rng()%2;
    if(quoted)file+='"';
    rows.push_back({f,file.size(),id.size(),ticks[(n/3)%ticks.size()],std::array{0,17,2000,2047,2048,31994}[n%6]});
    file+=id;
    if(quoted)file+='"';
    file+=",x,c_1,1\n";
  }
  std::vector<InputChunk> chunks;
  std::array<size_t,4> first{};
  for(size_t f=0;f<files.size();++f) {
    first[f]=chunks.size();
    for(size_t a=0;a<files[f].size();a+=32768)chunks.push_back({files[f],a,std::min(a+32768,files[f].size())});
  }
  ClickFileDirectory d;
  if(d.build(chunks,65536) || !d.build(chunks,uint64_t{1}<<31))std::abort();
  auto oracle=[&](size_t i){auto &r=rows[i];return Str(files[r.file]).substr(r.offset,r.len);};
  auto check=[&]<bool Compact>() {
    using Layout=SourceClickLayout<Compact>;using C=typename Layout::C;
    Layout layout{{-946684800000LL},&d};
    std::deque<SourceClick> extensions;std::vector<C> data;
    for(size_t i=0;i<rows.size();++i){
      auto &r=rows[i];auto ref=oracle(i);auto when=layout.origin-reach+r.ticks;
      auto c=layout.from_field(ref,when,r.campaign,files[r.file].data(),d.chunk_bases[first[r.file]],extensions);
      if(layout.click_time(c)!=when || layout.campaign_of(c)!=r.campaign || d.get(layout.locator(c))!=ref.data())std::abort();
      data.push_back(c);
    }
    if constexpr(Compact)if(extensions.empty() || extensions.size()==rows.size())std::abort();
    for(unsigned j=0;j<400000;++j){
      size_t a=rng()%rows.size(),b=j%2?rng()%rows.size():(a+3*(rng()%32))%rows.size();
      bool expected=rows[a].ticks==rows[b].ticks?oracle(b)<oracle(a):rows[a].ticks<rows[b].ticks;
      if(layout.click_less(data[a],data[b])!=expected)std::abort();
    }
  };
  check.template operator()<false>();check.template operator()<true>();
  std::puts("800000 timestamp and ID comparisons matched bytewise order, with cross-page IDs, ties, null bytes, epoch boundaries, campaign and timestamp overflow.");
}
