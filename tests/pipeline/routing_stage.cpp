#include "model.hpp"
#include "links.hpp"
#include <cassert>
#include <cstdio>
#include <string_view>
using namespace pipeline;
int main(int argc,char** argv){
    const auto mode=argc>1?std::atoi(argv[1]):0;
    const auto heavy=argc>2&&argv[2][0]=='h';
    const auto overlap=argc>2&&std::string_view(argv[2])=="overlap";
    const auto reversed=argc>2&&std::string_view(argv[2])=="heavyswap";
    G g;g.nt=3;g.nub=3;g.ucap=3*UBSIZE;g.ukey=30653;g.ckey=12340443;g.clen=300000000;
    const std::size_t n=heavy?133700:211;
    g.ccap=mode?n+500:0;g.cpart=mode==2;g.csr_cap=n+560;g.us_arena=sys::Arena(32u<<20);
    std::vector<u16> tab(g.ucap);std::vector<u32> off(g.ucap+g.nub);std::vector<links::Click> csr(g.csr_cap);
    g.utab=tab.data();g.csr=csr.data();g.csr_off=off.data();
    for(std::size_t i=0;i<g.nt;++i){g.workers.push_back(std::make_unique<TState>());g.t(i).us=parse::Part(g.us_arena,g.nub);}
    auto user_id=[&](u32 u,u32 a){return keyrec::scramble((u64(u)<<9)|a,g.ukey);};
    std::vector<std::vector<links::Click>> expected(g.ucap);
    u64 maxid=0;
    auto add=[&](u32 user,u32 alias,u32 camp,u32 ts,u32 i,u32 th){
        auto id=keyrec::scramble(i,g.ckey),w=(u64(ts)<<32)|(u64(camp)<<4)|alias;maxid=std::max(maxid,u64(i)+1);
        expected[user].push_back({w,id});
        if(mode==1){ckix::store_pair(csr[i],w,u64(user)|ckix::VALID);}
        else if(mode==2)g.t(th).us.push_indexed(user>>UB,i|((u64(user)&(UBSIZE-1))<<43),w);
        else g.t(th).us.push(user>>UB,id|((u64(user)&(UBSIZE-1))<<43),w);
    };
    for(std::size_t i=0;i<n;++i){if(overlap && i==5)continue;const auto u=reversed && i==n/2 ? 1u : u32(i*21/n+1);add(u,1,u32(i%13),u32((i*107)%45000),u32(i),u32(i%g.nt));}
    for(std::size_t i=0;i<190;++i)add(u32(UBSIZE*2+3+i/12),1,u32(i%11),u32(667-i*3),u32(n+i),u32(i%g.nt));
    if(overlap)add(UBSIZE+7,2,9,33,5,0);
    auto edge=[&](u32 user,u32 a,u32 b,u32 t){g.t(t).us.push(user>>UB,(u64{1}<<63)|((u64(user)&(UBSIZE-1))<<43)|(u64(a)<<4)|b,0);};
    edge(2,0,1,0);edge(2,1,2,1);edge(3,0,2,0);edge(3,3,4,1);edge(7,1,2,2);
    g.t(0).gen_edges.emplace_back(user_id(7,2),user_id(8,4));
    g.t(0).odd_events.push_back({1,100,7,2});g.t(2).odd_events.push_back({1,95,5,2});
    g.t(1).odd_events.push_back({2,10,UINT64_MAX,2});
    for(std::size_t i=0;i<g.nt;++i){g.t(i).us.finish();links::us_finished(g,maxid,true);}
    links::stage2(g);g.uf=links::uf_take(g);
    assert((tab[2]&MASK)==7 && (tab[2]&EXTRA) && (tab[3]&COMPLEX) && (tab[7]&COMPLEX));
    assert(g.extra.at(2)==std::vector<u64>{5});
    assert(links::resolve_normal(g,7,1)==links::resolve_normal(g,8,4));
    assert(links::resolve_normal(g,3,3)==links::resolve_normal(g,3,4));
    assert(links::resolve_normal(g,3,0)!=links::resolve_normal(g,3,3));
    for(std::size_t u=0;u<g.ucap;++u){auto a=g.csr_range(u32(u));auto b=expected[u];assert(a.size()==b.size());
        for(std::size_t i=1;i<a.size();++i)assert((a[i-1][0]>>32)<=(a[i][0]>>32));
        std::vector<links::Click> c(a.begin(),a.end());std::sort(c.begin(),c.end());std::sort(b.begin(),b.end());assert(c==b);}
    std::printf("%s, mode %d passed histogram, CSR lookup, heavy sorting, canonical graph and odd-event winners\n",heavy?"heavy":"normal",mode);
}
