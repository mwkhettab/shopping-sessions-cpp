#include "../../cpp/pipeline/input_release.hpp"
#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <mutex>
#include <thread>
#include <vector>
using namespace pipeline;
int main() {
    const std::array<FileInfo,3> files{{{Kind::Ev,2u<<20,8u<<20,0},{Kind::Ck,16u<<20,8u<<20,0},
                                       {Kind::Lk,32u<<20,8u<<20,0}}};
    std::vector<Chunk> q;
    for(unsigned file:{1u,2u,0u}) for(unsigned i=0;i<16;++i) q.push_back({file,i*524288u,(i+1)*524288u});
    for(std::size_t group:{2u,8u,128u}) for(std::size_t lag:{0u,7u,32u}) {
        parse::InputRelease r(q.size(),group,lag);
        std::mutex mutex;std::vector<std::pair<std::size_t,std::size_t>> ranges;
        std::array<std::thread,8> workers;std::barrier start{8};
        for(unsigned t=0;t<8;++t) workers[t]=std::thread([&,t]{
            start.arrive_and_wait();
            for(std::size_t i=t;i<q.size();i+=8) r.finish(files,q,q.size()-1-i,[&](auto a,auto b){
                assert(a<b);std::lock_guard lock(mutex);ranges.emplace_back(a,b);});
        });
        for(auto& t:workers)t.join();
        for(const auto& c:q) {
            auto a=files[c.file].off+c.a,e=files[c.file].off+c.b;
            assert(std::any_of(ranges.begin(),ranges.end(),[&](auto r){return r.first<=a && r.second>=e;}));
        }
        if(!lag && group==8) assert(ranges.size()==6);
    }
    parse::InputRelease original(48,1,32);assert(!original.grouped());
}
