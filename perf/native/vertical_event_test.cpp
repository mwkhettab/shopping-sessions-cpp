#include "input.hpp"
#include <cstdio>
#include <random>
using Fields7=std::array<Str,7>;
struct ObservedEvents {
  const std::vector<Fields7> &expected;
  KeyPool pool;
  size_t seen{},common_count{},prepared_count{};
  template<class Row> void operator()(const Row &row){
    if(seen>=expected.size())std::abort();
    for(unsigned i=0;i<7;++i)if(row[i]!=expected[seen][i])std::abort();
    ++seen;
  }
  void eligibility(Str event,Str ingest){
    if(seen>=expected.size())std::abort();
    const auto &r=expected[seen];
    if(r[0].size()<2 || r[0].size()>9 || r[1].size()<2 || r[1].size()>9 ||
       r[2]!=event || r[3]!=ingest || r[3].size()!=20 || (r[4]!="view" && r[4]!="cart") || r[5].empty())
      std::abort();
  }
  bool common(const char *id,unsigned iw,const char *user,unsigned uw,Str event,Str ingest){
    eligibility(event,ingest);
    const auto &r=expected[seen];
    if(r[0]!=Str(id,iw)||r[1]!=Str(user,uw))std::abort();
    if(pool.encode(r[0],'e').indirect() || pool.encode(r[1],'u').indirect())return false;
    ++common_count;++seen;
    return true;
  }
  void prepared(uint64_t a,uint64_t b,Str event,Str ingest){
    eligibility(event,ingest);
    const auto &r=expected[seen];
    if(!a || !b || (a&1) || (b&1) || pool.encode(r[0],'e').bits!=a || pool.encode(r[1],'u').bits!=b)std::abort();
    ++prepared_count;++seen;
  }
};
int main(){
#ifdef SHOPPING_X86_CSV
  if(!wide_csv_supported() || !__builtin_cpu_supports("bmi2") || !__builtin_cpu_supports("pclmul"))return 77;
  const bool key_cpu=__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512dq");
  std::mt19937 gen(284043);size_t checked=0,mixed=0,prepared=0;
  std::string large;
  for(unsigned iteration=0;iteration<6000;++iteration){
    std::string text;
    std::array<uint16_t,57> cuts{};
    for(unsigned row=0;row<8;++row){
      unsigned q=(iteration+row*19)%128;
      std::array<std::string,7> f{"e"+std::to_string(gen()),"u"+std::to_string(gen()),"2028-02-29T23:00:00Z","2028-03-01T01:00:00Z",
                                (row+iteration)%2?"view":"cart","p671","arbitrary price"};
      f[0].resize(2+gen()%8,'a');f[1].resize(2+gen()%8,'b');
      unsigned shape=(iteration+row)%17;
      if(shape>=6)f[2]=std::array<std::string,5>{"10001","00001791234567890","1940-03-02T04:07:20-23:10","",std::string(iteration%467,'Z')}[shape%5];
      if(shape==3)f[3]="bad";
      if(shape==4)f[4]="purchase";
      if(shape==5)f[5].clear();
      if(shape==7)f[0][0]='E';
      if(shape==8)f[1][0]='U';
      if(shape==9)f[0].back()='\0';
      if(shape==10){f[1].resize(9,'c');f[1].back()=char(0x82);}
      if(shape==11){f[0].resize(19,'x');f[1]="u";}
      if(shape==12 && f[0].size()>2)f[0][1]='\0';
      if(shape==13)f[4]="view ";
      if(shape==14)f[4]=std::string("vi\0w",4);
      for(unsigned c=0;c<7;++c){
        bool quote=(q>>c)&1;
        if(quote)text+='"';
        text+=f[c];
        if(quote)text+='"';
        text+=(c==6?'\n':',');
        cuts[1+7*row+c]=uint16_t(2*text.size()+unsigned(quote));
      }
    }
    std::vector<Fields7> expected;
    scalar_rows<7>(text,[&](const auto &r){expected.push_back(r);});
    if(expected.size()!=8)std::abort();
    ObservedEvents audit{expected};
    emit_shape_batch(text.data(),cuts.data()+1,audit);
    if(audit.seen!=8)std::abort();
    checked+=audit.seen;
    if(key_cpu){
      ObservedEvents v{expected};emit_key_batch(text.data(),cuts.data()+1,v);
      if(v.seen!=8)std::abort();
      checked+=v.seen;prepared+=v.prepared_count;
    }
    large+=text;
    if(iteration%9==0)large.insert(large.size()-text.size()/2,1,iteration%3==0?'\"':',');
    if(iteration%31==30){
      if(iteration%2)large.pop_back();
      expected.clear();scalar_rows<7>(large,[&](const auto &r){expected.push_back(r);});
      ObservedEvents rows{expected};parse_rows<7>(large,rows);
      if(rows.seen!=expected.size())std::abort();
      mixed+=rows.seen;large.clear();
    }
  }
  if(key_cpu && prepared<5000)std::abort();
  std::printf("%zu direct rows, %zu complete mixed-file rows and %zu verified vector hash pairs, AVX-512DQ %d\n",checked,mixed,prepared,int(key_cpu));
#endif
}
