#pragma once
#include "model_types.hpp"
#include "keyrec.hpp"
#include <atomic>
#include <bit>
#include <cassert>
#include <cstring>
#include <span>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define PIPELINE_CORE_X86_64 1
#endif

namespace pipeline {
namespace sys {
u8* anon(std::size_t);
[[noreturn]] void fallback(const char*);
extern std::atomic<bool> CORE_NOTOUCH;
}
}

namespace pipeline::core {
struct Sess { u64 start; u32 day; u64 rev; };
struct St {
    u64 prev, s_start;
    u32 s_day;
    u64 s_rev;
    u32 d_s, d_e;
    u64 d_rev;
    u32 catmask;
    std::size_t mark;
    u32 pidx;
};
struct Name {
    u32 user, mask;
    std::span<const u8> bytes;
    bool simple;
    static Name Simple(u32 u, u32 c) { return {u, c, {}, true}; }
    static Name Bytes(std::span<const u8> b) { return {0, 0, b, false}; }
};

template<class T> class Tab {
    T* p_{};
    std::size_t n_{};
public:
    Tab() = default;
    explicit Tab(std::size_t n)
        : p_(reinterpret_cast<T*>(sys::anon(std::max<std::size_t>(n,1)*sizeof(T)))), n_(n) {}
    std::size_t len() const { return n_; }
    T* as_mut_ptr() { return p_; }
    const T* as_ptr() const { return p_; }
    void set_ptr(T* p) { p_ = p; }
    T& operator[](std::size_t i) {
        if (i >= n_) sys::fallback("aggregate index");
        return p_[i];
    }
    const T& operator[](std::size_t i) const {
        if (i >= n_) sys::fallback("aggregate index");
        return p_[i];
    }
};

inline std::atomic<bool> COLS_PINNED{false};
template<class T> class Col {
    T* p_{};
    std::size_t len_{}, cap_{};
    T* home_{};
    std::size_t home_cap_{};
public:
    Col() = default;
    explicit Col(std::size_t cap) : cap_(std::max<std::size_t>(cap,16)), home_cap_(cap_) {
        p_ = reinterpret_cast<T*>(sys::anon(cap_*sizeof(T)));
        home_ = p_;
    }
    void push(T value) {
        if (len_ >= cap_) grow(len_+1);
        p_[len_++] = value;
    }
    void extend(std::span<const T> s) {
        if (len_ + s.size() > cap_) grow(len_+s.size());
        if (!s.empty()) std::memcpy(p_+len_,s.data(),s.size_bytes());
        len_ += s.size();
    }
    std::size_t len() const { return len_; }
    std::size_t cap() const { return cap_; }
    std::span<const T> as_slice() const { return {p_,len_}; }
    T* as_mut_ptr() { return p_; }
    const T* as_ptr() const { return p_; }
    void clear() { len_ = 0; }
    void truncate(std::size_t n) { len_ = n; }
    void reserve_total(std::size_t n) { if (n > cap_) grow(n); }
    bool in_slab() const { return p_ != home_; }
    void slab_full() { if (in_slab()) cap_ = len_; }
    void set_slab(T* p, std::size_t cap) {
        if (in_slab() || len_ > cap || !cap || (reinterpret_cast<std::uintptr_t>(p) & 4095)) return;
        if (len_) std::memcpy(p,p_,len_*sizeof(T));
        p_ = p;
        cap_ = cap;
    }
    [[gnu::cold,gnu::noinline]] void grow(std::size_t n) {
        if (!in_slab() || n > home_cap_ || len_ > home_cap_) sys::fallback("avec full");
        const auto bytes = len_*sizeof(T);
        bool moved = false;
#if defined(__linux__) && defined(SYS_mremap)
        const auto pages = (bytes + 4095) & ~std::size_t{4095};
        if (pages && !COLS_PINNED.load(std::memory_order_relaxed))
            moved = reinterpret_cast<void*>(syscall(SYS_mremap,p_,pages,pages,3,home_)) == home_;
#endif
        if (!moved && bytes) std::memcpy(home_,p_,bytes);
        p_ = home_;
        cap_ = home_cap_;
    }
};

inline std::pair<u64,std::size_t> simple_name_key(u64 ukey, u32 u, u32 mask) {
    std::pair<u64,std::size_t> best{UINT64_MAX,0};
    while (mask) {
        const unsigned a = std::countr_zero(mask);
        mask &= mask-1;
        const auto e = enc36(keyrec::scramble((u64(u)<<9) | a,ukey));
        if (e.first < best.first) best=e;
    }
    return best;
}
template<class G> inline auto simple_name(const G& g, u32 u, u32 mask) {
    return simple_name_key(g.ukey,u,mask);
}

class Core {
    u64 ws_mod_{};
    std::int32_t day0_{};
    std::vector<Sess> sess_;
    std::vector<u32> stamp_;
    u32 serial_{};
    std::array<u64,32> catsum_{};
    std::vector<u64> cts_;
    std::vector<std::array<u64,2>> iv_;
    std::vector<u32> dstart_;
    u32 dayof(u64 t) const { return u32((t + ws_mod_) / DAY_MS); }
    void flush_row(u32 pidx, u32 d, u32 s, u32 e, u64 revenue, u32 mask) {
        u8 top = 255;
        u64 best = 0;
        while (mask) {
            const auto c = std::countr_zero(mask);
            mask &= mask-1;
            const auto v = catsum_[c];
            catsum_[c] = 0;
            if (v > best) { best = v; top = u8(c); }
        }
        if (revenue > INT32_MAX || e > INT32_MAX) sys::fallback("revenue_cents or events over i32");
        keys.push(pidx); day.push(day0_+std::int32_t(d)); ses.push(std::int32_t(s));
        evs.push(std::int32_t(e)); rev.push(std::int32_t(revenue)); cat.push(top);
    }
    void next_session(St& st, u64 t) {
        sess_.push_back({st.s_start+WEEK_MS,st.s_day,st.s_rev});
        const auto d = dayof(t);
        if (d != st.s_day) {
            flush_row(st.pidx,st.s_day,st.d_s,st.d_e,st.d_rev,st.catmask);
            st.d_s = 0; st.d_e = 0; st.d_rev = 0; st.catmask = 0; st.s_day = d;
        }
        ++st.d_s;
        st.s_start = t; st.s_rev = 0;
    }
    void purchase(St& st, u64 price, std::size_t c) {
        st.s_rev += price;
        st.d_rev += price;
        catsum_[c] += price;
        st.catmask |= u32{1} << c;
    }
    void assist_interval(std::size_t c, u64 a, u64 b) {
        auto lo = std::lower_bound(sess_.begin(),sess_.end(),a,[](const Sess& s,u64 t){ return s.start<t; });
        auto hi = std::upper_bound(lo,sess_.end(),b,[](u64 t,const Sess& s){ return t<s.start; });
        auto k = std::size_t(lo-sess_.begin()), end = std::size_t(hi-sess_.begin());
        while (k < end) {
            const auto d = sess_[k].day;
            const auto e = std::min<std::size_t>(dstart_[d+1],end);
            agg[c*nd+d][1] += u32(e-k);
            k=e;
        }
    }
    [[gnu::noinline]] void assists_big(std::span<const std::array<u64,2>> cks) {
        std::size_t i=0;
        for (std::size_t d=0;d<=nd;++d) {
            while (i<sess_.size() && sess_[i].day<d) ++i;
            dstart_[d]=u32(i);
        }
        dstart_[nd+1]=u32(sess_.size());
        if (iv_.size()<organic+2) iv_.resize(organic+2,{UINT64_MAX,0});
        cts_.clear();
        for (const auto& k : cks) {
            const auto c = std::size_t((k[0]>>4)&(CBOUND-1));
            const auto t = k[0]>>32;
            if (c>=iv_.size()) sys::fallback("campaign number");
            auto& interval=iv_[c];
            if (interval[0]!=UINT64_MAX && t<=interval[1]) {
                interval[1]=t+WEEK_MS;
            } else {
                if (interval[0]==UINT64_MAX) cts_.push_back(c);
                else assist_interval(c,interval[0],interval[1]);
                interval={t,t+WEEK_MS};
            }
        }
        for (auto c : cts_) {
            auto [a,b]=iv_[c]; iv_[c][0]=UINT64_MAX; assist_interval(c,a,b);
        }
    }
public:
    std::size_t nd{}, organic{};
    Tab<std::array<u32,2>> agg;
    Tab<std::array<u64,2>> aggp;
    Col<u32> names_off;
    Col<u8> names;
    Col<u32> keys;
    Col<std::int32_t> day,ses,evs,rev;
    Col<u8> cat;
    u64 n_sess{};
    Core() = default;
    Core(std::size_t nc,std::size_t days,std::size_t row_cap,u64 ws_mod,std::int32_t day0)
        : ws_mod_(ws_mod),day0_(day0),stamp_(nc+1),dstart_(days+2),nd(days),organic(nc),
          agg((nc+2)*days),aggp((nc+2)*days),names_off(row_cap+1),names(row_cap*10),
          keys(row_cap),day(row_cap),ses(row_cap),evs(row_cap),rev(row_cap),cat(row_cap) {
        if (sys::CORE_NOTOUCH.load(std::memory_order_relaxed)) names_off.truncate(1);
        else names_off.push(0);
    }
    St begin(u64 w0) {
        sess_.clear();
        const auto t=w0>>33;
        return {t,t,dayof(t),0,1,0,0,0,day.len(),u32(names_off.len()-1)};
    }
    void feed(St& st, u64 w) {
        const auto t=w>>33;
        if (t-st.prev>GAP_MS) next_session(st,t);
        st.prev=t;
        ++st.d_e;
        const auto price=(w>>9)&0xffffff;
        if (price) purchase(st,price,(w>>4)&31);
    }
    u32 feed_digest(St& st,std::span<const u64> sess,std::span<const u64> purch,
                    std::span<const u64> extra,std::size_t& xi) {
        std::size_t pi=0;
        u32 bits=0;
        for (std::size_t i=0;i<sess.size()/2;++i) {
            const auto w=sess[2*i], last=sess[2*i+1], t=w&u64(UINT32_MAX);
            if (last<t) sys::fallback("digest session");
            while (xi<extra.size() && (extra[xi]>>33)<t) {
                bits|=u32{1}<<(extra[xi]&15); feed(st,extra[xi++]);
            }
            if (t-st.prev>GAP_MS) next_session(st,t);
            const auto count=u32(w>>32);
            if (count>u32(INT32_MAX)-std::min(st.d_e,u32(INT32_MAX)))
                sys::fallback("revenue_cents or events over i32");
            st.d_e+=count;
            while (pi<purch.size() && (purch[pi]>>32)==i) {
                const auto p=purch[pi++]; purchase(st,(p>>5)&0xffffff,p&31);
            }
            while (xi<extra.size() && (extra[xi]>>33)<=last) {
                const auto e=extra[xi++];
                bits|=u32{1}<<(e&15);
                ++st.d_e;
                const auto price=(e>>9)&0xffffff;
                if (price) purchase(st,price,(e>>4)&31);
            }
            st.prev=last;
        }
        if (pi!=purch.size()) sys::fallback("digest purchases");
        return bits;
    }
    std::pair<u32,bool> feed_slice(St& st,std::span<const u64> ws,u64 sprev) {
#if defined(PIPELINE_CORE_X86_64)
        if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512dq"))
            return feed_slice_avx512(st,ws,sprev);
#endif
        u32 bits=0;
        bool uns=!ws.empty() && (ws.front()>>33)<sprev;
        for (auto w : ws) {
            bits|=u32{1}<<(w&15); uns|=(w>>33)<st.prev; feed(st,w);
        }
        return {bits,uns};
    }
#if defined(PIPELINE_CORE_X86_64)
    [[gnu::target("avx512f,avx512dq"),gnu::noinline]]
    std::pair<u32,bool> feed_slice_avx512(St& st,std::span<const u64> ws,u64 sprev) {
        std::size_t i=0;
        const auto gapv=_mm512_set1_epi64(GAP_MS), pm=_mm512_set1_epi64(0xffffffLL<<9);
        const auto one=_mm512_set1_epi64(1), m15=_mm512_set1_epi64(15);
        auto bv=_mm512_setzero_si512();
        u32 unsm=0;
        auto last=st.prev;
        auto prevv=_mm512_set1_epi64(static_cast<long long>(last));
        for (;i+8<=ws.size();i+=8) {
            const auto w=_mm512_loadu_si512(ws.data()+i), ts=_mm512_srli_epi64(w,33);
            const auto pv=_mm512_alignr_epi64(ts,prevv,7);
            const u32 gap=_mm512_cmpgt_epu64_mask(_mm512_sub_epi64(ts,pv),gapv);
            unsm|=_mm512_cmplt_epu64_mask(ts,pv);
            bv=_mm512_or_si512(bv,_mm512_sllv_epi64(one,_mm512_and_si512(w,m15)));
            u32 flags=gap|_mm512_test_epi64_mask(w,pm), done=0;
            while (flags) {
                const u32 k=std::countr_zero(flags);
                flags&=flags-1; st.d_e+=k-done;
                st.prev=k==0?last:ws[i+k-1]>>33; feed(st,ws[i+k]);
                done=k+1;
            }
            st.d_e+=8-done;
            last=ws[i+7]>>33; prevv=ts;
        }
        st.prev=last;
        u32 bits=u32(_mm512_reduce_or_epi64(bv));
        bool uns=unsm || (!ws.empty() && (ws.front()>>33)<sprev);
        for (;i<ws.size();++i) {
            auto w=ws[i]; bits|=u32{1}<<(w&15); uns|=(w>>33)<st.prev; feed(st,w);
        }
        return {bits,uns};
    }
#endif
    void abort(const St& st) {
        for (u32 m=st.catmask;m;m&=m-1) catsum_[std::countr_zero(m)]=0;
        keys.truncate(st.mark); day.truncate(st.mark); ses.truncate(st.mark);
        evs.truncate(st.mark); rev.truncate(st.mark); cat.truncate(st.mark);
    }
    template<class G> void finish(const G& g,const St& st,std::span<const std::array<u64,2>> cks,Name name) {
        sess_.push_back({st.s_start+WEEK_MS,st.s_day,st.s_rev});
        flush_row(st.pidx,st.s_day,st.d_s,st.d_e,st.d_rev,st.catmask);
        n_sess+=sess_.size();
        if (name.simple) {
            const auto [v,len]=simple_name(g,name.user,name.mask);
            std::array<u8,8> bytes{};
            for (unsigned i=0;i<8;++i) bytes[i]=u8(v>>(56-8*i));
            names.push('u'); names.extend(std::span<const u8>(bytes).first(len));
        } else names.extend(name.bytes);
        if (names.len()>UINT32_MAX) sys::fallback("names over 4 GB");
        names_off.push(u32(names.len()));
        if (cks.empty()) {
            const auto base=organic*nd;
            for (const auto& s : sess_) {
                const auto at=base+s.day;
                ++agg[at][0];
                if (s.rev) { ++aggp[at][0]; aggp[at][1]+=s.rev; }
            }
            return;
        }
        const bool small=sess_.size()*cks.size()<=4096;
        std::size_t lo=0,hi=0;
        for (const auto& s:sess_) {
            while (hi<cks.size() && (cks[hi][0]>>32)<=s.start) ++hi;
            while (lo<hi && (cks[lo][0]>>32)+WEEK_MS<s.start) ++lo;
            const auto c=hi>lo?std::size_t((cks[hi-1][0]>>4)&(CBOUND-1)):organic;
            const auto at=c*nd+s.day;
            ++agg[at][0];
            if (s.rev) { ++aggp[at][0]; aggp[at][1]+=s.rev; }
            if (small && hi>lo) {
                const auto serial=++serial_;
                for (auto i=lo;i<hi;++i) {
                    const auto c0=std::size_t((cks[i][0]>>4)&(CBOUND-1));
                    if (stamp_[c0]!=serial) { stamp_[c0]=serial; ++agg[c0*nd+s.day][1]; }
                }
            }
        }
        if (!small) assists_big(cks);
    }
    template<class G,class Rows> void person(const G& g,const Rows& records,
                                            std::span<const std::array<u64,2>> cks,Name name) {
        if (records.empty()) return;
        auto st=begin(records.front().w0);
        for (const auto& r:records) feed(st,r.w0);
        finish(g,st,cks,name);
    }
};
}
