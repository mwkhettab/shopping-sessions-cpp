#include "parse_links.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
using namespace pipeline;
struct TestPart {
    struct Row {u64 b,w0,w1;friend auto operator<=>(const Row&,const Row&)=default;};
    std::vector<Row> rows;
    std::array<u8,64> buf{};
    const u8* lines_ptr() const {return buf.data();}
    void push(u64 b,u64 w0,u64 w1) {rows.push_back({b,w0,w1});}
    void push_many(u64 mask,std::size_t,const u64* bs,const u64* ptr,const std::array<u64,2>* rec) {
        for(;mask;mask&=mask-1) {auto i=std::countr_zero(mask);assert(ptr[i]==std::uintptr_t(lines_ptr())+bs[i]*64);push(bs[i],rec[i][0],rec[i][1]);}
    }
};
struct State {
    std::vector<std::pair<u64,u32>> raws;std::vector<u8> rawbuf;
    std::vector<u64> odd_users;std::vector<OddEv> odd_events;
    std::vector<std::array<u64,3>> odd_clicks;
    std::vector<std::pair<u64,u64>> gen_edges;
    u64 n_ev_rows{},n_ck_rows{},n_lk_rows{};TestPart ev,us;
};
struct Model {
    i64 ing_base=1710000000000,ws=1710000000000,cbase=1710000000000;
    u64 wlen=86400000,clen=86400000,ukey=2173242,ekey=3194894,ckey=51997855,ucap=8221,ecap=35319,ccap=101;
    bool cpart=true;
    std::vector<u8> prod=std::vector<u8>(PBOUND+65,3),camp=std::vector<u8>(CBOUND+64,255);
    const u8* region{};std::array<u64,2>* csr{};
    std::optional<std::pair<u32,u32>> normal(u64 tok)const {
        auto v=keyrec::unscramble(tok,ukey);
        if(tok<=keyrec::MASK && (v>>9)<ucap && (v&511)<NALIAS) return std::pair{u32(v>>9),u32(v&511)};
        return {};
    }
};
static std::string name(u64 id){auto [w,n]=enc36(id);std::string s="u";for(std::size_t j=0;j<n;++j) s+=char(w>>(56-j*8));return s;}
int main(){
    Model g;g.camp[5]=1;std::string buf;
    auto user=[&](u32 u,u32 a){return keyrec::scramble((u64(u)<<9)|a,g.ukey);};
    auto quote=[](const std::string& s){return s.size()>=8 ? '"'+s+'"' : s;};
    for(int i=0;i<360;++i) {
        auto a=name(user(i*23%8120,2)),b=name(user(i%6?i*23%8120:7,3));
        if(i%11==0) b=a;
        if(i%19==0) a="raw-value";
        if(i%31==0) b="u001";
        if(i%53==0) a="u"+std::string(299,'q');
        buf+=quote(a)+','+quote(b)+'\n';
        if(i%23==0) buf+="u1,broken\"row\nu2,u3,extra\n,u4\n";
    }
    buf+="u11,u22";g.region=reinterpret_cast<const u8*>(buf.data());
    State a,b;EvBatch ea,eb; CkBatch ca,cb;parse::Out oa,ob;
    const auto& tb=parse::tabs(g);
    parse::Ctx c{g,a,ea,ca,oa,tb,0ull,0ull,u64(buf.size())};
    parse::Ctx ref{g,b,eb,cb,ob,tb,0ull,0ull,u64(buf.size())};
    parse::links_chunk(c,buf);
    k_ref::for_each_line(buf,[&](auto i,auto j){parse::link_slow(ref,buf,i,j);});
    auto normalize=[](State& t){std::vector<std::pair<std::string,std::string>> values;
        auto tok=[&](u64 v){if(!(v&RAW)) return name(v);auto [off,n]=t.raws[v&(RAW-1)];return std::string(reinterpret_cast<const char*>(t.rawbuf.data()+off),n);};
        for(auto [x,y]:t.gen_edges) values.emplace_back(tok(x),tok(y));std::sort(values.begin(),values.end());return values;};
    std::sort(a.us.rows.begin(),a.us.rows.end());std::sort(b.us.rows.begin(),b.us.rows.end());
    assert(a.n_lk_rows==b.n_lk_rows && a.n_lk_rows==361 && a.us.rows==b.us.rows && normalize(a)==normalize(b));
    for(std::size_t i=0;i<181;++i) {
        EvRow e{keyrec::scramble(i,g.ekey),user(u32(i%8),u32(i%4)),g.ws+i64(i),g.ws+i64(i)+5,KIND_PURCHASE,3,111};
        if(i%7==0) e.eid=odd_id(1,3); if(i%11==0) e.uid=odd_id(5,3); if(i%13==0) e.ts=TS_BAD;
        event(c,e); ref.ev_one(e);
        ClRow k{keyrec::scramble(i%100,g.ckey),user(u32(i%8),u32(i%4)),5,g.ws+i64(i)};
        if(i%7==0) k.cid=odd_id(1,3);if(i%17==0) k.uid=ID_EMPTY;
        click(c,k);ref.ck_one(k);
    }
    c.ev_flush();c.ck_flush();
    assert(a.n_ev_rows==181&&a.n_ck_rows==181&&a.odd_events.size()==b.odd_events.size()&&a.odd_users.size()==b.odd_users.size());
    for(auto* v:{&a.ev.rows,&b.ev.rows,&a.us.rows,&b.us.rows}) std::sort(v->begin(),v->end());
    assert(a.ev.rows==b.ev.rows && a.us.rows==b.us.rows && a.odd_clicks.size()==b.odd_clicks.size());
    std::puts("link row shapes, vector/scalar packs, deferred raw spills and both sink batches passed");
}
