#include "../../cpp/pipeline/model.hpp"
#include "../../cpp/pipeline/out.hpp"
#include <charconv>
#include <iostream>
using namespace pipeline;
int main(int argc,char** argv) {
    if(argc!=4) return 2;
    if (std::getenv("SESSION_OUTPUT_REGION")) sys::alloc_enable();
    const std::string shape=argv[1],mode=argv[2];const bool nonempty=shape=="filled";
    G g;g.nt=4;g.nd=3;g.nc=3;g.day0=20000;g.row_cap=32000;g.ukey=111;g.check=true;g.ucap=100;
    std::array<u16,100> users{};g.utab=users.data();
    std::array<u32,101> offsets{};g.csr_off=offsets.data();std::array<std::atomic<u64>,2> claims{};g.claim=claims.data();
    std::array<std::array<u64,2>,20> csr{};g.csr=csr.data();g.csr_cap=csr.size();
    g.channels={{'p','a','i','d'},{'e','a','r','n','e','d'}};
    g.cats={{'x'},{'y'},{'z'}};g.camp={0,1,0};
    for(std::size_t i=0;i<g.nt;++i) {
        g.workers.emplace_back(new TState);
        auto& c=g.t(i).core;c=core::Core(g.nc,g.nd,g.row_cap,0,g.day0);
        if (nonempty && i!=3) {
            // Exercise an old reader while a later person can force the active column home.
            const auto slab=std::size_t{1}<<20;
            events::slab_cols(c,sys::anon(slab),slab);
        }
    }
    if (nonempty) {
        for(unsigned i=0;i<11900;++i) {
            auto& c=g.t(i%3).core;
            const auto name="seed-"+std::to_string(i);
            const u64 e0=(u64(100000+i)<<33)|((i%3==0?25ULL:0)<<9)|((i%3)<<4);
            const u64 e1=(u64(DAY_MS+100000+i)<<33)|((i%2?11ULL:0)<<9)|(((i+1)%3)<<4);
            const std::array<Rec,2> ev={Rec{e0,0},Rec{e1,0}};
            const std::array<std::array<u64,2>,1> ck={std::array<u64,2>{(u64(WEEK_MS)<<32)|((i%3)<<4),12}};
            c.person(g,ev,ck,core::Name::Bytes({reinterpret_cast<const u8*>(name.data()),name.size()}));
        }
        for(unsigned u=0;u<60;++u) {
            g.utab[u]=1;
            g.t(u%g.nt).mixed_users.push_back(u);
            g.t((u+1)%g.nt).cx_raw.push_back({(u64(23000+u)<<33)|(10u<<9)|(2u<<4),u});
        }
        for(unsigned i=0;i<3;++i) events::slab_full(g.t(i).core);
    }
    core::COLS_PINNED.store(true);
    if(mode=="combined") out::stage45(g,argv[3]);
    else {complex::stage4(g);out::stage5(g,argv[3]);}
}
