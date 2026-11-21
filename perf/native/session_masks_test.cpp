#include "event_layout.hpp"
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <span>

// The test only needs a map from campaign to a scratch value. End-to-end
// cases exercise the dense and sparse production campaign tables.
template<class T> using ScratchArray=std::unordered_map<int,T>;
#define SHOP_MASK_VECTOR 1
#include "session_click_masks.hpp"
struct Tally { int64_t sessions{},purchases{},revenue{},assists{}; bool operator==(const Tally &)const=default; };
template<class Layout> void check(unsigned n, Layout layout, Time first, std::mt19937 &random,
                                  Time stride=1800001) {
  using C=typename Layout::C;
  SessionClickMasks work;
  std::array<std::vector<C>,5> parts;
  KeyPool pool;
  std::vector<std::string> ids;
  ids.reserve(500);
  auto add=[&](Time t,int campaign){
    ids.push_back((random()%3?"k":"an arbitrarily long click ")+std::to_string(ids.size()));
    parts[random()%parts.size()].push_back(layout.click(pool.encode(ids.back(),'k'),t,campaign));
  };
  Time start=first;
  for(unsigned i=0;i<n;++i){
    if(i)start+=stride+random()%9000000;
    work.sessions[i]={start,int64_t(random()%7==0?random()%1000+1:0),int32_t((start-first)/86400000)};
    int campaign=1+int(random()%91);
    for(Time shift:{Time(0),Time(1),Time(-1),-reach,-reach-1,-reach+1})
      add(start+shift,campaign);
    add(start,1+int(random()%91));
  }
  add(first-1,100003);
  SessionClickMasks &saved=work;
  std::map<std::pair<int,int32_t>,Tally> expected,got;
  for(unsigned i=0;i<n;++i){
    auto session=work.sessions[i];const C *winner=nullptr;
    std::set<int> campaigns;
    for(auto &rows:parts)for(const auto &c:rows){
      auto t=layout.click_time(c);
      if(t>session.start || t<session.start-reach)continue;
      campaigns.insert(c.campaign);
      if(!winner || c.ts>winner->ts || (c.ts==winner->ts && c.id.user_less(winner->id,'k')))winner=&c;
    }
    auto &v=expected[{winner?winner->campaign:0,session.day}];
    ++v.sessions;v.purchases+=session.revenue>0;v.revenue+=session.revenue;
    for(int campaign:campaigns)++expected[{campaign,session.day}].assists;
  }
  for(unsigned repeat=0;repeat<2;++repeat){
    got.clear();
    saved.attribute(n,layout,[&](auto take){for(auto &part:parts)take(std::span<C>(part));},
                    [&](int key,int32_t day)->Tally&{return got[{key,day}];});
    if(expected!=got || !saved.touched.empty())std::abort();
  }
}
int main(){
  std::mt19937 random(183771);
  for(unsigned n=1;n<=64;++n)for(unsigned i=0;i<12;++i){
    // Keep compact timestamps within their representable (origin-reach) interval,
    // including the 1 ms before the earliest session's attribution boundary.
    check(n,WordLayout{-320000000},-319999500,random);
    check(n,WideLayout{},i%3==0?INT64_MAX-100000000000LL:i%2?3500000000000LL:-500000000000LL,
          random,i%4==0?700000001:1800001);
  }
  std::puts("1,536 compact and wide populations matched the independent per-session click oracle, each in two passes.");
}
