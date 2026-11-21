// Fresh-process, cumulative input work probes. Link the actual decoder, clock,
// catalog, and ID implementations of the measured release.
#define main shopping_production_main
#include "main.cpp"
#undef main
struct alignas(64) Digest { uint64_t bytes{}, keys{}, semantics{}, rows{}; KeyPool ids; };
template<int mode> void probe(const fs::path &dir,unsigned threads) {
  Input input;Window w(input.map(dir/"manifest.json"));Catalog c(input,dir);
  auto ev=input.parts(dir/"events"), cl=input.parts(dir/"clicks"), links=input.parts(dir/"links");
  std::vector<Digest> dig(threads);
  parallel(links.size()+ev.size()+cl.size(),threads,[&](size_t i,unsigned owner){
    auto &d=dig[owner];
    auto lengths=[&]<size_t N>(auto &r){++d.rows;for(size_t j=0;j<N;++j)d.bytes+=r[j].size();};
    if(i<links.size()){
      rows<2>(links[i].body(),[&](auto &r){
        if constexpr(mode==0) lengths.template operator()<2>(r);
        else if(!r[0].empty() && !r[1].empty()){
          ++d.rows;
          if constexpr(mode>=2)d.keys+=d.ids.encode(r[0],'u').hash()+d.ids.encode(r[1],'u').hash();
        }
      }); return;
    }
    i-=links.size();
    if(i<ev.size()){
      rows<7>(ev[i].body(),[&](auto &r){
        if constexpr(mode==0) lengths.template operator()<7>(r);
        else if(!r[0].empty()){
          Time ingest=ingest_time(r[3]);
          auto e=validate(r,ingest,c,w);
          d.semantics+=static_cast<uint64_t>(ingest)+static_cast<uint64_t>(e.ts)+e.price+e.category;
          ++d.rows;
          if constexpr(mode>=2) {
            d.keys+=d.ids.encode(r[0],'e').hash();
            if(e.ts!=bad) d.keys+=d.ids.encode(r[1],'u').hash();
          }
        }
      });return;
    }
    i-=ev.size();
    rows<4>(cl[i].body(),[&](auto &r){
      if constexpr(mode==0) lengths.template operator()<4>(r);
      else if(!r[0].empty() && !r[1].empty()){
        auto ts=timestamp(r[3]);auto campaign=c.campaign.find(r[2]);
        if(ts<w.start-reach||ts>=w.end||campaign<0)return;
        d.semantics+=static_cast<uint64_t>(ts)+unsigned(campaign);++d.rows;
        if constexpr(mode>=2)d.keys+=d.ids.encode(r[0],'k').hash()+d.ids.encode(r[1],'u').hash();
      }
    });
  });
  uint64_t count=0,bytes=0,keys=0,sem=0;
  for(auto &d:dig){count+=d.rows;bytes+=d.bytes;keys+=d.keys;sem+=d.semantics;}
  std::cout<<count<<' '<<bytes<<' '<<keys<<' '<<sem<<'\n';
}
int main(int argc,char **argv) {
  if(argc!=4)return 2;
  const int mode=std::stoi(argv[2]); const unsigned n=std::stoul(argv[3]);
  if(mode==0)probe<0>(argv[1],n);else if(mode==1)probe<1>(argv[1],n);else if(mode==2)probe<2>(argv[1],n);else return 2;
}
