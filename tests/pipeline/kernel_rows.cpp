#include "k_free.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
using namespace pipeline;
using Bytes=std::string_view;
using Row=std::array<u64,5>;
struct Rows {
    EvBatch eb;CkBatch cb;
    std::vector<Row> rows;
    std::vector<std::size_t> groups;
    EvBatch *ev(){return &eb;}
    CkBatch *ck(){return &cb;}
    void ev_flush() {
        groups.push_back(eb.n);
        for(std::size_t i=0;i<eb.n;++i) rows.push_back({eb.eid[i],eb.uid[i],u64(eb.ts[i]),u64(eb.ing[i]),eb.pp[i]});
        eb.n=0;
    }
    void ck_flush() {
        groups.push_back(cb.n);
        for(std::size_t i=0;i<cb.n;++i) rows.push_back({cb.cid[i],cb.uid[i],u64(cb.ts[i]),cb.camp[i],0});
        cb.n=0;
    }
};
template<bool Events,class K> void collect(Bytes data,Rows &r) {
    if constexpr (Events) K::events(data,r);
    else K::clicks(data,r);
}
template<bool Events> void check(Bytes data,const char *label) {
    Rows ref,pipe,chain;
    collect<Events,k_ref::Ref>(data,ref);
    if constexpr (Events) ref.ev_flush();else ref.ck_flush();
    collect<Events,k_free::Free>(data,pipe);
    collect<Events,k_free::FreeN>(data,chain);
    for(auto [which,a]:{std::pair{"pipe",&pipe},std::pair{"chain",&chain}}) {
        if(a->groups==ref.groups && a->rows==ref.rows) continue;
        auto e=std::mismatch(a->rows.begin(),a->rows.end(),ref.rows.begin(),ref.rows.end());
        auto row=e.first-a->rows.begin();
        std::fprintf(stderr,"%s %s %s row %td, got %zu expected %zu, %zu input bytes\n",
            label,(Events?"events":"clicks"),which,row,a->rows.size(),ref.rows.size(),data.size());
        if (e.first!=a->rows.end() && e.second!=ref.rows.end()) {
            for(auto v:*e.first) std::fprintf(stderr,"%016llx ",static_cast<unsigned long long>(v));
            std::fprintf(stderr,"\n");
            for(auto v:*e.second) std::fprintf(stderr,"%016llx ",static_cast<unsigned long long>(v));
            std::fprintf(stderr,"\n");
        }
        throw std::runtime_error("parser result mismatch");
    }
}
template<bool Events> void guarded(Bytes s) {
#if defined(__unix__) || defined(__APPLE__)
    auto page=std::size_t(sysconf(_SC_PAGESIZE)),length=std::max(page,((s.size()+page-1)/page)*page);
    auto base=static_cast<char *>(mmap(nullptr,length+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    if(base==MAP_FAILED || mprotect(base+page,length,PROT_READ|PROT_WRITE)) std::abort();
    for(auto offset:{std::size_t{0},length-s.size()}) {
        std::memcpy(base+page+offset,s.data(),s.size());
        check<Events>(Bytes(base+page+offset,s.size()),"guard pages");
    }
    munmap(base,length+2*page);
#else
    check<Events>(s,"bounded");
#endif
}
u64 seed=471672975;
std::size_t pick(std::size_t n) {seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed%n;}
const std::array<Bytes,26> times={"1700000000123","2000-02-29T23:59:59Z","2026-11-04T01:59:59+03:30",
    "2026-11-04T01:59:59-08:00","0001-01-01T00:00:00Z","0000-02-29T00:00:00Z","1900-02-29T00:00:00Z",
    "2024-02-29T24:00:00Z","2024-02-29T23:60:00Z","2024-02-29T23:59:60Z","","0","12345","123456789012345",
    "0000000000000000","1","9223372036854775808","9223372036854775807","000000000000000000000",
    "2024-01-01T23:59:59,00:00","hello","2024-01-01T23:59:59Z","2024-01-01T23:59:59+24:00",
    "2001-01-04T12:30:45Z","2012-01-04T14:40:13Z","2024-01-01T23:59:59+00:00"};
std::string wrap(Bytes s){return pick(4)?std::string(s):"\""+std::string(s)+"\"";}
std::string field(char prefix) {
    constexpr std::array<Bytes,12> vals={"0","a","1","5xy","z0jz","zab8fg00","99999999","01","","2=f","no space","ninechars"};
    return wrap(std::string(1,prefix)+std::string(vals[pick(vals.size())]));
}
std::string eventrow() {
    constexpr std::array<Bytes,6> kinds={"view","cart","purchase","open","","purchase"};
    constexpr std::array<Bytes,8> prods={"","p0","p1","p01","p12345678","p123456789","plain","p129"};
    constexpr std::array<Bytes,10> prices={"","0","01239","10000000","10000001","100000000","00000000013","-1","3.4","12"};
    return field('e')+","+field('u')+","+wrap(times[pick(times.size())])+","+wrap(times[pick(times.size())])+","+
        wrap(kinds[pick(kinds.size())])+","+wrap(prods[pick(prods.size())])+","+wrap(prices[pick(prices.size())]);
}
std::string clickrow() {
    constexpr std::array<Bytes,8> camps={"","c_0","c_1","c_8192","c_21928373","c_0192","other","c_9"};
    return field('k')+","+field('u')+","+wrap(camps[pick(camps.size())])+","+wrap(times[pick(times.size())]);
}
template<bool Event> void cases() {
    const std::string regular=Event?"eab705c83,u14b28a21,2024-05-02T23:34:21Z,2024-05-03T00:01:02Z,view,p359,\n":
        "kab705c83,u14b28a21,c_37,2024-05-02T23:34:21Z\n";
    std::string data;
    for(unsigned n:{0,1,2,63,64,65,72,128,220}) {
        data.clear();
        for(unsigned i=0;i<n;++i) data+=regular;
        guarded<Event>(data);
        if (!data.empty()) { data.pop_back();guarded<Event>(data); }
    }
    data.clear();
    for (int i=0;i<1100;++i) data+=regular;
    guarded<Event>(data);
    for(unsigned i=0;i<5000;++i) {
        auto s=Event?eventrow():clickrow();
        if(pick(9)==0) {
            constexpr std::array<char,6> mutations={',','"', '\n', '\0','!','\r'};
            auto pos=pick(s.size()+1);
            s.insert(s.begin()+static_cast<std::ptrdiff_t>(pos),mutations[pick(mutations.size())]);
        }
        if((i%97)==0) s.append(160,'x');
        data+=s+'\n';
    }
    guarded<Event>(data);
    std::string bursts;
    for(unsigned j=0;j<400;++j) {
        for(unsigned i=0;i<60;++i) bursts+=regular;
        bursts+=(Event?eventrow():clickrow())+'\n';
    }
    guarded<Event>(bursts);
    for(unsigned i=0;i<257;++i) {
        auto offset=pick(regular.size()),n=pick(280);
        check<Event>(Bytes(data).substr(offset,n),"truncated first and last lines");
    }
}
}
int main() {
    cases<true>();cases<false>();
    std::printf("Both CSV kernels agree with every reference row and 64-row flush. SIMD dispatch %s.\n",
                pipeline::k_free::detail::available()?"on":"off");
}
