#pragma once
#include "model_types.hpp"

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#ifdef __linux__
#include <sys/mman.h>
#endif

namespace pipeline::parse {
inline constexpr std::size_t CHUNK = 4096;
inline constexpr std::size_t LINK_OFF = 4032;
inline constexpr std::size_t BLOCK = 2u << 20;
inline bool NT = true, RELEASE = true, PREFETCH = false;

inline bool popw() {
    static const bool value = [] {
        const char* setting = std::getenv("XL_POPW");
        return !setting || std::string_view(setting) != "0";
    }();
    return value;
}

struct alignas(64) Line { std::array<Rec, 4> rows; };

class Part {
    struct alignas(64) Page {
        Rec rows[LINK_OFF / sizeof(Rec)];
        Page* next;
    };
    static_assert(sizeof(Line) == 64 && sizeof(Page) == CHUNK);
    static_assert(offsetof(Page, next) == LINK_OFF);
    std::vector<Line> lines_;
    std::vector<u8> fill_;
    std::vector<std::uintptr_t> cur_;
    std::vector<u32> nch_, n_;
    std::vector<Page*> head_;
    void* arena_{};
    u8* (*allocate_)(void*, std::size_t){};
    u8* block_{};
    u8* block_end_{};
    bool populate_{};
    std::vector<u64> pend_;
    std::vector<std::pair<std::size_t,std::size_t>> ordinal_bounds_;

    explicit Part(std::size_t nb)
        : lines_(nb), fill_(nb + 64), cur_(nb, LINK_OFF),
          nch_(nb), n_(nb), head_(nb), populate_(popw()),
          pend_(72 * sizeof(Line) / sizeof(u64)) {}

    [[gnu::noinline]] std::uintptr_t new_chunk(std::size_t bucket, std::uintptr_t old) {
        if (block_ == block_end_) {
            block_ = allocate_(arena_, BLOCK);
            block_end_ = block_ + BLOCK;
#ifdef __linux__
            if (populate_) {
                // MADV_POPULATE_WRITE is 23 on Linux; older kernels return EINVAL.
                ::madvise(block_, BLOCK, 23);
            }
#endif
        }
        auto* page = ::new (block_) Page;
        block_ += CHUNK;
        if (old == LINK_OFF) {
            head_[bucket] = page;
        } else {
            auto* previous = reinterpret_cast<Page*>(old - LINK_OFF);
            previous->next = page;
        }
        ++nch_[bucket];
        return reinterpret_cast<std::uintptr_t>(page);
    }

    inline std::uintptr_t take_line(std::size_t bucket) {
        auto address = cur_[bucket];
        if ((address & (CHUNK - 1)) == LINK_OFF) address = new_chunk(bucket, address);
        cur_[bucket] = address + sizeof(Line);
        fill_[bucket] = 0;
        return address;
    }
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    static inline bool wide_stores() {
#if defined(__AVX512F__)
        return true;
#else
        static const bool available = __builtin_cpu_supports("avx512f");
        return available;
#endif
    }
    template<bool Stream> __attribute__((target("avx512f"),always_inline))
    static inline void line_x86(std::uintptr_t address,const Line& line) {
        auto value = _mm512_load_si512(&line);
        auto *out = reinterpret_cast<__m512i*>(address);
        if constexpr (Stream) _mm512_stream_si512(out,value);
        else _mm512_store_si512(out,value);
    }
    template<bool Stream> __attribute__((target("avx512f")))
    void flush_one_x86(std::size_t b) {line_x86<Stream>(take_line(b),lines_[b]);}
    template<bool Stream> __attribute__((target("avx512f")))
    void flush_many_x86(std::size_t k) {
        for (std::size_t i=0;i<k;++i) {
            auto b=std::size_t(pend_[i]);
            line_x86<Stream>(take_line(b),lines_[b]);
        }
    }
#endif
    template<bool Stream> inline void flush_line(std::size_t b) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        if (wide_stores()) {flush_one_x86<Stream>(b);return;}
#endif
        const auto address=take_line(b);
        std::memcpy(reinterpret_cast<void*>(address),&lines_[b],sizeof(Line));
    }
    inline void flush_lines(std::size_t k) {
        if (!k) return;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        if (wide_stores()) {
            if (NT) flush_many_x86<true>(k);
            else flush_many_x86<false>(k);
            return;
        }
#endif
        for (std::size_t i=0;i<k;++i) {
            auto b=std::size_t(pend_[i]);
            const auto address=take_line(b);
            std::memcpy(reinterpret_cast<void*>(address),&lines_[b],sizeof(Line));
        }
    }

    [[gnu::cold,gnu::noinline]] void flush_full(std::size_t k) { flush_lines(k); }

public:
    Part() = default;
    Part(const Part&) = delete;
    Part& operator=(const Part&) = delete;
    Part(Part&&) = default;
    Part& operator=(Part&&) = default;
    static Part empty() { return {}; }
    static Part new_unbound(std::size_t nb) { return Part(nb); }

