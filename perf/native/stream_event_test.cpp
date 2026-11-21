#include "input.hpp"
#include <cstdio>
#include <cstring>
#include <random>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(SHOPPING_X86_CSV) && defined(__x86_64__)
using Fields=std::array<Str,7>;
struct StreamAudit {
  std::vector<Fields> scalar;
  size_t next{},marked{};
  bool accept{};
#ifdef CHECK_STREAM_BATCH
  size_t common_rows{};
  bool common(const char *id,unsigned il,const char *user,unsigned ul,Str ts,Str ingest) {
    if(next>=scalar.size())std::abort();
    const auto &row=scalar[next];
    if(Str(id,il)!=row[0] || Str(user,ul)!=row[1] || ts!=row[2] || ingest!=row[3] ||
       row[5].empty() || (row[4]!="view" && row[4]!="cart"))std::abort();
    ++common_rows;
    next+=accept;
    return accept;
  }
#endif
  void compare(const Fields &r) {
    if(next>=scalar.size() || r!=scalar[next]) std::abort();
  }
  void operator()(const Fields &r) { compare(r); ++next; }
  bool rich(const char *id,unsigned il,const char *user,unsigned ul,Str event,
            Str arrival,Str kind,Str product,Str price) {
    if (il<2 || il>9 || ul<2 || ul>9 || arrival.size()!=20) std::abort();
    compare({Str(id,il),Str(user,ul),event,arrival,kind,product,price});
    ++marked;
    next+=accept;
    return accept;
  }
};
size_t verify_stream(Str data) {
  StreamAudit audit;
  scalar_rows<7>(data, [&](const Fields &r) { audit.scalar.push_back(r); });
  for (bool direct : {false, true}) {
    for (bool force_avx2 : {false, true}) {
      audit.accept = direct;
      audit.next = 0;
      if (force_avx2)
        stream_events_avx2(data, audit);
      else
        parse_rows<7>(data, audit);
      if (audit.next != audit.scalar.size()) std::abort();
    }
  }
  return audit.marked;
}
#endif

int main() {
#if defined(SHOPPING_X86_CSV) && defined(__x86_64__)
  if (!wide_csv_supported() || !__builtin_cpu_supports("bmi2") ||
      !__builtin_cpu_supports("pclmul"))
    return 0;
  const std::string row = "e13,u22,0000000000001,2025-11-01T00:00:01Z,view,p,\n";
  const std::string quoted = "\"e11\",\"u45\",\"1\",\"2025-11-01T00:00:01Z\",\"cart\",\"\",\"\"\n";
  std::string population;
  for (unsigned i=0; i<164; ++i) population += i%3 ? row : quoted;
  size_t marked=0, cases=0;
  auto run = [&](Str s){ ++cases; marked += verify_stream(s); };
  run(population);
#ifdef CHECK_STREAM_BATCH
  {
    StreamAudit audit;
    scalar_rows<7>(population,[&](const Fields &r){audit.scalar.push_back(r);});
    audit.accept=true;
    stream_events_avx2(population,audit);
    if(audit.next!=audit.scalar.size() || !audit.common_rows)std::abort();
  }
#endif
  std::mt19937 rng(501791);
  for (unsigned width : {63,64,128,256,512,1024,2048,4096}) {
    for(int displacement=-4; displacement<=4; ++displacement) {
      size_t at = size_t(int(width) + displacement);
      for(char ch : std::array<char,7>{'"',',','\r','\n','\0','x',char(0xfd)}) {
        auto input=population;
        input[at]=ch;
        run(input);
        input[at-1]='"';
        run(input);
      }
      auto shifted = population;
      // Keep the separator count unchanged while moving a physical row boundary.
      auto lf=shifted.find('\n',at), comma=shifted.rfind(',',lf);
      std::swap(shifted[lf],shifted[comma]);
      run(shifted);
      for(auto suffix : {std::string("\""),std::string(",\n"),std::string("\r\n"),quoted,row}) {
        auto partial=population.substr(0,at)+suffix;
        run(partial);
      }
      auto long_field=std::string("\"")+std::string(at,'x')+"\",,,,,,\n";
      run(long_field+population);
      run(population+long_field);
    }
  }
  for (unsigned trial=0; trial<1300; ++trial) {
    auto input=population;
    for (unsigned j=0; j<1+trial%9; ++j) {
      size_t offset=rng()%input.size();
      char ch="\",\n\r\0x"[rng()%6];
      if(j%3==0) input[offset]=ch;
      else if(j%3==1) input.insert(offset,1,ch);
      else input.erase(offset,1);
    }
    if(trial%5==0) input.resize(rng()%input.size());
    run(input);
  }
#if defined(__linux__)
  const size_t page = size_t(sysconf(_SC_PAGESIZE));
  char *pages = static_cast<char *>(mmap(nullptr,5*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
  if (pages==MAP_FAILED || mprotect(pages+page,3*page,PROT_READ|PROT_WRITE)) std::abort();
  for (unsigned span : {1,63,64,65,511,512,513,1023,1024,1025,2047,2048,2049,4095,4096,4097,8193}) {
    for (bool left : {false,true}) {
      char *start = left ? pages+page : pages+4*page-span;
      std::memcpy(start,population.data(),span);
      run(Str(start,span));
    }
  }
  if(munmap(pages,5*page)) std::abort();
#endif
  if(!marked) std::abort();
  std::printf("%zu streaming populations, %zu checked callbacks across fallback and selected readers\n",cases,marked);
#endif
  return 0;
}
