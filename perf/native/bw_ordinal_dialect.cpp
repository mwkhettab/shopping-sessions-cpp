#include "input.hpp"
#include <cstdio>

#ifdef SHOPPING_X86_CSV
struct Counts {size_t rows{},fast{};};
template<size_t N> void compare(Str input,bool wide,Counts &counts) {
  std::vector<std::array<Str,N>> expected;
  auto baseline=[&](auto &row){expected.push_back(row);};
  scalar_rows<N>(input,baseline);
  size_t i=0;
  auto callback=[&](const auto& row){
    using R=std::remove_cvref_t<decltype(row)>;
    if constexpr(std::is_same_v<R,OrdinalFields16> || std::is_same_v<R,CommaTileFields<N,true>> || std::is_same_v<R,CommaTileFields<N,false>>
#if __has_include(<byte_ordinal_tiles.hpp>)
      || std::is_same_v<R,ByteTileFields<true>> || std::is_same_v<R,ByteTileFields<false>> || std::is_same_v<R,WordTileFields>
#endif
    )++counts.fast;
    if(i>=expected.size())std::abort();
    for(size_t f=0;f<N;++f)if(row[f]!=expected[i][f])std::abort();
    ++i;
  };
  if(wide)ordinal16_wide<N,512,false>(input,callback);
  else ordinal16_standard<N,512,false>(input,callback);
  if(i!=expected.size())std::abort();
  counts.rows+=i;
}
template<size_t N> void exercise(bool wide,Counts &counts) {
  std::string good;
  for(unsigned i=0;i<120;++i){
    for(size_t f=0;f<N;++f){
      if(f)good+=',';
      if((i+f)%7==0)good+="\"\"";
      else if((i+f)%3==0)good+="\" x \"";
      else if((i+f)%5)good+="v";
    }
    good+='\n';
  }
  compare<N>(good,wide,counts);
  for(unsigned padding=0;padding<260;++padding){
    for(unsigned width:{127u,128u,254u,255u,256u,257u,510u,511u,512u,763u,1024u}){
      std::string large=std::string(padding,'p')+std::string(N-1,',')+"\n";
      // Total content length, including quotes and all N-1 commas, is exactly width.
      large+='"';large.append(width-(N-1)-2,'q');large+='"';
      large+=std::string(N-1,',')+"\n";large+=good;
      compare<N>(large,wide,counts);
    }
  }

  for(unsigned code=0;code<2187;++code){
    std::string field;
    unsigned n=code;
    for(unsigned j=0;j<7;++j){field+="x\","[n%3];n/=3;}
    std::string text;
    unsigned pos=code%N;
    for(size_t f=0;f<N;++f){
      if(f)text+=',';
      if(f==pos){text.append((code%4)*64+61+(code%7),'x');text+=field;}
      else if((f+code)%2)text+="\"a\"";
    }
    text+='\n';text+=good;
    if(code%2)text.pop_back();
    compare<N>(text,wide,counts);
  }
}
#endif
int main(){
#ifdef SHOPPING_X86_CSV
  if(!wide_csv_supported() || !__builtin_cpu_supports("bmi2") || !__builtin_cpu_supports("pclmul"))return 77;
  bool wide=__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw");
  Counts all;
  for(unsigned mode=0;mode<1+unsigned(wide);++mode){
    Counts counts;
    exercise<2>(mode,counts);exercise<4>(mode,counts);exercise<7>(mode,counts);
    if(counts.fast<100000)std::abort();
    all.rows+=counts.rows;all.fast+=counts.fast;
  }
  std::printf("%zu checked rows, %zu consumed directly in certified tiles; AVX512F/BW %d\n",all.rows,all.fast,int(wide));
#endif
}
