#include "../../cpp/pipeline/sys.hpp"
#include <cassert>
#include <numeric>
#include <random>
using namespace pipeline;
int main(int argc,char **argv) {
    using namespace sys;
    if (argc==2 && std::string_view(argv[1])=="--generic") return 0;
    if (argc==2 && std::string_view(argv[1])=="retry") {
        exec_init(argc,argv,"","",1); fallback("test retry");
    }
    U64Map m;
    for (unsigned i=0;i<20000;++i) assert(m.get_or_insert(i*11993ULL,i)==i);
    for (unsigned i=0;i<20000;++i) assert(m.get(i*11993ULL)==i && m.get_or_insert(i*11993ULL,888)==i);
    assert(!m.get(3));
    alloc_enable(); std::pmr::set_default_resource(&region_resource);
    std::pmr::vector<int> owned(190000); std::iota(owned.begin(),owned.end(),0);
    assert(owned.back()==189999);
    for (auto n:{32u,4096u,65000u,200000u}) {
        void *p=RegionResource::get(n,64,false); std::memset(p,31,n);
        RegionResource::deallocate_block(p,n,64);
        void *q=RegionResource::get(n,64,true);
        assert(a_mine(q) && q==p && !static_cast<u8*>(q)[n-1]);
        RegionResource::deallocate_block(q,n,64);
    }
    Arena arena(2*HP); AVec<u64> v(96);
    const std::array<u64,3> tail{2,3,4};v.push(1);v.extend(tail);
    assert(v.len()==4 && v.as_slice()[3]==4);
    std::array<std::atomic<unsigned>,8> visits{};
    auto exercise=[&](unsigned n){
        run_par(n,[&](std::size_t t){
            ++visits[t];auto *p=arena.alloc(64);std::memset(p,int(t+1),64);
        });
    };
    exercise(8); pool_start(8);
    for (unsigned i=0;i<200;++i) exercise(i%8+1);
    for (unsigned t=0;t<8;++t) assert(visits[t]==1+25*(8-t));
    assert(arena.used()==(8+25*36)*64);
    ::_exit(0);
}
