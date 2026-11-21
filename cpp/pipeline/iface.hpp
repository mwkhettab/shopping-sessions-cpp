#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace pipeline {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;
inline constexpr u64 ID_EMPTY = UINT64_MAX, ID_ODD = u64{1} << 63;
inline constexpr i64 TS_BAD = INT64_MIN, TS_HUGE = INT64_MAX;
inline constexpr u32 PROD_EMPTY = UINT32_MAX, PROD_ODD = UINT32_MAX - 1;
inline constexpr u32 CAMP_EMPTY = UINT32_MAX, CAMP_ODD = UINT32_MAX - 1;
inline constexpr u8 KIND_VIEW = 0, KIND_CART = 1, KIND_PURCHASE = 2, KIND_OTHER = 3;

struct EvRow { u64 eid, uid; i64 ts, ingest; u8 kind; u32 product, price; };
struct ClRow { u64 cid, uid; u32 campaign; i64 ts; };

inline constexpr std::size_t NB = 64, BATCH_ALIGN = NB * 8, SCRATCH = 256;
inline constexpr u64 PP_INVALID = u64{1} << 63, PP_SOME = PROD_ODD;
inline constexpr u64 PP_NONE = PP_INVALID | PROD_EMPTY;
inline constexpr u64 SCRATCH_INIT = 0x2c2c2c2c2c2c2c2cULL;

inline constexpr u64 ev_pp(u8 kind, u32 product, u32 price) {
    const u64 k = u64{kind} << 56;
    if (kind == KIND_PURCHASE) {
        const u64 invalid = product == PROD_EMPTY || price == 0 ? PP_INVALID : 0;
        return product | (u64{price} << 32) | k | invalid;
    }
    if (product == PROD_EMPTY) return PP_NONE | k;
    return PP_SOME | k | (kind > KIND_PURCHASE ? PP_INVALID : 0);
}

struct alignas(BATCH_ALIGN) EvBatch {
    std::array<i64, NB> ts{};
    std::array<u64, NB> pp{}, eid{}, uid{};
    std::array<i64, NB> ing{};
    std::array<u64, 8> pad{};
    std::array<u64, SCRATCH> scratch{};
    std::size_t n{};
    EvBatch() {
        scratch.fill(SCRATCH_INIT);
        scratch[2] = scratch[3] = 0;
    }
    EvRow row(std::size_t i) const {
        const u64 p = pp[i];
        return {eid[i], uid[i], ts[i], ing[i], u8((p >> 56) & 3),
                u32(p), u32(p >> 32) & 0x00ffffff};
    }
};
struct alignas(BATCH_ALIGN) CkBatch {
    std::array<i64, NB> ts{};
    std::array<u64, NB> camp{}, cid{}, uid{};
    std::array<u64, SCRATCH> scratch{};
    std::size_t n{};
    CkBatch() {
        scratch.fill(SCRATCH_INIT);
        scratch[2] = scratch[3] = 0;
    }
    ClRow row(std::size_t i) const { return {cid[i], uid[i], u32(camp[i]), ts[i]}; }
};
static_assert(alignof(EvBatch) == 512 && offsetof(EvBatch, ts) == 0);
static_assert(offsetof(EvBatch, pp) == 512 && offsetof(EvBatch, ing) == 2048);
static_assert(offsetof(EvBatch, scratch) == 2624 && offsetof(EvBatch, n) == 4672);
static_assert(alignof(CkBatch) == 512 && offsetof(CkBatch, ts) == 0);
static_assert(offsetof(CkBatch, scratch) == 2048 && offsetof(CkBatch, n) == 4096);

template<class Sink> inline void event(Sink &sink, const EvRow &r) {
    auto &b = *sink.ev();
    const auto i = b.n;
    b.eid[i] = r.eid; b.uid[i] = r.uid;
    b.ts[i] = r.ts; b.ing[i] = r.ingest;
    b.pp[i] = ev_pp(r.kind, r.product, r.price);
    if (++b.n == NB) sink.ev_flush();
}
template<class Sink> inline void click(Sink &sink, const ClRow &r) {
    auto &b = *sink.ck();
    const auto i = b.n;
    b.cid[i] = r.cid; b.uid[i] = r.uid;
    b.ts[i] = r.ts; b.camp[i] = r.campaign;
    if (++b.n == NB) sink.ck_flush();
}
inline constexpr u64 odd_id(std::size_t offset, std::size_t len) {
    return ID_ODD | (u64(offset) << 8) | std::min<std::size_t>(len, 255);
}
inline constexpr bool id_is_odd(u64 id) { return id != ID_EMPTY && (id & ID_ODD); }
inline constexpr std::size_t odd_offset(u64 id) { return (id & ~ID_ODD) >> 8; }
inline constexpr std::size_t odd_len(u64 id) { return id & 255; }
}
