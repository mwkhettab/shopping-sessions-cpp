#include "../../cpp/pipeline/parse_part.hpp"
#include <cassert>
#include <cstdlib>
#include <new>
#include <random>

using namespace pipeline;
struct Arena {
    std::vector<void*> allocated;
    u8* alloc(std::size_t n) {
        void* p = std::aligned_alloc(parse::CHUNK, n);
        if (!p) throw std::bad_alloc();
        allocated.push_back(p);
        return static_cast<u8*>(p);
    }
    ~Arena() { for (auto p : allocated) std::free(p); }
};

int main() {
    for (bool nt : {false,true}) for (std::size_t buckets : {1, 19, 522}) {
        Arena arena;
        auto part = parse::Part::new_unbound(buckets);
        part.set_arena(arena);
        parse::NT = nt;
        std::vector<std::vector<std::array<u64, 2>>> expected(buckets);
        std::mt19937_64 rng(52791 + buckets);
        for (unsigned step = 0; step < 900; ++step) {
            auto n = step % 65;
            u64 mask = n == 64 ? UINT64_MAX : (u64{1} << n) - 1;
            if (step % 3 == 0) mask &= rng();
            alignas(64) std::array<u64, 2> records[64]{};
            u64 offsets[64]{}, pointers[64]{};
            for (unsigned j=0;j<n;++j) {
                const auto b = rng()%buckets;
                offsets[j] = b;
                pointers[j] = reinterpret_cast<std::uintptr_t>(part.lines_ptr()+parse::PART_STAGE_BYTES*b);
                records[j] = {rng(),rng()};
                if ((mask >> j) & 1) expected[b].push_back(records[j]);
            }
            part.push_many(mask,n,offsets,pointers,records);
            const auto b = rng()%buckets;
            const std::array<u64,2> pair{rng(),rng()};
            part.push(b,pair[0],pair[1]);
            expected[b].push_back(pair);
        }
        part.finish();
        for (std::size_t b=0;b<buckets;++b) {
            assert(part.count(b)==expected[b].size());
            std::size_t seen=0;
            part.for_each(b,[&](std::span<const Rec> rows){
                assert(!rows.empty() && rows.size()<=parse::LINK_OFF/sizeof(Rec));
                for(auto item:rows){
                    assert(item.w0==expected[b][seen][0] && item.w1==expected[b][seen][1]);
                    ++seen;
                }
            });
            assert(seen==expected[b].size());
        }
        part.for_each(buckets,[](auto){std::abort();});
    }
}
