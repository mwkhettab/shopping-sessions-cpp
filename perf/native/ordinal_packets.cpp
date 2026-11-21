#include "../cpp/input.hpp"
#include "../cpp/ordinal_math.hpp"
#include "../cpp/ordinal_ids.hpp"
#include <cstdio>
#include <random>

#if defined(SHOPPING_X86_CSV) && defined(__x86_64__)
static std::string id(char prefix,uint64_t n,uint64_t salt){
  uint64_t v=ordinal_ids::Codec{salt}.spelling(n);std::string s;
  do{unsigned d=v%36;v/=36;s.push_back(char(d<10?'0'+d:'a'+d-10));}while(v);
  s.push_back(prefix);std::reverse(s.begin(),s.end());return s;
}
template<unsigned N>struct Audit {
  uint64_t event_salt=97110381,user_salt=64102001,event_bound=1<<21,user_bound=1<<18;
  bool cap_lanes=false,cap_multiply=false;
  static constexpr bool auxiliary=true;
#if __has_include("../cpp/ordinal_months.hpp")
  InputMonths calendar{timestamp("2000-02-19T00:00:00Z"),timestamp("2000-03-19T00:00:00Z")};
#endif
  std::vector<std::array<Str,N>> rows;
  size_t next{},fast{};
  uint64_t decode(Str key,char prefix,uint64_t salt){auto v=ordinal_ids::parse(key,prefix);if(v==ordinal_ids::invalid)std::abort();return ordinal_ids::Codec{salt}.ordinal(ordinal_ids::unmix(v));}
  template<class Row>void operator()(const Row &r){if(next>=rows.size())std::abort();for(unsigned i=0;i<N;++i)if(r[i]!=rows[next][i])std::abort();++next;}
  bool common(const char*,unsigned,const char*,unsigned,Str,Str){return false;}
  bool rich(const char*,unsigned,const char*,unsigned,Str,Str,Str,Str,Str){return false;}
  void numeric(uint64_t a,uint64_t b) requires(N==2){const auto &r=rows.at(next++);if(a!=decode(r[0],'u',user_salt)||b!=decode(r[1],'u',user_salt))std::abort();++fast;}
  void numeric(uint64_t uid,Str cid,Str campaign,Str ts) requires(N==4){const auto &r=rows.at(next++);if(uid!=decode(r[1],'u',user_salt)||cid!=r[0]||campaign!=r[2]||ts!=r[3])std::abort();++fast;}
  void numeric(uint64_t eid,uint64_t uid,Str ts,Str ingest) requires(N==7){const auto &r=rows.at(next++);if(eid!=decode(r[0],'e',event_salt)||uid!=decode(r[1],'u',user_salt)||ts!=r[2]||ingest!=r[3])std::abort();++fast;}
  void rich_numeric(uint64_t eid,uint64_t uid,Time ts,Time ingest,Str kind,Str product,Str price,bool simple) requires(N==7){const auto &r=rows.at(next++);if(eid!=decode(r[0],'e',event_salt)||uid!=decode(r[1],'u',user_salt)||ts!=timestamp(r[2])||ingest!=ingest_time(r[3])||kind!=r[4]||product!=r[5]||price!=r[6]||simple!=(!product.empty()&&(kind=="view"||kind=="cart")))std::abort();++fast;}
};
template<unsigned N>static void check(bool dq){
  std::mt19937_64 rng(91487+N);Audit<N>a;a.cap_multiply=dq;
  std::string body;
  std::array<Str,19> clocks{"2000-02-29T13:26:05Z","2000-03-01T00:00:00+00:01","1900-02-29T01:02:03Z","2400-02-29T23:58:59Z","2000-02-00T22:11:33Z","2000-01-61T24:59:58Z","2019-05-21T11:12:13-23:59","2000-01-03T11:12:13-24:00","2000-02-25T11:12:13-00:60","2000-02-25 11:12:13Z","0000000001235","1234567x90123","0000000000000000000000000123","2000-02-19T11:12:13z","184467440737095516160","1791071890000", "2000-22-15T21:03:00Z", "2000-25-03T11:12:13+00:00", "2000-02-31T11:12:13Z"};
  for(unsigned j=0;j<16000;++j){
    std::array<std::string,N> row;
    auto user=[&]{return id('u',(rng()%8000)*512+(rng()%4),a.user_salt);};
    if constexpr(N==2)row={user(),user()};
    if constexpr(N==4)row={id('k',j,77),user(),j%4?"c_2":" campaign",std::string(clocks[j%clocks.size()])};
    if constexpr(N==7)row={id('e',j,a.event_salt),user(),std::string(clocks[j%clocks.size()]),j%3?"2000-02-26T01:17:43Z":"2400-02-29T23:11:12Z",j%7?"view":"purchase",j%19?"p4":"",std::to_string(j+1)};
    if(j%397==0)row[0]="";if(j%283==0)row[1]="u0nul";if(j%307==0)row[1]=std::string(341,'x');
    for(unsigned f=0;f<N;++f){bool quoted=rng()%3==0;if(quoted)body+='"';body+=row[f];if(quoted)body+='"';body+=f+1==N?'\n':',';}
    if(j%127==0)body+="broken,\"midquote,field\n";
    if(j%107==0)body.append("\n");
  }
  body.pop_back();scalar_rows<N>(body,[&](const auto &r){a.rows.push_back(r);});
  if constexpr(N==7)parse_rows<7>(body,a);
#if __has_include("../cpp/ordinal_auxiliary.hpp")
  else auxiliary_rows<N>(body,a);
#else
  else return;
#endif
  if(a.next!=a.rows.size()||a.fast<3000)std::abort();
  std::printf("%u fields, %zu ordered callbacks and %zu checked numeric callbacks, DQ cap %u\n",N,a.next,a.fast,dq);
}
#endif
int main(){
#if defined(SHOPPING_X86_CSV) && defined(__x86_64__)
  if(wide_csv_supported()&&__builtin_cpu_supports("bmi2")&&__builtin_cpu_supports("pclmul")&&__builtin_cpu_supports("avx512f")&&__builtin_cpu_supports("avx512bw")&&__builtin_cpu_supports("avx512dq")){
    for(bool dq:{false,true}){check<7>(dq);if(__builtin_cpu_supports("avx512vbmi2")){check<4>(dq);check<2>(dq);}}
  }
#endif
}
