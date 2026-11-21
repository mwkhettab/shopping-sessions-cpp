#include "input.hpp"
#include <climits>
#include <joint_clock.hpp>
#include <cstdio>
#include <cstdlib>
#include <random>
int main(){
  const std::array<std::string,12> dates{"1969-12-31","0000-02-29","1900-02-28","1900-02-29","2000-02-29","2000-02-30","1970-01-01","0099-01-01","9999-12-31","2025-02-28","2026-10-31","2026-11-01"};
  std::mt19937_64 rng(154815);size_t checked=0,simd=0;
  auto check=[&](Str event,Str ingest){
    auto ref=EventClocks{timestamp(event),ingest_time(ingest)};
    auto exact=[&](EventClocks a){if(a.event!=ref.event || a.arrival!=ref.arrival)std::abort();};
    exact(joint_event_clocks<true,true>(event,ingest));exact(joint_event_clocks<true,false>(event,ingest));
    exact(joint_event_clocks<false,true>(event,ingest));++checked;
  };
  std::string a="2026-04-19T17:59:03Z",v="2026-04-20T23:59:59-23:59";
  for(size_t width:{20u,25u})for(size_t col=0;col<width;++col)for(int c=0;c<256;++c){
    std::string s=v.substr(0,width);if(width==20)s[19]='Z';s[col]=char(c);check(s,a);
  }
  for(unsigned k=0;k<190000;++k){
    unsigned h=rng()%24,m=rng()%60,s=rng()%60,oh=rng()%28,om=rng()%65;
    char end[20];std::snprintf(end,sizeof end,"T%02u:%02u:%02uZ",h,m,s);
    a=dates[rng()%dates.size()]+end;
    v=dates[rng()%dates.size()]+end;
    if(k%3==0){char z[8];std::snprintf(z,sizeof z,"%c%02u:%02u",k%2?'+':'-',oh,om);v.pop_back();v+=z;}
    if(k%7==0)v=std::to_string(rng());
    if(k%11==0)v=std::string(rng()%7,'0')+std::to_string(rng()%9999999999999LL);
    if(k%13==0 && !v.empty())v[rng()%v.size()]=char(rng());
    if(k%17==0)a=a.substr(0,rng()%17);
    check(v,a);
#ifdef SHOPPING_X86_CSV
    if(__builtin_cpu_supports("ssse3")){
      char e[9],in[9];std::snprintf(e,sizeof e,"%02u:%02u:%02u",h,m,s);
      unsigned ah=rng()%24,am=rng()%60,as=rng()%60;
      std::snprintf(in,sizeof in,"%02u:%02u:%02u",ah,am,as);
      if(k%3==0)e[rng()%8]=char(rng());
      auto parts=paired_hms(e,in);
      if(parts.event!=clock_seconds<true>(e) || parts.arrival!=int(3600*ah+60*am+as))std::abort();
      ++simd;
    }
#endif
  }
  std::printf("%zu independent clock comparisons, %zu SIMD lane comparisons, year zero and all byte substitutions passed.\n",checked,simd);
}
