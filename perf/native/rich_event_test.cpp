#include "input.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>

using Seven=std::array<Str,7>;
struct RowComparison {
  std::vector<Seven> scalar;
  size_t next{}, direct{};
  void operator()(const Seven &row) {
    if(next>=scalar.size() || scalar[next]!=row)std::abort();
    ++next;
  }
  bool rich(const char *id,unsigned iw,const char *user,unsigned uw,Str ts,Str ingest,Str kind,Str product,Str price){
    const Seven row{Str(id,iw),Str(user,uw),ts,ingest,kind,product,price};operator()(row);++direct;return true;
  }
  bool common(const char *id,unsigned id_width,const char *user,unsigned user_width,Str ts,Str ingest) {
    if(next>=scalar.size())std::abort();
    const auto &row=scalar[next];
    if(id_width<2 || id_width>9 || user_width<2 || user_width>9 || row[0]!=Str(id,id_width) ||
       row[1]!=Str(user,user_width) || row[2]!=ts || row[3]!=ingest ||
       (row[4]!="view" && row[4]!="cart") || row[5].empty())std::abort();
    ++next;++direct;
    return true;
  }
};
int main(){
  std::mt19937 random(99237);
  const std::array<std::string,7> times{"0000000000001","2025-11-01T00:00:00Z","2026-05-11T00:00:00-13:17",
                                         "123456","",std::string(41,'x'),std::string(23,'Z')};
  size_t cases=0,direct=0;
  const std::array<std::string,9> kinds{"view","purchase","cart","purchasE","p","viewX","",std::string(18,'p'),"pURCHASE"};
  for(unsigned width=0;width<21;++width){
    for(unsigned stamp=0;stamp<times.size();++stamp){
      std::string batch;
      for(unsigned wraps=0;wraps<128;++wraps){
        unsigned uid_width=(37*wraps+width)%22;
        std::array<std::string,7> fields{std::string(width,'e'),std::string(uid_width,'u'),times[stamp],
          "2025-11-01T00:00:01Z",kinds[(wraps+width+stamp)%kinds.size()],wraps%17?"p":"",wraps%5?"":"999"};
        std::string line;
        for(unsigned c=0;c<7;++c){
          if(c)line+=',';
          bool q=wraps>>c&1;
          if(q)line+='"';
          line+=fields[c];
          if(q)line+='"';
        }
        auto append=[&](const std::string &row){batch+=row;batch+='\n';++cases;};
        append(line);
        for(unsigned k=0;k<6;++k){
          auto s=line;
          size_t at=random()%s.size();
          if(k==0)s[at]='"';
          if(k==1)s.insert(at,1,',');
          if(k==2)s.erase(at,1);
          if(k==3)s[at]='\0';
          if(k==4){std::swap(s[at],s[random()%s.size()]);}
          if(k==5){s[at]='\r';s.insert(random()%s.size(),1,'"');}
          append(s);
        }
      }
      if(width%2)batch.pop_back();
      RowComparison audit;
      scalar_rows<7>(batch,[&](auto &r){audit.scalar.push_back(r);});
      rows<7>(batch,audit);
      if(audit.next!=audit.scalar.size())std::abort();
      direct+=audit.direct;
    }
  }
#ifdef SHOPPING_X86_CSV
  if(wide_csv_supported() && direct==0)std::abort();
#endif
  std::printf("%zu complete and mutated rows matched the scalar reader, including %zu direct event callbacks.\n",cases,direct);
}
