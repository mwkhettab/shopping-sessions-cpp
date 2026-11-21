#include "input.hpp"
#include <partition_slab.hpp>
#include <trim_revision_pages.hpp>
#include <cstdio>
#include <cstdlib>
struct Mapping {
  void *region=MAP_FAILED;size_t bytes{};
  ~Mapping(){if(region!=MAP_FAILED)munmap(region,bytes);}
};
template<bool unmap> void check(size_t page,size_t prefix,size_t length){
  Mapping m;m.bytes=length;
  m.region=mmap(nullptr,length,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  if(m.region==MAP_FAILED)std::abort();
  auto base=m.region;auto *v=static_cast<unsigned char*>(base);
  for(size_t i=0;i<length;++i)v[i]=static_cast<unsigned char>(i*113+5);
  trim_revision_mapping<unmap>(m,prefix,page);
  size_t expected=prefix>=length?length:std::min(length,((prefix+page-1)/page)*page);
  if(unmap && (m.bytes!=expected || (m.region==MAP_FAILED)!=(expected==0)))std::abort();
  if(!unmap && (m.bytes!=length || m.region!=base))std::abort();
  for(size_t i=0;i<std::min(prefix,length);++i)if(v[i]!=static_cast<unsigned char>(i*113+5))std::abort();
  if(m.region!=MAP_FAILED)trim_revision_mapping<unmap>(m,std::min(prefix,m.bytes),page);
}
int main(){
  auto page=size_t(sysconf(_SC_PAGESIZE));
  for(size_t length:{page,page+137,page*5,page*11+59})
    for(size_t prefix:{size_t(0),size_t(1),page-1,page,page+1,page*2+17,length-1,length,length+1}){
      check<false>(page,prefix,length);check<true>(page,prefix,length);
    }
  std::puts("72 full-byte comparisons passed around partial pages, empty regions, advice and repeated trims.");
}
