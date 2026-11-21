#include "input.hpp"
#include "row_clocks.hpp"
#include <cstdio>
#include <random>

template<int width>
void check_clocks() {
#if defined(SHOPPING_X86_CSV)
  std::mt19937 g(4703+width);
  std::array<std::string,4> e,a;
  const char *sample=width==13?"1791071890000":width==20?"2032-02-29T23:59:59Z":"2032-02-29T23:59:59-23:59";
  for(unsigned changes=0;changes<unsigned(width)*256+1500;++changes){
    ClockRow r[4];Time event[4],ingest[4];
    for(unsigned i=0;i<4;++i){
      e[i]=sample;
      if(changes<unsigned(width)*256){
        if(i%2==0)e[i][(changes/256+i)%width]=char(changes%256);
      }else for(unsigned j=0;j<3;++j)e[i][g()%width]=char(g()%256);
      if constexpr(width!=13)e[i][4]='-'; // the production caller enforces this
      char arrival[25];
      std::snprintf(arrival,sizeof(arrival),"%04u-%02u-%02uT%02u:%02u:%02uZ",unsigned(g()%9999+1),unsigned(g()%12+1),unsigned(g()%28+1),unsigned(g()%24),unsigned(g()%60),unsigned(g()%60));
      a[i]=arrival;r[i]={{},{},e[i].data(),a[i].data(),0,0};
    }
    clock_block<width>(r,event,ingest);
    for(unsigned i=0;i<4;++i){
      if(event[i]!=timestamp(e[i]) || ingest[i]!=ingest_time(a[i])){
        std::fprintf(stderr,"width %d case %u lane %u epoch %lld vs %lld\n",width,changes,i,(long long)event[i],(long long)timestamp(e[i]));
        std::abort();
      }
    }
  }
#endif
}
int main(){
#if defined(SHOPPING_X86_CSV)
  if(!__builtin_cpu_supports("avx2"))return 77;
  check_clocks<13>(); check_clocks<20>(); check_clocks<25>();
  std::puts("Four-lane F1/F2/F3 and ingest clocks matched the scalar reference on byte mutations and valid civil dates.");
#else
  return 77;
#endif
}
