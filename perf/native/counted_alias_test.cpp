#include "input.hpp"
#include "counted_alias_slots.hpp"
#include <cstdio>
#include <random>
#include <thread>

template<size_t Slots,unsigned Tries> void exercise() {
  CountedAliasSlots<Slots,Tries> book; book.prepare(10*Slots);
  std::vector<std::string> texts;
  for(unsigned i=0;i<257;++i)texts.push_back(i%4?"arbitrary-"+std::to_string(i):"u"+std::to_string(i));
  texts.emplace_back("u\0\0x",4);
  texts.emplace_back("u\0x\0",4);
  texts.emplace_back("unicode-用户");
  texts.emplace_back(" u1");
  texts.emplace_back("u1 ");
  KeyPool pool;std::vector<Key> keys;
  for(const auto &s:texts)keys.push_back(pool.encode(s,'u'));
  std::mt19937_64 rng(20371);
  struct Row {size_t name;Key code;bool click,erase;};
  std::vector<Row> rows;
  std::vector<size_t> ev(texts.size()),cl(texts.size());
  const size_t shard=1;
  for(size_t i=0;i<80000;++i){
    size_t name=i%7?rng()%texts.size():1;bool click=i%6==0,erase=!click && (i%11<3 || name==8);
    auto key=keys[name];if(i%9==0)key=pool.encode(texts[name],'u');
    Key k=click?book.template observe<true>(shard,key,i):book.template observe<false>(shard,key,i);
    rows.push_back({name,k,click,erase});
    if(click)++cl[name];else if(!erase)++ev[name];
  }
  std::array<std::jthread,8> workers;
  for(unsigned j=0;j<8;++j)workers[j]=std::jthread([&,j]{
    for(size_t i=j;i<rows.size();i+=8)if(rows[i].erase)book.discard(shard,rows[i].code);
  });
  for(auto &w:workers)w.join();
  std::vector<size_t> got_ev(texts.size()),got_cl(texts.size());
  for(auto i:book.parts[shard].occupied){
    auto &sl=book.get(shard,i);
    auto it=std::find(keys.begin(),keys.end(),sl.key);if(it==keys.end())std::abort();
    size_t name=it-keys.begin();got_ev[name]+=sl.events;got_cl[name]+=sl.clicks;
  }
  for(auto i:book.parts[shard].uncached_events)if(auto &r=rows[i];r.code.bits){
    if(!(r.code==keys[r.name])||book.counted(r.code.bits)||r.click)std::abort();
    ++got_ev[r.name];
  }
  for(auto i:book.parts[shard].uncached_clicks){
    auto &r=rows[i];
    if(!(r.code==keys[r.name])||book.counted(r.code.bits)||!r.click)std::abort();
    ++got_cl[r.name];
  }
  for(const auto &r:rows){
    if(r.erase!=!r.code.bits)std::abort();
    if(book.counted(r.code.bits)&&!(book.get(shard,r.code.bits>>2).key==keys[r.name]))std::abort();
  }
  if(ev!=got_ev || cl!=got_cl)std::abort();
}
int main(){exercise<1,1>();exercise<8,2>();exercise<128,4>();exercise<512,1>();
  puts("320000 observations matched, including forced collisions, equal indirect IDs, and eight-worker duplicate removal.");}