    template<class Arena> void set_arena(Arena& arena) {
        arena_ = &arena;
        allocate_ = [](void* a, std::size_t n) -> u8* {
            return static_cast<Arena*>(a)->alloc(n);
        };
    }
    template<class Arena> Part(Arena& arena, std::size_t nb) : Part(nb) { set_arena(arena); }

    inline const u8* lines_ptr() const { return reinterpret_cast<const u8*>(lines_.data()); }

    inline void push(std::size_t bucket, u64 w0, u64 w1) {
        const auto offset = fill_[bucket];
        lines_[bucket].rows[offset / sizeof(Rec)] = {w0, w1};
        if (offset == 48) {
            if (NT) flush_line<true>(bucket);
            else flush_line<false>(bucket);
        } else {
            fill_[bucket] = u8(offset + sizeof(Rec));
        }
    }

    void prepare_ordinals() {
        if (ordinal_bounds_.empty()) ordinal_bounds_.assign(head_.size(),{SIZE_MAX,0});
    }
    const auto ordinal_range(std::size_t b) const {
        return ordinal_bounds_.empty() ? std::pair<std::size_t,std::size_t>{SIZE_MAX,0} : ordinal_bounds_[b];
    }
    inline void note_ordinal(std::size_t b,u64 word) {
        const auto ordinal=std::size_t(u32(word));
        auto& range=ordinal_bounds_[b];
        range.first=std::min(range.first,ordinal);
        range.second=std::max(range.second,ordinal+1);
    }
    inline void push_indexed(std::size_t b,u64 w0,u64 w1) {
        prepare_ordinals();note_ordinal(b,w0);push(b,w0,w1);
    }
    template<bool Indexed> inline void push_packet(u64 mask,std::size_t n,const u64* buckets,const u64* line_pointers,
                                                  const std::array<u64,2>* records) {
        if constexpr(Indexed) prepare_ordinals();
        std::size_t pending = 0;
        auto row = [&](std::size_t i) {
            const auto b = std::size_t(buckets[i]);
            if constexpr(Indexed) note_ordinal(b,records[i][0]);
            unsigned offset = fill_[b];
            if (offset == 64) {
                flush_full(pending);
                pending = 0;
                offset = 0;
            }
            auto* destination = reinterpret_cast<u8*>(std::uintptr_t(line_pointers[i])) + offset;
            std::memcpy(std::assume_aligned<16>(destination),
                        std::assume_aligned<16>(records + i), sizeof(Rec));
            const auto next = unsigned(offset + sizeof(Rec));
            fill_[b] = u8(next);
            pend_[pending] = b;
            pending += next >> 6;
        };
        if (mask == (n >= 64 ? UINT64_MAX : (u64{1} << n) - 1)) {
            for (std::size_t i = 0; i < n; ++i) row(i);
        } else {
            while (mask) {
                auto i = std::countr_zero(mask);
                mask &= mask - 1;
                row(i);
            }
        }
        flush_lines(pending);
    }

    inline void push_many(u64 mask,std::size_t n,const u64* buckets,const u64* line_pointers,
                          const std::array<u64,2>* records) {
        push_packet<false>(mask,n,buckets,line_pointers,records);
    }
    inline void push_indexed_many(u64 mask,std::size_t n,const u64* buckets,const u64* line_pointers,
                                  const std::array<u64,2>* records) {
        push_packet<true>(mask,n,buckets,line_pointers,records);
    }
    void finish() {
        for (std::size_t b = 0; b < head_.size(); ++b) {
            const auto bytes = fill_[b];
            if (bytes) {
                auto destination = cur_[b];
                if ((destination & (CHUNK - 1)) == LINK_OFF)
                    destination = new_chunk(b, destination);
                std::memcpy(reinterpret_cast<void*>(destination), &lines_[b], bytes);
                cur_[b] = destination + bytes;
                fill_[b] = 0;
            }
            if (head_[b]) {
                const auto last = (cur_[b] - 1) & ~std::uintptr_t(CHUNK - 1);
                n_[b] = (nch_[b] - 1) * u32(LINK_OFF / sizeof(Rec))
                      + u32((cur_[b] - last) / sizeof(Rec));
            }
        }
#if defined(__x86_64__) || defined(_M_X64)
        if (NT) _mm_sfence();
#endif
    }

    template<class F> inline void for_each(std::size_t b, F&& f) const {
        if (b >= head_.size() || !head_[b]) return;
        const auto head_of_last = (cur_[b] - 1) & ~std::uintptr_t(CHUNK - 1);
        auto* page = head_[b];
        while (reinterpret_cast<std::uintptr_t>(page) != head_of_last) {
            auto* next = page->next;
#if defined(__GNUC__) || defined(__clang__)
            if (PREFETCH) {
                __builtin_prefetch(next);
                __builtin_prefetch(reinterpret_cast<const u8*>(next) + 64);
                __builtin_prefetch(&next->next);
            }
#endif
            f(std::span<const Rec>(page->rows));
            page = next;
        }
        f(std::span<const Rec>(page->rows, (cur_[b] - head_of_last) / sizeof(Rec)));
    }
    inline std::size_t count(std::size_t b) const { return n_[b]; }
};
}
