#include "input.hpp"
#include <cstdio>
struct alignas(64) EventDigest {
  uint64_t rows{}, lengths{}, keys{}, times{}, direct{}, batched{};
  KeyPool pool;
};
template<unsigned Mode> struct ProjectedEvents {
  EventDigest &d;
  void times(Str ts,Str arrival) {
    ++d.rows;d.lengths+=ts.size()+arrival.size();
    if constexpr(Mode==2)d.times+=uint64_t(timestamp(ts))^uint64_t(ingest_time(arrival));
  }
  template<class Row> void operator()(const Row &row) {
    if(row[0].empty())return;
    if constexpr(Mode)d.keys+=d.pool.encode(row[0],'e').hash()+d.pool.encode(row[1],'u').hash();
    times(row[2],row[3]);
  }
  bool common(const char *id,unsigned iw,const char *user,unsigned uw,Str ts,Str arrival) {
    if constexpr(sizeof(uintptr_t)!=8 || std::endian::native!=std::endian::little)return false;
    if(id[0]!='e' || user[0]!='u' || !id[iw-1] || !user[uw-1])return false;
    uint64_t a=little_word(id+1)&(UINT64_MAX>>(8*(9-iw)));
    uint64_t b=little_word(user+1)&(UINT64_MAX>>(8*(9-uw)));
    if((a|b)>>63)return false;
    if constexpr(Mode)d.keys+=permute_key(a)+permute_key(b);
    times(ts,arrival);++d.direct;return true;
  }
  void prepared(uint64_t id,uint64_t user,Str ts,Str arrival){
    if constexpr(Mode)d.keys+=(id>>1)+(user>>1);
    times(ts,arrival);++d.direct;++d.batched;
  }
};
template<unsigned Mode> int sample(const char *dir,unsigned threads){
  Input input;auto chunks=input.parts(fs::path(dir)/"events");
  std::vector<EventDigest> workers(threads);
  auto start=std::chrono::steady_clock::now();
  parallel(chunks.size(),threads,[&](size_t j,unsigned t){
    ProjectedEvents<Mode> callback{workers[t]};rows<7>(chunks[j].body(),callback);
  });
  double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  uint64_t rows=0,lengths=0,keys=0,times=0,direct=0,batched=0;
  for(auto &x:workers){rows+=x.rows;lengths+=x.lengths;keys+=x.keys;times+=x.times;direct+=x.direct;batched+=x.batched;}
  std::cout<<seconds<<" "<<rows<<" "<<lengths<<" "<<keys<<" "<<times<<" "<<direct<<" "<<batched<<"\n";
  return 0;
}
int main(int argc,char **argv){
  if(argc!=4)return 2;
  unsigned n=std::stoul(argv[2]);if(!n || n>64)return 2;
  switch(std::stoul(argv[3])){case 0:return sample<0>(argv[1],n);case 1:return sample<1>(argv[1],n);case 2:return sample<2>(argv[1],n);default:return 2;}
}
