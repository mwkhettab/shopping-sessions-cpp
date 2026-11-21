#include "model.hpp"
#include "parse.hpp"
#include <cassert>
#include <cstdio>
#include <string>
using namespace pipeline;
static std::string id(u8 prefix,u64 y){auto [w,n]=enc36(y);std::string s(1,char(prefix));for(std::size_t i=0;i<n;++i)s+=char(w>>(56-8*i));return s;}
int main(int argc,char**){
    G g;g.nt=3;g.nub=2;g.ucap=UBSIZE*2;g.nb=2;g.ecap=BSIZE*2;g.ukey=78122004;g.ekey=9013377;
    g.nd=1;g.nc=1;g.row_cap=128;g.ws=k_ref::parse_ts("2024-04-05T00:00:00Z");g.ing_base=g.ws;g.cbase=g.ws;g.wlen=DAY_MS;g.clen=DAY_MS;
    g.prod.assign(PBOUND+65,0);g.camp.assign(CBOUND+64,255);g.camp[5]=0;
    g.ev_arena=sys::Arena(32u<<20);g.us_arena=sys::Arena(32u<<20);
    std::vector<u16> ut(g.ucap);std::vector<u32> off(g.ucap+g.nub);std::vector<links::Click> csr(5024);g.csr=csr.data();g.csr_off=off.data();g.csr_cap=csr.size();g.utab=ut.data();
    for(std::size_t i=0;i<g.nt;++i)g.workers.push_back(std::make_unique<TState>());
    std::array<std::string,3> data{"id,user,ts,ingest,kind,product,price\n","id,user,campaign,time\n","a,b\n"};
    std::array<std::size_t,3> headers{data[0].size(),data[1].size(),data[2].size()};
    constexpr auto ts="2024-04-05T00:01:00Z";auto user=[&](u32 u,u32 a){return id('u',keyrec::scramble((u64(u)<<9)|a,g.ukey));};
    for(std::size_t i=0;i<9297;++i)data[0]+=id('e',keyrec::scramble(i,g.ekey))+','+user(u32(i/3),0)+','+ts+','+ts+",view,p1,\n";
    for(std::size_t i=0;i<4773;++i)data[1]+=id('k',i)+','+user(u32(i/3),0)+",c_5,"+ts+'\n';
    for(std::size_t i=0;i<71;++i)data[2]+=user(i,0)+','+user(i,1)+'\n';
    std::string joined;
    for(std::size_t i=0;i<3;++i){data[i].pop_back();g.files.push_back({Kind(i),joined.size(),data[i].size(),headers[i]});joined+=data[i];g.queue.push_back({u32(i),headers[i],data[i].size()});
        if(argc>1){auto* f=std::tmpfile();assert(f);assert(std::fwrite(data[i].data(),1,data[i].size(),f)==data[i].size());std::fflush(f);int fd=::dup(::fileno(f));std::fclose(f);parse::keep_fd(fd);}}
    g.region=reinterpret_cast<const u8*>(joined.data());parse::RELEASE=false;parse::CORE_READY=true;
    sys::run_par(g.nt,[&](auto tid){parse::stage1(g,tid);});links::stage2(g);
    u64 ev=0,ck=0,lk=0;std::size_t kept=0;for(auto& ptr:g.workers){ev+=ptr->n_ev_rows;ck+=ptr->n_ck_rows;lk+=ptr->n_lk_rows;for(std::size_t b=0;b<g.nb;++b)kept+=ptr->ev.count(b);}
    assert(ev==9297 && kept==ev && ck==4773 && lk==71);for(std::size_t u=0;u<71;++u)assert((ut[u]&MASK)==3);
    for(std::size_t u=0;u<1591;++u)assert(g.csr_range(u).size()==3);
    std::puts("parallel line-aligned chunk scheduling, sink counts, linked CSR and no-final-LF passed");
}
