#include "input.hpp"
#include <cstdio>
#include <random>

using Seven = std::array<Str,7>;
struct ClockAudit {
  const std::vector<Seven> &expected;
  KeyPool pool;
  size_t next{}, fast{};
  template<class Row> void operator()(const Row &r) {
    if(next>=expected.size())std::abort();
    for(unsigned i=0;i<7;++i)if(expected[next][i]!=r[i])std::abort();
    ++next;
  }
  void decoded(uint64_t id,uint64_t user,Time event,Time arrival) {
    if(next>=expected.size())std::abort();
    auto &r=expected[next++];
    if(pool.encode(r[0],'e').bits!=id || pool.encode(r[1],'u').bits!=user ||
       timestamp(r[2])!=event || ingest_time(r[3])!=arrival ||
       (r[4]!="view" && r[4]!="cart") || r[5].empty())std::abort();
    ++fast;
  }
};
#ifdef SHOPPING_X86_CSV
__attribute__((target("avx2,bmi,bmi2,popcnt,pclmul,avx512f,avx512bw,avx512dq")))
void primitive(std::mt19937 &rng) {
  for(unsigned iter=0;iter<20000;++iter) {
    alignas(64) char words[8][8];
    for(auto &p:words) {
      for(unsigned j=0;j<8;++j)p[j]=char('0'+(rng()%10));
      if(iter%3)p[2]=p[5]=':';
      if(iter%5<2)p[rng()%8]=char(rng()%256);
    }
    auto v=_mm512_load_si512(words);
    __mmask8 ok=255,digits=eight_ascii_digits(v);
    alignas(64) uint64_t seconds[8],decimal[8];
    _mm512_store_si512(seconds,eight_hms(v,&ok));
    _mm512_store_si512(decimal,eight_decimal_words(v));
    for(unsigned i=0;i<8;++i) {
      int s=clock_seconds<true>(words[i]);
      bool is_decimal=true;unsigned value=0;
      for(char raw:words[i]){unsigned k=unsigned(uint8_t(raw))-'0';is_decimal&=k<10;value=value*10+k;}
      if(bool(ok&(1u<<i))!=(s>=0) || (s>=0 && seconds[i]!=uint64_t(s)) ||
         bool(digits&(1u<<i))!=is_decimal || (is_decimal && decimal[i]!=value))std::abort();
    }
  }
}
void mixed(std::mt19937 &rng) {
  const std::array<std::string,21> forms{
    "0000000000000","1234567890123","9999999999999","00000000000000000001",
    "0000000000000000000000123","2028-02-29T23:59:59Z","1900-02-29T12:00:00Z",
    "2000-02-29T00:00:00+23:59","1970-01-01T00:00:00-00:00",
    "1969-12-31T23:59:59Z","2001-02-03T00:07:55+24:00",
    "1999-03-03T00:52:19-00:60","", "1","-1","18446744073709551615",
    "2026-12-14t00:02:06Z","1980-01-02T06:59:00z","2500-04-31T01:02:03Z",
    "2004-04-30T01:02:03-00:56","0000-01-01T00:00:00Z"};
  size_t emitted=0;
  for(unsigned i=0;i<6500;++i){
    std::array<uint16_t,58> cuts{};
    std::string text;
    for(unsigned lane=0;lane<8;++lane) {
      std::string stamp=forms[(i+lane*5)%forms.size()];
      if(i%4==0 && !stamp.empty()) {
        size_t at=rng()%stamp.size();
        char ch=char(rng()%256);
        if(ch!='"'&&ch!=','&&ch!='\n')stamp[at]=ch;
      }
      std::array<std::string,7> fields{std::string(2+rng()%8,'e'),std::string(2+rng()%8,'u'),stamp,
        i%2?"2000-02-29T23:12:33Z":"1970-01-01T00:00:00Z",i%3?"view":"cart","p",""};
      if((i+lane)%23==0)fields[4]="purchase";
      if((i+lane)%37==0)fields[1].back()='\0';
      if((i+lane)%131==0)fields[0]="Long event id";
      for(unsigned j=0;j<7;++j){
        bool quotes=rng()%3==0;
        if(quotes)text+='"';text+=fields[j];if(quotes)text+='"';
        text+=j==6?'\n':',';
        cuts[1+7*lane+j]=uint16_t(2*text.size()+quotes);
      }
    }
    std::vector<Seven> values;scalar_rows<7>(text,[&](const Seven &r){values.push_back(r);});
    if(values.size()!=8)std::abort();
    auto check=[&]<bool ISO,bool Epoch,bool Share>() {
      ClockAudit audit{values};
      emit_clock_batch<ISO,Epoch,Share>(text.data(),cuts.data()+1,audit);
      if(audit.next!=8)std::abort();
      emitted+=audit.fast;
    };
    check.template operator()<false,false,false>();check.template operator()<true,false,false>();
    check.template operator()<false,true,false>();check.template operator()<true,true,false>();
    check.template operator()<true,true,true>();
  }
  if(emitted<200000)std::abort();
  std::printf("160000 independent eight-byte cases and %zu checked mixed-format row callbacks\n",emitted);
}
#endif
int main(){
#ifdef SHOPPING_X86_CSV
  if(wide_csv_supported() && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("pclmul") &&
     __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512dq")) {
    std::mt19937 rng(211977);primitive(rng);mixed(rng);
  } else std::puts("The vector kernel needs AVX-512F/BW/DQ. Checked the portable build only.");
#endif
}
