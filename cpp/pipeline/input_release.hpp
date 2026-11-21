#pragma once
#include "model_types.hpp"
#include <atomic>
#include <memory>
#include <span>

namespace pipeline::parse {
#ifndef SHOP_INPUT_RELEASE_GROUP
#define SHOP_INPUT_RELEASE_GROUP 8
#endif
#ifndef SHOP_INPUT_ADVICE
#define SHOP_INPUT_ADVICE 2
#endif
class InputRelease {
    std::size_t group_,lag_;
    std::unique_ptr<std::atomic_bool[]> done_;
    std::atomic<std::size_t> front_{};
public:
    InputRelease(std::size_t n,std::size_t group,std::size_t lag)
        :group_(std::max<std::size_t>(1,group)),lag_(lag),
         done_(group_>1?std::make_unique<std::atomic_bool[]>(n):nullptr) {}
    bool grouped() const {return group_>1;}
    template<class Release> void finish(std::span<const FileInfo> files,std::span<const Chunk> q,
                                         std::size_t i,Release&& release) {
        const auto bounds=[&](std::size_t j){
            const auto& c=q[j];const auto& f=files[c.file];
            return std::pair{f.off+c.a,f.off+std::min(c.b,f.len)};
        };
        done_[i].store(true,std::memory_order_release);
        auto f=front_.load(std::memory_order_acquire);
        if(lag_ && i>=f && i-f>=lag_) {
            auto [a,b]=bounds(i);if(b>a) release(a,b);
        }
        while(f<q.size()) {
            const auto end=f+std::min(group_,q.size()-f);
            for(std::size_t j=f;j<end;++j)
                if(!done_[j].load(std::memory_order_acquire)) return;
            if(!front_.compare_exchange_weak(f,end,std::memory_order_acq_rel,std::memory_order_acquire)) continue;
            auto [a,b]=bounds(f);
            for(std::size_t j=f+1;j<end;++j) {
                auto [start,stop]=bounds(j);
                if(start>=b && start-b<=4u*1024*1024) b=stop;
                else {if(b>a) release(a,b);a=start;b=stop;}
            }
            if(b>a) release(a,b);
            f=end;
        }
    }
};
}
