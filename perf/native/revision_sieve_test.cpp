#include "input.hpp"
#include <revision_sieve.hpp>
#include <cstdlib>
#include <cstdio>
#include <random>
int main(){
  std::mt19937_64 random(431296);size_t inspected=0;
  for(size_t words:{1u,16u,512u,4096u}){
    std::vector<LocalRevisionSieve> parts;for(int i=0;i<7;++i)parts.emplace_back(words);
    std::vector<uint32_t> expected(words*64);std::vector<uint64_t> key;
    for(size_t i=0;i<95000;++i){
      uint64_t k=i%5?random():17;
      ++expected[k%(64*words)];parts[random()%7].add(k);key.push_back(k);
    }
    LocalRevisionSieve all(words);
    for(size_t w=0;w<words;++w)for(auto &p:parts)all.words[w].merge(p.words[w]);
    for(auto h:key){if(all.suspect(h)!=(expected[h%(64*words)]>1))std::abort();++inspected;}
    if(all.singles()!=size_t(std::count(expected.begin(),expected.end(),1u)))std::abort();
  }
  std::string header="event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents\n";
  std::array<std::string,3> files{header,header,header};
  std::vector<std::string> names{"e1","e000abc","e12345678","unusual alias","eABC",std::string("e\0x",3),
                                  std::string(6000,'z'),std::string(1,char(155))};
  for(size_t i=0;i<1700;++i){
    auto &f=files[i%3];auto id=names[i%names.size()];
    if(i%19==0)id+=" single "+std::to_string(i);
    if(i%2)id='"'+id+'"';f+=id;
    f+=i%17?",u,1,2026-01-01T00:00:00Z,view,p1,\n":",too,few\n";
    if(i%11==0)f+="bad\"first,ignored,1,2025-12-31T21:00:00Z,view,p2,\n";
  }
  for(auto &f:files)f.pop_back();
  for(size_t length:{1u,13u,256u,6144u,131072u}){
    std::vector<InputChunk> chunks;
    for(auto &f:files)for(size_t off=0;off<f.size();off+=length)chunks.push_back({f,off,std::min(f.size(),off+length)});
    // Very small boundaries are covered on a shorter prefix for this unit.
    if(length==1 || length==13){chunks.clear();for(auto &f:files){Str small(f.data(),std::min<size_t>(20519,f.size()));for(size_t off=0;off<small.size();off+=length)chunks.push_back({small,off,std::min(small.size(),off+length)});}}
    for(unsigned workers:{1u,8u}){
      InputRevisionSieve filter;filter.build(chunks,4000,workers,8);
      if(filter.repeated.empty())std::abort();
      size_t size=filter.repeated.size()*64;
      std::vector<uint32_t> occurrences(size);std::vector<uint64_t> valid;
      KeyPool pool;
      for(auto &c:chunks){
        Str body=c.body();first_ids(body,[&](Str id){auto k=pool.encode(id,'e');++occurrences[k.hash()%(size)];if(k.indirect())pool.other.pop_back();});
        scalar_rows<7>(body,[&](auto &r){if(!r[0].empty()){auto k=pool.encode(r[0],'e');valid.push_back(k.hash());if(k.indirect())pool.other.pop_back();}});
      }
      size_t records=0,singles=0;for(auto c:occurrences){records+=c;singles+=c==1;}
      if(filter.records!=records || filter.singletons!=singles)std::abort();
      for(auto h:valid){if(filter.suspect(h)!=(occurrences[h%size]>1) || !occurrences[h%size])std::abort();++inspected;}
      std::sort(valid.begin(),valid.end());for(size_t i=1;i<valid.size();++i)if(valid[i]==valid[i-1] && !filter.suspect(valid[i]))std::abort();
    }
  }
  std::printf("%zu bucket and parsed event checks passed with split files, missing final newlines, quoted and arbitrary IDs.\n",inspected);
}
