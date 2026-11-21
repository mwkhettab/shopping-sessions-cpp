#include "input.hpp"
#include "second_domain.hpp"
#include <cassert>
#include <cstdio>
#include <random>

int main(){
  using namespace std::chrono;
  std::mt19937 gen(510229);
  size_t clocks=0, windows=0;
  for(unsigned i=0;i<40000;++i){
    int y=std::array<int,12>{0,1,4,399,1600,1899,1900,1969,1970,2000,2100,9999}[gen()%12];
    unsigned m=1+gen()%12;
    unsigned days=unsigned(year_month_day_last{year(y),month_day_last{month(m)}}.day());
    unsigned d=1+gen()%days,h=gen()%24,mi=gen()%60,s=gen()%60;
    auto date=sys_days{year(y)/month(m)/day(d)}.time_since_epoch().count();
    Time local=(date*86400+h*3600+mi*60+s);
    char utc[32],zone[40];std::snprintf(utc,sizeof(utc),"%04d-%02u-%02uT%02u:%02u:%02uZ",y,m,d,h,mi,s);
    unsigned zh=gen()%24,zm=gen()%60;int sign=gen()%2?1:-1;
    std::snprintf(zone,sizeof(zone),"%04d-%02u-%02uT%02u:%02u:%02u%c%02u:%02u",y,m,d,h,mi,s,sign>0?'+':'-',zh,zm);
    Time adjusted=local-sign*int(zh*3600+zm*60);
    assert(checked_iso_second(utc)==local && checked_iso_second(zone)==adjusted);
    auto in=seconds_arrival(utc);
    assert(in.seconds==local && in.milliseconds==local*1000);clocks+=3;
    std::string text=i%2?utc:zone;
    if(i%3==0){size_t off=gen()%text.size();text[off]=char(gen()%256);}
    Time reference=timestamp(text);
    if((text.size()==20||text.size()==25)&&text[4]=='-'){
      auto val=checked_iso_second(text);assert(reference==(val==bad?bad:val*1000));++clocks;
    }
    Time ms=adjusted*1000;
    for(Time margin:{-1001,-1000,-999,-1,0,1,999,1000,1001}){
      for(Time delay:{-86401000,0,86399000,86400000,86400001,86401000}){
        SecondWindow w(ms+margin,ms+margin+1100);
        Time arrival=ms+delay;ArrivalStamp raw{arrival/1000,arrival};
        Time want=reference!=bad && reference>=w.first_ms && reference<w.end_ms &&
           (arrival<=reference || uint64_t(arrival)-uint64_t(reference)<=day_ms)?reference:bad;
        assert(w.event(text,raw)==want);
        if(arrival%1000==0)assert(w.event_whole_ingest(text,raw.seconds)==want);
        if(ms>=0){
          auto digits=std::string(i%18,'0')+std::to_string(ms+1);
          auto t=ms+1;Time expected=t>=w.first_ms && t<w.end_ms && arrival-t<=day_ms?t:bad;
          assert(w.event(digits,raw)==expected);
          if(arrival%1000==0)assert(w.event_whole_ingest(digits,raw.seconds)==expected);
        }
        ++windows;
      }
    }
  }
  assert(seconds_arrival("invalid").milliseconds==bad);
  std::printf("%zu clocks and %zu window/lag boundaries with proleptic Gregorian and scalar comparisons passed\n",clocks,windows);
}
