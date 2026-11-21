#include "../../cpp/pipeline/core.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
namespace pipeline::sys {
std::atomic<bool> CORE_NOTOUCH{true};
u8* anon(std::size_t size) {
    auto p=static_cast<u8*>(std::calloc(1,size));
    if (!p) throw std::bad_alloc{};
    return p;
}
[[noreturn]] void fallback(const char* why) { throw std::runtime_error(why); }
}
using namespace pipeline;
u64 read() { u64 x; if (!(std::cin>>x)) throw std::runtime_error("input"); return x; }
void raw(u64 n) { for (unsigned i=0;i<8;++i) std::cout.put(static_cast<char>(n>>(8*i))); }
int main() {
    struct Global {u64 ukey;};
    const Global g{read()};
    const auto nc=read(),nd=read(),ws=read(); const auto day=u32(read()); const auto np=read();
    core::Core c(nc,nd,20000,ws,static_cast<i32>(day));
    for (u64 p=0;p<np;++p) {
        const auto mode=read(); const auto person=u32(read()),mask=u32(read()); const auto ne=read(),nk=read();
        std::vector<Rec> evs(ne); std::vector<std::array<u64,2>> cks(nk);
        for (auto& r:evs) r={read(),0};
        for (auto& k:cks) k={read(),read()};
        const std::array<u8,6> b={'A',0,'o','d','d',255};
        const auto name=mask?core::Name::Simple(person,mask):core::Name::Bytes(b);
        if (mode==0) { c.person(g,evs,cks,name); continue; }
        auto st=c.begin(evs.front().w0);
        if (mode==1 || mode==3) {
            std::vector<u64> ws;
            for (const auto& r:evs) ws.push_back(r.w0);
            auto [bits,uns]=c.feed_slice(st,ws,evs.front().w0>>33);
            raw(bits); raw(uns);
            if (mode==3) { c.abort(st); continue; }
        } else if (mode==2) {
            std::vector<u64> sess,purch,extra;
            for(auto* v:{&sess,&purch,&extra}) {
                v->resize(read()); for(auto& x:*v) x=read();
            }
            std::size_t xi=0; raw(c.feed_digest(st,sess,purch,extra,xi)); raw(xi);
            for (;xi<extra.size();++xi) c.feed(st,extra[xi]);
        }
        c.finish(g,st,cks,name);
    }
    auto emit=[&](const auto& col) { raw(col.len()); for(auto x:col.as_slice()) raw(u64(x)); };
    raw(c.n_sess); emit(c.names_off); emit(c.names); emit(c.keys); emit(c.day); emit(c.ses);
    emit(c.evs); emit(c.rev); emit(c.cat);
    for(std::size_t i=0;i<c.agg.len();++i) {
        for(auto x:c.agg[i]) raw(x); for(auto x:c.aggp[i]) raw(x);
    }
}
