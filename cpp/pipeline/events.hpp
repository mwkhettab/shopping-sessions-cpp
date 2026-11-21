#pragma once
#include "events_fast.hpp"
#include "links.hpp"
#include <charconv>

namespace pipeline::events {
inline constexpr bool UF_TAIL_IN_STAGE3=true;
inline std::optional<std::size_t> env_number(const char* var) {
    const auto p=std::getenv(var);
    if (!p) return {};
    std::string_view v=p;
    const auto front=v.find_first_not_of(" \t\r\n");
    if (front==std::string_view::npos) return {};
    v=v.substr(front,v.find_last_not_of(" \t\r\n")-front+1);
    std::size_t value{};
    const auto [end,err]=std::from_chars(v.data(),v.data()+v.size(),value);
    if (err!=std::errc{} || end!=v.data()+v.size()) return {};
    return value;
}
template<class G> std::vector<Share> shares(const G& g,Pool& pool,bool old) {
    std::vector<Share> v(g.nt);if (old) return v;
    const auto [dense,hot]=fast_sizes(g.nc,g.nd);
    const auto cells=(g.nc+2)*g.nd;
    for (auto& s:v) {
        s.dense=pool.take(r4k(dense),4096);s.hot=pool.take(r4k(hot),4096);
        s.agg=pool.take(r4k(cells*8),4096);s.aggp=pool.take(r4k(cells*16),4096);
    }
    std::size_t n=0;
    for (std::size_t i=0;i<g.nt;++i) n+=g.t(i).n_ev_rows;
    const auto want=r4k(n/g.nt*13/100*131/5)+16*4096;
    const auto left=pool.left();
    const auto slab=std::min(want,(left-std::min(left,EP_BYTES*g.nt))/g.nt)&~std::size_t{4095};
    if (slab>=1<<20) for(auto& s:v){s.slab=pool.take(slab,4096);s.slab_len=s.slab?slab:0;}
    return v;
}
#ifdef PIPELINE_EVENTS_NATIVE
PIPELINE_EV_ATTR inline void name_selftest(u64 ukey) {
    u64 x=0x123456789abcdefULL;
    auto next=[&]{x=x*6364136223846793005ULL+1442695040888963407ULL;};
    for(u64 i=0;i<600000;++i) {
        next();
        const auto u=u32(x>>24)&u32(ODDU-1),klass=i%3 ? std::max(1u,u32(x>>3)&u32(MASK)):1u<<((x>>8)%NALIAS);
        u64 key=i%2 ? (x>>7)&keyrec::MASK:ukey;
        if (i>=200000) {
            const auto j=i-200000;
            const u64 y=j<100000?j:j<200000?(POW36[1+j%7]+(j/7)%41-20)&keyrec::MASK:(x>>13)&(keyrec::MASK>>(j%40));
            const u64 v=(u64(u)<<9)|std::countr_zero(klass);
            key=keyrec::unshuffle(y)^((v*keyrec::A)&keyrec::MASK);
            if (keyrec::scramble(v,key)!=y) sys::fallback("name key self-test");
        }
        auto want=core::simple_name_key(key,u,klass);
        if (want!=fast_name(key,u,klass) || want!=fast_name_y(key,u,klass) || want!=fast_name_v(key,u,klass)) sys::fallback("name self-test");
        const auto b=name_key_scalar(key,u,klass);
        u64 out[4];put256(out,key_chars4(_mm256_set_epi64x(x,x>>9,0x10,b)));
        if (out[0]!=__builtin_bswap64(want.first) || (b&15)!=want.second) sys::fallback("name character self-test");
    }
    constexpr auto limit=POW36[8];
    for(u64 i=0;i<3000000;++i) {
        next();u64 norm;
        switch (i%4) {
            case 0: norm=(x>>20)%limit;break;
            case 1: norm=(((x>>20)%1679616)*1679616+(i/4)%3-1)%limit;break;
            case 2: norm=(((x>>20)%(limit/1296))*1296+(i/4)%3-1)%limit;break;
            default: norm=(limit-1-(i/4)%5000)>>((x>>50)%42);
        }
        u64 keys[4],out[4];
        for(unsigned j=0;j<4;++j) keys[j]=(((norm+j*7)%limit)<<4)|(1+(i+j)%8);
        put256(out,key_chars4(get256(keys)));
        for(unsigned j=0;j<4;++j) if (out[j]!=__builtin_bswap64(key_chars(keys[j]).first)) sys::fallback("digit self-test");
    }
    for(u64 i=0;i<1679616;++i) if (((i>>4)*207127>>24)!=i/1296 || (i<1296 && (i*1821>>16)!=i/36)) sys::fallback("digit division");
    for(u64 i=0;i<(1u<<22);++i) if ((i*6515625>>39)!=i/84375) sys::fallback("day division");
}
#endif
template<class G> void stage3(G& g) {
    g.next.store(0,relaxed);
    const auto* old3=std::getenv("XL_OLD3");
    const auto old=g.nd>16 || BB>15 || (old3 && std::string_view(old3)=="1") || !fast_cpu();
    if (auto v=env_number("XL_DIGMIN")) DIG_MIN.store(*v?*v:SIZE_MAX,relaxed);
#ifdef PIPELINE_EVENTS_NATIVE
    if (g.check && fast_cpu()) name_selftest(g.ukey);
#endif
    {
        Pool pool(g);auto sv=shares(g,pool,old);
        sys::run_par(g.nt,[&](std::size_t tid){
            if (tid+1==g.nt) links::uf_tail(g);
#ifdef PIPELINE_EVENTS_NATIVE
            if (!old) {stage3a(g,tid,&pool,sv[tid]);return;}
#endif
            stage3a_old(g,tid);
        });
    }
    g.uf=links::uf_take(g);
    const auto full=env_number("XL_SLABFULL").value_or(0);
    if (full==1) for(std::size_t i=0;i<g.nt;++i) slab_full(g.t(i).core);
    sys::lap("ev3a");stitch(g);sys::lap("stitch");
    g.next.store(0,relaxed);
    sys::run_par(g.nt,[&](std::size_t tid){stage3b(g,tid);});
    if (full==2) for(std::size_t i=0;i<g.nt;++i) slab_full(g.t(i).core);
    core::COLS_PINNED.store(true,relaxed);
}
}
