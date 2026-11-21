#include "../../cpp/pipeline/events_runs.hpp"
#ifdef SESSION_TEST_FAST
#include "../../cpp/pipeline/events_fast.hpp"
#endif
#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>

using namespace pipeline;
using namespace pipeline::events;
struct Partition {
    std::array<std::vector<Rec>,5> buckets;
    template<class F> void for_each(std::size_t b,F f) const {if (!buckets[b].empty()) f(std::span(buckets[b]));}
    auto count(std::size_t b) const {return buckets[b].size();}
    template<class F> void for_each_chunk(std::size_t b,F f) const {
        const auto& data=buckets[b];
        for (std::size_t i=0;i<data.size();i+=252) {
            const auto n=std::min<std::size_t>(252,data.size()-i);
            const auto* p=data.data()+i;
            f(p,n,reinterpret_cast<const i8*>(i+n==data.size()?p:p+n));
        }
    }
};
struct Worker {
    Partition ev;
    std::vector<Rec> slots,cx_raw,runbuf;
    std::vector<u32> mixed_users;
    std::vector<std::array<u64,2>> ck;
    core::Core core{6,13,20000,0,10};
    u64 n_runs{},n_simple{},n_complex{},n_odd{},n_ev_rows{};
    std::array<u64,8> prof{};
};
struct Model {
    std::size_t nt{2},nb{5},nc{6},nd{13},row_cap{20000},ev_bytes{3000000};u64 ukey{823},ws_mod{};i32 day0{10};
    mutable std::atomic<std::size_t> next{};
    std::array<u16,100> utabs{}; u16* utab=utabs.data();
    mutable std::array<std::atomic<u64>,2> claims{};std::atomic<u64>* claim=claims.data();
    mutable std::array<EdgeInfo,5> einfo{};EdgeInfo* edges=einfo.data();
    mutable std::array<Worker,2> worker{};
    std::vector<std::vector<Seg>> joined;
    std::unordered_map<u32,std::vector<u64>> extra;
    struct Arena {
        void release_part(std::size_t,std::size_t) const {}
        std::size_t used() const {return 0;}
        u8* alloc(std::size_t) const {return nullptr;}
    } us_arena,ev_arena;
    std::array<std::vector<std::array<u64,2>>,100> csr;
    Worker& t(std::size_t tid) const {return worker[tid];}
    std::span<const std::array<u64,2>> csr_range(u32 u) const {return csr[u];}
};
using Row=std::tuple<std::string,i32,i32,i32,i32,u8>;
auto rows(const core::Core& c) {
    std::vector<Row> got;
    for(std::size_t i=0;i<c.day.len();++i) {
        auto p=c.keys.as_ptr()[i],a=c.names_off.as_ptr()[p],b=c.names_off.as_ptr()[p+1];
        got.emplace_back(std::string(reinterpret_cast<const char*>(c.names.as_ptr()+a),b-a),
            c.day.as_ptr()[i],c.ses.as_ptr()[i],c.evs.as_ptr()[i],c.rev.as_ptr()[i],c.cat.as_ptr()[i]);
    }
    std::sort(got.begin(),got.end());
    return got;
}
inline u64 packed(u32 t,u32 a,u32 pr=0,u32 cat=0){return (u64(t)<<33)|(u64(pr)<<9)|(cat<<4)|a;}
int main() {
    Model g;
    for (auto& tv:g.utabs) tv=3;
    g.utab[8]=COMPLEX;g.utab[5]|=EXTRA;
    g.extra[5]={packed(86100000,0,1,1),packed(87500000,0,101,1)};
    for (u32 u=0;u<g.csr.size();++u) {
        const u32 n=u==3?19:u%7,t0=650000000;
        for(u32 i=0;i<n;++i) g.csr[u].push_back({(u64(t0+i*800000)<<32)|u64((i%5)<<4)|(u==4?4:i%2),u64(n-i+1)});
    }
    std::map<std::pair<u32,u32>,Rec> expected;
    auto add=[&](u32 b,u32 slot,u32 u,u32 ing,u64 w,bool odd=false){
        const Rec r{w,(u64(odd)<<63)|(u64(u)<<37)|(u64(ing)<<16)|slot};
        g.worker[(slot+ing)%2].ev.buckets[b].push_back(r);
        ++g.worker[(slot+ing)%2].n_ev_rows;
        auto [it,ins]=expected.try_emplace({b,slot},r);if (!ins && ((it->second.w1>>16)&ING_MAX)>ing) it->second=r;
    };
    using std::size_t;
    std::mt19937 rd(413);
    for(u32 b:{0u,1u,3u,4u}) {
        for (u32 i=0;i<950;++i) {
            const auto u=b==0 ? (i<7?1: i<200?2:i<500?3:4):
                b==1 ? (i<10?4:i<400?5:i<540?6:i<820?7:8):
                b==3 ? (i<99?8:i<380?9:i<799?10:11):(i<173?11:12);
            u32 t=(b==1 && u==5)?u32(86000000+i*3500):u32(670000000+b*25000000+i*3700);
            if (i%123==0 && u==7) t-=500000;
            const auto slot=i*16+(i%7),a=(u==9 && i%6==0)?3:(i%2);
            const auto w=packed(t,a,i%9==0?u32(rd()%750+1):0,i%9==0?u32(rd()%32):0);
            if (i%7==0) add(b,slot,89,11,packed(t+600,1,12,2));
            add(b,slot,u,2,w);
            if (i%49==0) add(b,slot,u,1,packed(INVALID_TS,0));
            if (i==400 && b==0) add(b,slot+1,11,2,w,true);
        }
    }
    for(auto& t:g.worker) for (auto& records:t.ev.buckets) std::shuffle(records.begin(),records.end(),rd);
#ifdef SESSION_TEST_FAST
#ifdef PIPELINE_EVENTS_NATIVE
    if (!fast_cpu()) {std::cerr<<"AVX512CD/DQ/VL/BW unavailable\n";return 77;}
    DIG_MIN=64;Pool pool(g);stage3a(g,0,&pool,{});
#else
    std::cerr<<"AVX512CD/DQ/VL/BW unavailable\n";return 77;
#endif
#else
    stage3a_old(g,0);
#endif
    stitch(g);g.next=0;stage3b(g,1);
    Worker oracle;Model ref;
    ref.utabs=g.utabs;ref.csr=g.csr;ref.extra=g.extra;
    std::map<u32,std::vector<Rec>> byuser;
    std::vector<std::pair<u64,u64>> complex,from,odd;
    for(auto [key,r]:expected) {
        if ((r.w0>>33)==INVALID_TS) continue;
        if (r.w1>>63) odd.emplace_back(ODDU|user_of(r),r.w0);
        else byuser[user_of(r)].push_back(r);
    }
    for(auto& [u,records]:byuser) process_run(ref,oracle,u,records);
    auto got=rows(g.worker[0].core),b=rows(g.worker[1].core);got.insert(got.end(),b.begin(),b.end());std::sort(got.begin(),got.end());
    assert(got==rows(oracle.core));
    for(size_t i=0;i<(g.nc+1)*g.nd;++i) for(size_t f=0;f<2;++f) {
        assert(g.worker[0].core.agg[i][f]+g.worker[1].core.agg[i][f]==oracle.core.agg[i][f]);
        assert(g.worker[0].core.aggp[i][f]+g.worker[1].core.aggp[i][f]==oracle.core.aggp[i][f]);
    }
    for(auto& worker:g.worker) for(auto r:worker.cx_raw) from.emplace_back(r.w1,r.w0);
    for(auto r:oracle.cx_raw) complex.emplace_back(r.w1,r.w0);
    complex.insert(complex.end(),odd.begin(),odd.end());std::sort(complex.begin(),complex.end());std::sort(from.begin(),from.end());assert(complex==from);
    assert(got.size()>0 && !complex.empty());
    auto edge=Edge::start(2,nullptr);
    struct Scratch {Edge* edge;u32* p_sid;u32* p_w;};
    u32 ps[]={0,1,1,2},pw[]={90,80,70,60},si[]={FLAG,3,7,9|FLAG},ss[]={3,14,24};
    u64 q[]={3|(4ULL<<32),4,10,14,18,19,20,24,27};
    Scratch scratch{edge,ps,pw};
    auto dig=digest_get(digest_put(2,scratch,{false,14,3,9,1,3},q,si,ss,4));
    assert(dig.first==14 && dig.last==27 && dig.count==6 && dig.bits==1);
    assert(dig.sess[0]==((4ULL<<32)|14) && dig.sess[1]==20 && dig.sess[2]==((2ULL<<32)|24));
    assert(dig.purch[0]==80 && dig.purch[1]==70 && dig.purch[2]==((1ULL<<32)|60));
    std::cout<<expected.size()<<" unique slots, "<<got.size()<<" daily rows, "<<complex.size()<<" exceptional events matched\n";
}
