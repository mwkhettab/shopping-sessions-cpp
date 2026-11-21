#include "events_packet12.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <sys/mman.h>
#include <unistd.h>
using namespace pipeline;
struct Guard {
    std::size_t page=std::size_t(::sysconf(_SC_PAGESIZE));
    u8* ptr=static_cast<u8*>(::mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    Guard(){assert(ptr!=MAP_FAILED);assert(!::mprotect(ptr+page,page,PROT_NONE));}
    ~Guard(){::munmap(ptr,2*page);}
};
int main() {
    Guard mem;
    std::vector<Rec> got(BSIZE),want(BSIZE),unpacked(BSIZE);
    std::mt19937_64 rng(97);std::size_t processed=0,escapes=0;
    auto ingest=[](Rec r){return (r.w1>>16)&ING_MAX;};
    for(u64 gen=1;gen<=337;++gen) {
        const std::size_t n=gen-1;assert(12*n<=mem.page);
        u8* p=mem.ptr+mem.page-12*n;
        std::vector<Rec> full;
        for(unsigned th=0;th<3;++th) {
            for(std::size_t i=0;i<n;++i) {
                const auto slot=(i%8<5?0:(i%8==5?BSIZE-1:rng()%BSIZE));
                const auto in=i%5==0?0:i%11==0?ING_MAX:rng()%ING_MAX;
                const auto u=(i%17==0?(u64{1}<<22)-1:rng()%300);
                const u64 alias=i%32,ts=i%13==0?INVALID_TS:rng()%INVALID_TS;
                const Rec row{ts<<33|((i&1)<<32)|alias,u<<37|in<<16|slot};
                const u64 front=row.w1|(alias<<59);const u32 high=u32(row.w0>>32);
                std::memcpy(p+12*i,&front,8);std::memcpy(p+12*i+8,&high,4);
                auto& expected=want[slot];
                if(u16(expected.w1)!=gen || ingest(row)<ingest(expected))
                    expected={row.w0,(row.w1&~u64{0xffff})|gen};
                if(i%9==0) {
                    const u64 esc_ing=i%3==0?0:i%7;
                    const Rec er{(rng()%INVALID_TS)<<33|((rng()&0xffffff)<<9)|(rng()&511),
                                 (u64{1}<<63)|((rng()%900)<<37)|(esc_ing<<16)|slot};
                    full.push_back(er);
                }
            }
            events::scatter_chunk12(p,n,got.data(),gen,nullptr);
            events::scatter12_scalar(p,n,unpacked.data(),gen);
            for(const auto er:full) {
                const auto slot=er.w1&(BSIZE-1);auto& x=want[slot];
                if(u16(x.w1)!=gen || ingest(er)<ingest(x)) x={er.w0,(er.w1&~u64{0xffff})|gen};
                events::scatter_record(er,got.data(),gen);events::scatter_record(er,unpacked.data(),gen);
            }
            processed+=n;escapes+=full.size();full.clear();
            assert(!std::memcmp(got.data(),want.data(),BSIZE*sizeof(Rec)));
            assert(!std::memcmp(unpacked.data(),want.data(),BSIZE*sizeof(Rec)));
        }
    }
    std::cout<<"guard-page shapes 0..336, packed "<<processed<<", escapes "<<escapes<<" matched all slots\n";
}
