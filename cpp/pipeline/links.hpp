#pragma once
#include "links_index.hpp"
#include <unordered_map>

namespace pipeline::links {
struct Tail {Uf uf;std::vector<Cx> cx;bool part1=true;};
inline std::unique_ptr<Tail> tail;
inline std::atomic_bool TAIL_TAKEN{true};
template<class G> void uf_tail(const G& g) {
    if(TAIL_TAKEN.exchange(true)||!tail)return;
    if(tail->part1){std::size_t n=0;for(std::size_t th=0;th<g.nt;++th){const auto& t=g.t(th);n+=2*t.gen_edges.size()+t.odd_users.size()/8+t.odd_clicks.size()/8;}
        tail->uf=Uf(n+1024);tail->uf.part1(g);tail->part1=false;}
    tail->uf.part2a(g);for(const auto& cx:tail->cx)tail->uf.cx(g,cx);tail->uf.finish(g);
}
template<class G> Uf uf_take(const G& g){uf_tail(g);auto t=std::move(tail);return t ? std::move(t->uf) : Uf();}
template<class G> void odd_events(G& g) {
    u64 odd=0,total=0;for(std::size_t th=0;th<g.nt;++th){const auto& t=g.t(th);odd+=t.odd_events.size();total+=t.n_ev_rows;}
    if(odd*50>total+5000)sys::fallback("more than 2% odd events");
    std::unordered_map<std::string,OddEv> unique;std::vector<u8> tmp;
    for(std::size_t th=0;th<g.nt;++th)for(const auto& ev:g.t(th).odd_events){
        g.id_str(ev.eid,'e',tmp);auto [p,inserted]=unique.try_emplace(std::string(tmp.begin(),tmp.end()),ev);
        if(!inserted && ev.ing<p->second.ing)p->second=ev;
    }
    g.extra.clear();g.odd_kept.clear();for(const auto& [id,ev]:unique){
        if((ev.w0>>33)==INVALID_TS)continue;
        if(ev.user&ODDU)g.odd_kept.push_back(ev);else{g.utab[ev.user]|=EXTRA;g.extra[u32(ev.user)].push_back(ev.w0);}
    }
    for(auto& [u,rows]:g.extra)std::sort(rows.begin(),rows.end());
}
template<class G> void prelude(const G& g,bool inq) {
    DEFER_RAW.store(inq);plan=std::make_unique<Plan>();
    if(!g.ccap)prelude_part(g);else if(g.cpart)prelude_a(g);else prelude_ix(g);
    READY.store(true,std::memory_order_release);
}
template<class G> void us_finished(const G& g,u64 end,bool inq) {
    auto n=CEND.load();while(n<end&&!CEND.compare_exchange_weak(n,end)){}
    if(US_FIN.fetch_add(1)+1==g.nt && inq)prelude(g,true);
}
template<class G,class T> inline bool job(const G& g,T& t,Scratch& sc) {
    if(!READY.load(std::memory_order_acquire))return false;
    return g.ccap ? job_index(g,t,sc) : job_part(g,t,sc);
}
template<class G> inline bool done(const G&) {
    if(!READY.load(std::memory_order_acquire))return false;
    auto& p=*plan;if(p.fin.load()!=p.normal.size())return false;
    for(auto& h:p.heavy)if(h->sort_fin.load()!=SORT_UNITS)return false;return true;
}
template<class G> void stage2(G& g) {
    if(!READY.load())prelude(g,false);
    if(!done(g))sys::run_par(g.nt,[&](std::size_t tid){auto& t=g.t(tid);Scratch sc;for(;;){if(job(g,t,sc))continue;if(done(g))break;sys::spin_loop();}});
    sys::lap("st2b");odd_events(g);
    DEFER_RAW.store(false);for(auto s:deferred)raw_ties(g,s);deferred.clear();
    auto p=std::move(plan);
    if(g.ccap && g.cpart){std::size_t end=0;g.csr_base.clear();g.csr_base.reserve(g.nub+1);
        for(auto [lo,hi]:p->range){if(hi>lo){if(lo<end)sys::fallback("click index order");end=hi;}g.csr_base.push_back(std::min(lo,end));}g.csr_base.push_back(end);g.ncsr=end;
    }else{g.ncsr=p->base[g.nub];g.csr_base=std::move(p->base);}
    if(g.ccap)for(const auto& x:p->fixes)fix_odd(g,x);
    tail=std::make_unique<Tail>();for(auto& h:p->heavy)tail->cx.push_back(std::move(h->cx));
    TAIL_TAKEN.store(false);sys::lap("st2a");
}
}
