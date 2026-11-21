#include "input.hpp"
#include <random>
#include <cassert>
int main(){
  std::mt19937 g(77602);
  const std::array<std::string,4> t={"1783382890850","2000-02-29T13:23:13Z","2000-02-29T13:23:13+12:00","0000000000000"};
  std::string csv;
  auto test=[&] {
    std::vector<std::array<Str,7>> a,b;
    scalar_rows<7>(csv,[&](auto r){a.push_back(r);});
    rows<7>(csv,[&](auto r){b.push_back(r);});
    assert(a==b);csv.clear();
  };
  for(unsigned k=0;k<140000;++k) {
    std::array<std::string,7> r={"e12345678","uabcdef01",t[g()%t.size()],"2000-02-29T14:12:11Z",
      std::array<std::string,4>{"cart","noop","purchase","view"}[g()%4],std::string(g()%15,'x'),std::string(g()%12,'0')};
    if((k/1000)%3==1){r[0].resize(2+k%17,'A');r[1].resize(3+k%13,'U');}
    std::string line;
    for(unsigned i=0;i<7;++i){if(i)line+=',';bool q=g()%3==0;if(q)line+='"';line+=r[i];if(q)line+='"';}
    if(k%3)for(size_t m=g()%4;m;--m){
      auto j=g()%line.size();
      char c=std::array<char,9>{'"',',','\n','\0','\r','-',char(0xff),'+','Z'}[g()%9];
      if(k%7==0)line.erase(j,1);else if(k%5==0)line.insert(j,1,c);else line[j]=c;
    }
    csv+=line;if(k%101)csv+='\n';
    if(k%801==800)test();
  }
  test();
}
