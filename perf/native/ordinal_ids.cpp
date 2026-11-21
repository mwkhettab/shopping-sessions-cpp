#include "../cpp/input.hpp"
#include "../cpp/ordinal_math.hpp"
#include "../cpp/ordinal_ids.hpp"
#include <cstdio>
#include <random>

#if defined(SHOPPING_X86_CSV) && defined(__x86_64__)
__attribute__((target("avx2,avx512f,avx512bw,avx512dq")))
static void check_lanes(const std::array<std::string,8>& names,ordinal_ids::Codec codec){
  alignas(64) char text[192]{};
  alignas(16) uint16_t offsets[8],widths[8];
  __mmask8 eligible=0xff;
  for(unsigned i=0;i<8;++i){
    offsets[i]=16*i;widths[i]=uint16_t(names[i].size());
    std::memcpy(text+offsets[i],names[i].data(),names[i].size());
    if(widths[i]<2 || widths[i]>9)eligible&=~(1u<<i);
  }
  auto from=_mm_load_si128(reinterpret_cast<const __m128i*>(offsets)),width=_mm_load_si128(reinterpret_cast<const __m128i*>(widths));
  auto values=base36_lanes(text,from,width,'u',eligible);
  alignas(64) uint64_t plain[8],decoded[8],fast[8];
  _mm512_store_si512(plain,values);_mm512_store_si512(decoded,index_dq(values,codec.salt));
  bool ifma=__builtin_cpu_supports("avx512ifma");
  if(ifma)_mm512_store_si512(fast,index_ifma(values,codec.salt));
  for(unsigned i=0;i<8;++i){
    auto reference=ordinal_ids::parse(names[i],'u');
    bool active=reference!=ordinal_ids::invalid;
    if(active!=bool(eligible&(1u<<i)))std::abort();
    if(active&&(plain[i]!=reference || decoded[i]!=codec.ordinal(ordinal_ids::unmix(reference)) ||
               (ifma&&fast[i]!=decoded[i])))std::abort();
  }
}
#endif
int main(){
  std::mt19937_64 rng(381001);
  std::array<std::string,8> names;
  for(unsigned trial=0;trial<45000;++trial){
    ordinal_ids::Codec c{rng()&ordinal_ids::mask};
    for(auto &s:names){
      uint64_t n=rng()&ordinal_ids::mask;
      if(c.ordinal(ordinal_ids::unmix(c.spelling(n)))!=n)std::abort();
      std::array<char,9> bytes;
      Key k=ordinal_ids::user_key(c,n);s=k.user_text(bytes);
      if(ordinal_ids::parse(s,'u')!=c.spelling(n))std::abort();
      switch(rng()%9){
        case 0:s[0]='U';break;
        case 1:s[1]='0';break;
        case 2:s.resize(1+rng()%9,'z');break;
        case 3:s[rng()%s.size()]=char(rng());break;
        case 4:s="uzzzzzzzz";break;
        case 5:s.resize(2);break;
        default:break;
      }
    }
#if defined(SHOPPING_X86_CSV) && defined(__x86_64__)
    if(__builtin_cpu_supports("avx512f")&&__builtin_cpu_supports("avx512bw")&&__builtin_cpu_supports("avx512dq"))
      check_lanes(names,c);
#endif
  }
  puts("360000 reversible ID cases passed, including irregular and out-of-range lane spellings.");
}
