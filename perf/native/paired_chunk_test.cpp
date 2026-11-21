#include "input.hpp"
#include <paired_chunk_reader.hpp>
#include <cstdio>
#include <random>

int main(){
  PairedChunkReader reader;
  size_t total=0;
  std::mt19937 rng(22550751);
  for(unsigned chunk=0;chunk<21;++chunk){
    std::string csv;
    for(unsigned row=0;row<5400;++row){
      csv+=row%71 ? "\"e01234567\",uabcdef01,0000000000001,2000-02-29T14:12:11Z,view,\"p\",\n"
                   : ",,,\"\",\"x\",,\n";
      if((row+chunk)%229==0)csv+="u,b,\"xx,extra\n";
    }
    if(chunk%5==0)csv.insert(csv.size()/2,std::string(95321,'X'));
    if(chunk%7==0)csv.pop_back();
    if(chunk==15)csv=std::string(150000,'x');
    if(chunk==16)csv=std::string(150000,'x')+"\n";
    if(chunk==17)csv.clear();
    std::vector<std::array<Str,7>> expected;
    scalar_rows<7>(csv,[&](const auto &r){expected.push_back(r);});
    size_t at=0;
    auto compare=[&](const auto &r){
      if(at>=expected.size())std::abort();
      for(unsigned i=0;i<7;++i)if(r[i]!=expected[at][i])std::abort();
      ++at;
    };
    if(chunk>=9)reader.ordinal=bool(chunk%2);
    reader.scan(csv,rng()%8,compare);
    if(at!=expected.size())std::abort();
    total+=at;
  }
#ifdef SHOPPING_X86_CSV
  if(wide_csv_supported() && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("pclmul") && reader.measured!=reader.samples)std::abort();
#endif
  std::printf("%zu scalar rows matched while switching parsers at real half-chunk boundaries, %u timing pairs.\n",total,reader.measured);
}
