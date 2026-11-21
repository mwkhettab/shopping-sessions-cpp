#include "input.hpp"
#include <event_layout.hpp>
#include <partition_slab.hpp>
#include <sparse_raw_events.hpp>
#ifdef SHOP_TEST_WORD
#include <raw_event_words.hpp>
#endif
#include <cstdio>
#include <cstdlib>
#include <random>

template<class Storage> void exercise(size_t hint,bool wide_group){
  KeyPool pool;
  std::deque<std::string> texts;
  std::vector<Key> names;
  for(unsigned k=1;k<=1600;++k){
    auto &s=texts.emplace_back(k%3?"u"+std::to_string(k):"raw-outside alpha "+std::to_string(k));
    names.push_back(pool.encode(s,'u'));
  }
  std::mt19937_64 random(12068);
  struct Row{Key user;uint64_t payload;uintptr_t group;bool dropped;};
  std::array<std::vector<Row>,shards> want;
  Storage rows;
  rows.reserve(hint);
  for(unsigned n=0;n<100000;++n){
    Key user=names[n%7?random()%names.size():3];
    uint32_t off=uint32_t(random());
    if(n%3!=1)off=off/1000*1000;
    if(n%71==0)off=UINT32_MAX;
    if(n%73==0)off=0;
    uint32_t detail=n%5==0?uint32_t(random()):0;
    uint64_t event=(uint64_t(off)<<32)|detail;
    auto b=user.hash()%shards;
    size_t i=want[b].size();
    uintptr_t gi=uintptr_t((n%17==0 && wide_group)?(uint64_t{1}<<43)+n:n%157);
    bool drop=n%11==0;
    rows.append(b,user,event);
    if(drop)rows.erase(b,i);
    want[b].push_back({user,event,gi,drop});
  }
  for(size_t b=0;b<shards;++b){
    size_t observed=0;
    rows.index_users(b,[&](Key value){
      auto &r=want[b][observed++];
      if(r.dropped){if(value.bits)std::abort();return uintptr_t(UINTPTR_MAX);}
      if(!(value==r.user))std::abort();
      return r.group;
    });
    if(observed!=want[b].size())std::abort();
    size_t i=0;
    rows.scatter(b,[&](uintptr_t group,uint64_t raw){
      while(i<want[b].size() && want[b][i].dropped)++i;
      if(i==want[b].size() || want[b][i].group!=group || want[b][i].payload!=raw)std::abort();
      ++i;
    });
    while(i<want[b].size() && want[b][i].dropped)++i;
    if(i!=want[b].size())std::abort();
    rows.clear_bucket(b);
  }
}
int main(){
  for(size_t hint:{0u,1u,512u}){
    exercise<SparseRawEvents<WordLayout,false,false>>(hint,true);
    exercise<SparseRawEvents<WordLayout,true,false>>(hint,true);
#ifdef SHOP_TEST_WORD
    exercise<FinalRawWords<WordLayout>>(hint,true);
#endif
  }
#ifdef SHOP_TEST_WORD
  std::puts("900000 raw event positions matched all identities, lost rows, maximum timestamps, spilled groups and purchase words.");
#else
  std::puts("600000 sparse raw event positions matched independent keys, counts and complete values.");
#endif
}
