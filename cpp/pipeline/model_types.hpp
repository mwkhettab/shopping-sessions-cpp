#pragma once
#include "iface.hpp"
#include <bit>
#include <utility>

namespace pipeline {
using i32 = std::int32_t;
using i16 = std::int16_t;
using i8 = std::int8_t;
inline constexpr u32 BB = 14, UB = 12;
inline constexpr std::size_t BSIZE = std::size_t{1} << BB, UBSIZE = std::size_t{1} << UB;
inline constexpr u64 SPIN_US_DEFAULT = 400;
inline constexpr i64 DAY_MS = 86400000;
inline constexpr u64 WEEK_MS = 604800000, GAP_MS = 1800000;
inline constexpr std::size_t PBOUND = 1u << 17, CBOUND = 1u << 14;
inline constexpr u64 INVALID_TS = 0x7fffffff, ING_MAX = (1u << 21) - 1;
inline constexpr u16 COMPLEX = 0x8000, EXTRA = 0x4000, MASK = 0x3fff;
inline constexpr u64 NALIAS = 14, RAW = u64{1} << 42, ODDU = u64{1} << 26;
inline constexpr std::size_t NSHARD = 64;
inline constexpr u64 TAG_UF = u64{1} << 62, TAG_ODDC = u64{2} << 62;

struct Rec { u64 w0, w1; };
struct OddEv { u64 eid; i64 ing; u64 w0, user; };
enum class Kind { Ev, Ck, Lk };
struct FileInfo { Kind kind; std::size_t off, len, hdr; };
struct Chunk { u32 file; std::size_t a,b; };
struct Seg { u32 tid{}, off{}, len{}, user{}; };
struct EdgeInfo { u32 n{}; Seg first{},last{}; };
static_assert(sizeof(Rec) == 16 && sizeof(Seg) == 16 && sizeof(OddEv) == 32);

inline constexpr auto PAIR36 = []{
    std::array<u16,1296> t{};
    constexpr char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = u16(alphabet[i/36]) | (u16(alphabet[i%36]) << 8);
    return t;
}();
inline std::pair<u64,std::size_t> enc36(u64 y) {
    const u32 hi = u32(y / 1679616), lo = u32(y % 1679616);
    const u64 le = u64(PAIR36[hi/1296]) | (u64(PAIR36[hi%1296]) << 16) |
                   (u64(PAIR36[lo/1296]) << 32) | (u64(PAIR36[lo%1296]) << 48);
    const u64 be = __builtin_bswap64(le), z = be ^ 0x3030303030303030ULL;
    const unsigned leading = z ? unsigned(std::countl_zero(z)) / 8 : 7;
    return {be << (8*leading),8-leading};
}
inline std::size_t shard(u64 pkey) { return (pkey * 0x9e3779b97f4a7c15ULL) >> 58; }
}
