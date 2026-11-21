#include "../../cpp/pipeline/model.hpp"
#include "../../cpp/pipeline/events_fast.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <tuple>
using namespace pipeline;
using namespace pipeline::events;
using K=std::array<u64,2>;
using Row=std::tuple<std::string,i32,i32,i32,i32,u8>;
std::vector<Row> rows(const core::Core& c) {
    std::vector<Row> out;
    for(std::size_t i=0;i<c.keys.len();++i) {
        const auto p=c.keys.as_ptr()[i],a=c.names_off.as_ptr()[p],b=c.names_off.as_ptr()[p+1];
        out.emplace_back(std::string(c.names.as_ptr()+a,c.names.as_ptr()+b),c.day.as_ptr()[i],
            c.ses.as_ptr()[i],c.evs.as_ptr()[i],c.rev.as_ptr()[i],c.cat.as_ptr()[i]);
    }
    std::sort(out.begin(),out.end());return out;
}
int main() {
#ifndef PIPELINE_EVENTS_NATIVE
    return 77;
#else
    if (!fast_cpu()) return 77;
    constexpr unsigned nusers=514,nc=21,nd=15;
    G g;g.nt=2;g.nc=nc;g.nd=nd;g.day0=5;g.ukey=720137;g.ws_mod=23009;g.row_cap=70000;g.ucap=nusers;g.ev_bytes=3*BSIZE*16;g.nb=3;
    g.ev_arena=sys::Arena(8<<20);
    g.us_arena=sys::Arena(24<<20);
    auto* base=g.us_arena.alloc(20<<20);
    std::memset(base,130,20<<20);
    std::array<u16,nusers> utab{};
    std::array<u32,nusers+1> off{};
    std::array<std::atomic<u64>,(nusers+63)/64> claims{};
    std::array<EdgeInfo,3> edges{};
    g.utab=utab.data();g.claim=claims.data();g.edges=edges.data();g.csr_off=off.data();
    for(unsigned th=0;th<g.nt;++th) {
        g.workers.emplace_back(new TState);
        auto& t=g.t(th);t.core=core::Core(nc,nd,g.row_cap,g.ws_mod,g.day0);
        t.ev=parse::Part(g.ev_arena,g.nb);
    }
    std::vector<K> csr;
    std::map<u32,std::vector<Rec>> records;
    for(unsigned u=0;u<nusers;u++) {
        off[u]=csr.size();
        const u32 cls=u%4==0?1:u%4==1?0x7f:u%4==2?0xff:0x3fff;
        utab[u]=u16(cls);if(!u) utab[u]|=EXTRA;
        const auto nclick=u%11==0?233u:16u;
        for(u32 j=0;j<nclick;++j) {
            const u32 ts=nclick>16 ? 400000000+j*500000 : 605000000+j*1000;
            const auto a=(u%11==0 && j%6==0)?11:0;
            csr.push_back({(u64(ts)<<32)|((j%nc)<<4)|a,1000-j});
        }
    }
    off.back()=csr.size();g.ncsr=csr.size();csr.resize(csr.size()+64);g.csr=csr.data();g.csr_cap=csr.size();
    auto interior=[](u32 slot){return slot<12000 ? 2+(slot-9000)/65 : 49+(slot-12000)/13;};
    const u32 seam=interior(BSIZE-1);
    std::vector<unsigned> eventcount(nusers);
    auto event=[&](u32 b,u32 slot,u32 u) {
        const u32 i=eventcount[u]++;
        const u32 ts=310000000+((u<=1 || u==seam)?i/100*2100003+(i%100)*100:u32(i*1900000))+u%2;
        const u32 alias=std::countr_zero(u32(utab[u]&MASK));const u32 cat=(i+3*u)%32,price=(i%4)?17+((i+u)%60):0;
        const u64 w=(u64(ts)<<33)|(u64(price)<<9)|(cat<<4)|alias;
        const u64 w1=(u64(u)<<37)|(3ull<<16)|slot;
        auto& t=g.t((slot^b)&1);t.ev.push(b,w,w1);++t.n_ev_rows;
        records[u].push_back({w,w1});
        if (i%47==0) {g.t(((slot^b)&1)^1).ev.push(b,0,(u64(100)<<37)|(5ull<<16)|slot);}
    };
    // Dense 16,384-slot bucket, one complete digest-only boundary bucket and a long
    // seam. Interior groups exercise both pending-work flush thresholds and all lanes.
    for(u32 slot=0;slot<BSIZE;++slot) event(0,slot,0);
    for(u32 slot=0;slot<BSIZE;++slot) {
        const u32 u=slot<4800?0:slot<9000?1:interior(slot);
        event(1,slot,u);
    }
    for(u32 slot=0;slot<4600;++slot) event(2,slot,slot<3500?seam:seam+1+(slot-3500)/60);
    const u64 extra=(u64(320000000)<<33)|(19u<<9)|(15u<<4);g.extra[0]={extra};
    for(unsigned t=0;t<g.nt;++t) g.t(t).ev.finish();
    Pool pool(g);
    auto make_share=[&](unsigned tid){
        const auto [d,h]=fast_sizes(g.nc,g.nd);
        const auto cells=(g.nc+2)*g.nd;Share s;
        s.dense=pool.take(r4k(d),4096);s.hot=pool.take(r4k(h),4096);
        s.agg=pool.take(r4k(cells*8),4096);s.aggp=pool.take(r4k(cells*16),4096);
        s.slab_len=2<<20;s.slab=pool.take(s.slab_len,4096);
        assert(s.slab);(void)tid;return s;
    };
    DIG_MIN=4096;
    auto s=make_share(0);stage3a(g,0,&pool,s);stitch(g);g.next=0;stage3b(g,1);
    assert((g.edges[0].first.len&DIG) && (g.edges[1].first.len&DIG));
    assert(g.t(0).core.names.in_slab() && g.t(0).core.day.in_slab());
    core::Core expected(nc,nd,g.row_cap,g.ws_mod,g.day0);
    for(auto& [u,rs]:records) {
        if(!u) {rs.push_back({extra,0});std::sort(rs.begin(),rs.end(),[](auto a,auto b){return a.w0<b.w0;});}
        const u32 klass=utab[u]&MASK;
        std::vector<K> selected;
        for(auto k:g.csr_range(u)) if ((klass>>(k[0]&15))&1) selected.push_back(k);
        expected.person(g,rs,selected,core::Name::Simple(u,klass));
    }
    auto got=rows(g.t(0).core),r2=rows(g.t(1).core);got.insert(got.end(),r2.begin(),r2.end());
    std::sort(got.begin(),got.end());assert(got==rows(expected));
    for(std::size_t i=0;i<(nc+1)*nd;++i)for(unsigned j=0;j<2;++j){
        assert(g.t(0).core.agg[i][j]+g.t(1).core.agg[i][j]==expected.agg[i][j]);
        assert(g.t(0).core.aggp[i][j]+g.t(1).core.aggp[i][j]==expected.aggp[i][j]);
    }
    std::cout<<2*BSIZE+4600<<" events, "<<g.ncsr<<" clicks, "<<got.size()<<" rows matched with recycled storage\n";
#endif
}
