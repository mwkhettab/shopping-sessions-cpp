#include "input.hpp"
#include "alias_quotient.hpp"
#include <cstdio>
#include <random>

struct AliasCase { Key key; };
template<unsigned Ways,unsigned Distance> void trial(size_t buckets,unsigned shard){
  AliasQuotients<Ways,Distance> index(buckets);
  std::vector<AliasCase> groups;
  std::unordered_map<Key,size_t,KeyHash> reference;
  std::deque<std::string> strings;
  KeyPool pool;
  auto check=[&](Key k){
    auto [it,added]=reference.try_emplace(k,reference.size());
    auto id=index.intern(k,groups);
    if(id!=it->second || groups.size()!=reference.size() || !(groups[id].key==k))std::abort();
  };
  std::mt19937_64 rng(7293681+buckets+Ways);
  std::vector<Key> seen;
  for(unsigned i=0;i<12000;++i){
    uint64_t home=(i%3)?buckets-1:rng()%buckets;
    uint64_t quotient=(i%2)?i%83:((rng()>>15)|1);
    uint64_t hash=(((quotient*buckets+home)*shards+shard)&key_mask);
    Key key{uintptr_t(hash<<1)};
    if(i%7==0){
      strings.push_back("unusual large borrowed alias "+std::to_string(i%63));
      key=pool.encode(strings.back(),'u');
    }
    check(key);seen.push_back(key);
    check(seen[rng()%seen.size()]);
  }
  for(auto key:seen)check(key);
}
int main(){
  for(size_t n:{1u,4u,64u,2048u})for(unsigned b:{0u,31u,511u}){
    trial<1,3>(n,b);trial<2,0>(n,b);trial<4,1>(n,b);trial<2,2>(n,b);
  }
  std::puts("1,728,000 exact alias lookups passed across bucket wraparound, full locator ranges and repeated indirect strings.");
}
