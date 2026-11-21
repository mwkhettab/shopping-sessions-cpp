#pragma once
#include "metrics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using Str = std::string_view;
using Time = int64_t;
constexpr Time day_ms = 86400000, reach = 7 * day_ms, bad = INT64_MIN;
template <class V> using Dict = std::unordered_map<Str, V>;

struct InputChunk {
  Str file;
  size_t begin, end;
  size_t size() const { return end - begin; }
  Str body() const {
    size_t first = begin;
    if (!begin || file[begin - 1] != '\n') {
      // Skip the header or a row owned by an earlier chunk. Bound this
      // search so that a long physical line costs linear time overall.
      size_t lf = file.substr(begin, size()).find('\n');
      if (lf == Str::npos)
        return {};
      first += lf + 1;
    }
    if (first == end)
      return {};
    size_t last = end;
    if (end < file.size() && file[end - 1] != '\n') {
      size_t lf = file.find('\n', end);
      last = lf == Str::npos ? file.size() : lf + 1;
    }
    return file.substr(first, last - first);
  }
};
struct Input {
  std::vector<Str> mappings;
  ~Input() {
    for (auto s : mappings)
      munmap(const_cast<char *>(s.data()), s.size());
  }
  Str map(const fs::path &p) {
    int fd = open(p.c_str(), O_RDONLY);
    if (fd < 0)
      throw std::runtime_error("open " + p.string());
    struct stat st{};
    if (fstat(fd, &st)) {
      close(fd);
      throw std::runtime_error("stat " + p.string());
    }
    if (!st.st_size) {
      close(fd);
      return {};
    }
    auto ptr = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED)
      throw std::runtime_error("mmap " + p.string());
    mappings.emplace_back(static_cast<const char *>(ptr), st.st_size);
    return mappings.back();
  }
  std::vector<InputChunk> parts(const fs::path &dir) {
    std::vector<InputChunk> chunks;
    constexpr size_t block = 512 * 1024;
    for (auto &file : fs::directory_iterator(dir)) {
      if (file.path().extension() != ".csv")
        continue;
      Str data = map(file.path());
      for (size_t off = 0; off < data.size(); off += block)
        chunks.push_back({data, off, std::min(data.size(), off + block)});
    }
    return chunks;
  }
};

template <size_t N, class F> void scalar_rows(Str data, F &&callback) {
  std::array<Str, N> fields;
  while (!data.empty()) {
    size_t end = data.find('\n');
    Str line = data.substr(0, end);
    data.remove_prefix(line.size() + (end != Str::npos));
    for (size_t i = 0; i < N; ++i) {
      auto comma = line.find(',');
      if ((comma == Str::npos) != (i + 1 == N))
        break;
      Str s = line.substr(0, comma);
      if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s.remove_prefix(1);
        s.remove_suffix(1);
      }
      if (s.find('"') != Str::npos)
        break;
      fields[i] = s;
      if (i + 1 == N)
        callback(fields);
      else
        line.remove_prefix(comma + 1);
    }
  }
}

#if defined(__SSE2__)
#include <emmintrin.h>
inline std::pair<unsigned, unsigned> delimiters(const char *p) {
  auto v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
  auto ends = _mm_or_si128(_mm_cmpeq_epi8(v, _mm_set1_epi8(',')),
                           _mm_cmpeq_epi8(v, _mm_set1_epi8('\n')));
  return {_mm_movemask_epi8(ends),
          _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_set1_epi8('"')))};
}
#elif defined(__aarch64__)
#include <arm_neon.h>
inline unsigned mask(uint8x16_t matches) {
  constexpr uint8_t weights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                   1, 2, 4, 8, 16, 32, 64, 128};
  auto bits = vandq_u8(matches, vld1q_u8(weights));
  return vaddv_u8(vget_low_u8(bits)) |
         (unsigned(vaddv_u8(vget_high_u8(bits))) << 8);
}
inline std::pair<unsigned, unsigned> delimiters(const char *p) {
  auto v = vld1q_u8(reinterpret_cast<const uint8_t *>(p));
  auto ends =
      vorrq_u8(vceqq_u8(v, vdupq_n_u8(',')), vceqq_u8(v, vdupq_n_u8('\n')));
  return {mask(ends), mask(vceqq_u8(v, vdupq_n_u8('"')))};
}
#endif
#include "csv_x86.hpp"
#include "line_csv.hpp"
#include "line_tile.hpp"
#include "marked_events.hpp"
template <size_t N, class F> void parse_rows(Str data, F &&callback) {
#ifdef SHOPPING_X86_CSV
#ifdef __x86_64__
  if constexpr (N == 7) {
    if (wide_csv_supported() && __builtin_cpu_supports("bmi2") &&
        __builtin_cpu_supports("pclmul")) {
      if (__builtin_cpu_supports("avx512f") &&
          __builtin_cpu_supports("avx512bw") &&
          __builtin_cpu_supports("avx512vbmi2")) {
        marked_events_vbmi2(data, callback);
        return;
      }
      marked_events_avx2(data, callback);
      return;

    }
  }
#endif
  if (N == 7 && wide_csv_supported()) {
    tiled_lines<N, 16>(data, callback);
    return;
  }
  if (wide_csv_supported()) { line_csv_rows<N>(data,callback); return; }
  if (wide_csv_supported()) {
    wide_csv_rows<N>(data, callback);
    return;
  }
#endif
#if defined(__SSE2__) || defined(__aarch64__)
  if (data.empty())
    return;
  std::array<Str, N> fields;
  size_t column = 0, expected_quotes = 0, actual_quotes = 0;
  const char *field = data.data();
  auto finish_field = [&](const char *end) {
    Str s(field, end - field);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
      s.remove_prefix(1);
      s.remove_suffix(1);
      expected_quotes += 2;
    }
    if (column < N)
      fields[column] = s;
    ++column;
  };
  for (size_t off = 0; off < data.size(); off += 16) {
    const char *base = data.data() + off;
    char tail[16]{};
    const char *block = base;
    if (data.size() - off < 16) {
      std::memcpy(tail, base, data.size() - off);
      block = tail;
    }
    auto [ends, quotes] = delimiters(block);
    while (ends) {
      unsigned at = std::countr_zero(ends);
      ends &= ends - 1;
      finish_field(base + at);
      field = base + at + 1;
      if (base[at] == '\n') {
        unsigned before = (1u << at) - 1;
        actual_quotes += std::popcount(quotes & before);
        quotes &= ~before;
        if (column == N && expected_quotes == actual_quotes)
          callback(fields);
        column = actual_quotes = expected_quotes = 0;
      }
    }
    actual_quotes += std::popcount(quotes);
  }
  if (data.back() != '\n') {
    finish_field(data.data() + data.size());
    if (column == N && expected_quotes == actual_quotes)
      callback(fields);
  }
#else
  scalar_rows<N>(data, callback);
#endif
}
template <size_t N, class F> void rows(Str data, F &&callback) {
#ifdef SHOPPING_DIAGNOSTICS
  // Instrument all callbacks in one of every 32 chunks on each worker.
  // Report the sample itself so estimates can be scaled by input coverage.
  static thread_local unsigned chunk_count = 0;
  metrics::local.csv_bytes += data.size();
  if (chunk_count++ % 32 == 0) {
    auto start = metrics::now();
    int64_t callbacks = 0;
    parse_rows<N>(data, [&](auto &r) {
      auto entry = metrics::now();
      callback(r);
      callbacks += metrics::now() - entry;
    });
    metrics::local.csv_sample += data.size();
    metrics::local.csv_ns += metrics::now() - start - callbacks;
    return;
  }
#endif
  parse_rows<N>(data, callback);
}

inline Time number(Str s, Time limit) {
  Time n = 0;
  if (s.empty())
    return bad;
  if (s.size() <= 18) {
    for (unsigned char c : s) {
      unsigned d = unsigned(c) - '0';
      if (d > 9)
        return bad;
      n = n * 10 + d;
    }
    return n <= limit ? n : bad;
  }
  for (unsigned char c : s) {
    unsigned d = unsigned(c) - '0';
    if (d > 9 || n > (limit - static_cast<Time>(d)) / 10)
      return bad;
    n = n * 10 + d;
  }
  return n;
}
inline int pair_digits(const char *p) {
  unsigned a = unsigned(static_cast<unsigned char>(p[0])) - '0';
  unsigned b = unsigned(static_cast<unsigned char>(p[1])) - '0';
  return (a | b) < 16 && a < 10 && b < 10 ? int(10 * a + b) : -1000;
}
inline uint64_t little_word(const char *p) {
  uint64_t word;
  std::memcpy(&word, p, sizeof(word));
  if constexpr (std::endian::native == std::endian::big)
    word = __builtin_bswap64(word);
  return word;
}
inline bool eight_digits(uint64_t word) {
  const uint64_t digits = word ^ 0x3030303030303030ULL;
  // Once the high nibbles match ASCII '0', adding six sets bit 4 in
  // precisely the bytes above nine. No carry reaches another byte.
  return !(digits & 0xf0f0f0f0f0f0f0f0ULL) &&
         !((digits + 0x0606060606060606ULL) & 0x1010101010101010ULL);
}
inline uint32_t decimal_eight(uint64_t word) {
  uint64_t digits = word & 0x0f0f0f0f0f0f0f0fULL;
  uint64_t pairs = (digits * 10 + (digits >> 8)) & 0x00ff00ff00ff00ffULL;
  uint64_t quads = (pairs * 100 + (pairs >> 16)) & 0x0000ffff0000ffffULL;
  return uint32_t(quads) * 10000 + uint32_t(quads >> 32);
}
template <bool validate> inline int clock_seconds(const char *clock) {
  // XOR with "00:00:00" in little-endian byte order. For a valid clock the
  // separators become zero, and hour, minute and second pairs start at 0,3,6.
  uint64_t digits = little_word(clock) ^ 0x30303a30303a3030ULL;
  if constexpr (validate) {
    if ((digits & 0xf0f0fff0f0fff0f0ULL) ||
        ((digits + 0x0606000606000606ULL) & 0x1010001010001010ULL))
      return -1;
  }
  uint64_t pairs = digits * 10 + (digits >> 8);
  int h = uint8_t(pairs), m = uint8_t(pairs >> 24), s = uint8_t(pairs >> 48);
  if constexpr (validate)
    if (h > 23 || m > 59 || s > 59)
      return -1;
  return (h * 60 + m) * 60 + s;
}
inline Time midnight(const char *p) {
  struct CachedDay {
    uint64_t prefix{};
    uint16_t suffix{};
    Time result = bad;
  };
  static thread_local std::array<CachedDay, 64> cache{};
  uint64_t prefix;
  uint16_t suffix;
  std::memcpy(&prefix, p, 8);
  std::memcpy(&suffix, p + 8, 2);
  auto &hit = cache[(unsigned(uint8_t(p[8])) * 11 + uint8_t(p[9])) % 64];
  if (hit.prefix == prefix && hit.suffix == suffix)
    return hit.result;
  int century = pair_digits(p), yy = pair_digits(p + 2), m = pair_digits(p + 5),
      d = pair_digits(p + 8);
  int y = century * 100 + yy;
  if (p[4] != '-' || p[7] != '-' || century < 0 || yy < 0 || m < 1 || m > 12 ||
      d < 1)
    return bad;
  constexpr int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (d >
      lengths[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
    return bad;
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400, year = y - era * 400;
  int days = era * 146097 + year * 365 + year / 4 - year / 100 +
             (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1 - 719468;
  hit = {prefix, suffix, days * day_ms};
  return hit.result;
}
inline Time timestamp(Str s) {
  if (s.size() == 13) {
    auto first = little_word(s.data()), last = little_word(s.data() + 5);
    if (!eight_digits(first) || !eight_digits(last))
      return bad;
    return Time(decimal_eight(first)) * 100000 +
           decimal_eight(last & ~0xffffffULL);
  }
  if ((s.size() != 20 && s.size() != 25) || s[4] != '-')
    return number(s, INT64_MAX);
  auto p = s.data();
  int seconds = clock_seconds<true>(p + 11);
  if (s[10] != 'T' || seconds < 0)
    return bad;
  int offset = 0;
  if (s.size() == 25) {
    int oh = pair_digits(p + 20), om = pair_digits(p + 23);
    if ((p[19] != '+' && p[19] != '-') || p[22] != ':' || unsigned(oh) > 23 ||
        unsigned(om) > 59)
      return bad;
    offset = (oh * 60 + om) * (p[19] == '+' ? 1 : -1);
  } else if (p[19] != 'Z')
    return bad;
  auto day = midnight(p);
  return day == bad ? bad : day + (seconds - offset * 60) * 1000LL;
}
inline Time ingest_time(Str s) {
  if (s.size() != 20)
    return timestamp(s);
  // The contract guarantees F1 for ingest_ts.
  auto day = midnight(s.data());
  return day == bad ? bad : day + clock_seconds<false>(s.data() + 11) * 1000LL;
}
// Common identifiers contain a prefix and up to eight suffix bytes. Encode
// them losslessly inline. Other byte strings refer to stable, borrowed views.
// Domains (event IDs, user IDs) never compare keys from different prefixes.
// Permute the 63 inline payload bits bijectively. Shard and table lookups
// can reuse the stored hash, while display recovers the exact original bytes.
inline constexpr uint64_t key_mask=UINT64_MAX>>1;
inline constexpr uint64_t key_multiplier=0x9e3779b97f4a7c15ULL;
inline constexpr uint64_t key_inverse=[]{
  uint64_t x=1;
  for(unsigned i=0;i<6;++i) x*=2-key_multiplier*x;
  return x;
}();
static_assert(key_multiplier*key_inverse==1);
inline uint64_t permute_key(uint64_t n) {
  n^=n>>27; n=(n*key_multiplier)&key_mask;
  return n^(n>>31);
}
inline uint64_t recover_key(uint64_t n) {
  n^=(n>>31)^(n>>62); n=(n*key_inverse)&key_mask;
  return n^(n>>27)^(n>>54);
}
struct BorrowedKey { Str text; size_t hash; };
struct Key {
  uintptr_t bits{};
  bool indirect() const { return bits & 1; }
  Str view() const { return reinterpret_cast<const BorrowedKey *>(bits-1)->text; }
  bool operator==(Key b) const {
    return bits == b.bits || (indirect() && b.indirect() && view() == b.view());
  }
  size_t hash() const {
    if (indirect())
      return reinterpret_cast<const BorrowedKey *>(bits-1)->hash;
    return bits >> 1;
  }
  Str user_text(std::array<char, 9> &buffer, char prefix = 'u') const {
    if (indirect())
      return view();
    uint64_t n = recover_key(bits >> 1);
    buffer[0] = prefix;
    std::memcpy(buffer.data() + 1, &n, 8);
    return {buffer.data(), size_t(1 + (std::bit_width(n) + 7) / 8)};
  }
  bool user_less(Key other, char prefix = 'u') const {
    if (!indirect() && !other.indirect())
      return __builtin_bswap64(recover_key(bits >> 1)) < __builtin_bswap64(recover_key(other.bits >> 1));
    std::array<char, 9> left, right;
    return user_text(left,prefix) < other.user_text(right,prefix);
  }
};
struct KeyHash {
  size_t operator()(Key k) const { return k.hash(); }
};
struct KeyPool {
  std::deque<BorrowedKey> other;
  Key encode(Str s, char prefix) {
    if constexpr (sizeof(uintptr_t) == 8 &&
                  std::endian::native == std::endian::little) {
      if (s.size() >= 2 && s.size() <= 9 && s.front() == prefix &&
          s.back() != 0) {
        uint64_t n = 0;
        if (s.size() == 9)
          std::memcpy(&n, s.data() + 1, 8);
        else
          std::memcpy(&n, s.data() + 1, s.size() - 1);
        if (!(n >> 63))
          return {uintptr_t(permute_key(n) << 1)};
      }
    }
    other.push_back({s,std::hash<Str>{}(s)});
    static_assert(alignof(Str) >= 4);
    return {reinterpret_cast<uintptr_t>(&other.back()) | 1};
  }
};
constexpr size_t shards = 512;
template <class F> void parallel(size_t jobs, unsigned threads, F &&f) {
  std::atomic<size_t> next{0};
  std::exception_ptr error;
  std::mutex lock;
  std::vector<std::jthread> workers;
#ifdef SHOPPING_DIAGNOSTICS
  std::vector<metrics::WorkerLoad> loads(std::min<size_t>(threads, jobs));
#endif
  for (unsigned t = 0; t < std::min<size_t>(threads, jobs); ++t)
    workers.emplace_back([&, t] {
#ifdef SHOPPING_DIAGNOSTICS
      auto &load = loads[t];
      const auto start = metrics::now(), cpu = metrics::thread_cpu();
#endif
      try {
        for (size_t i;
             (i = next.fetch_add(1, std::memory_order_relaxed)) < jobs;) {
          f(i, t);
#ifdef SHOPPING_DIAGNOSTICS
          ++load.jobs;
#endif
        }
      } catch (...) {
        std::lock_guard guard(lock);
        error = std::current_exception();
        next.store(jobs);
      }
#ifdef SHOPPING_DIAGNOSTICS
      load.elapsed = metrics::now() - start;
      load.cpu = metrics::thread_cpu() - cpu;
#endif
    });
  workers.clear();
#ifdef SHOPPING_DIAGNOSTICS
  if (!loads.empty()) {
    double min_s = 1e100, max_s = 0, cpu_s = 0;
    size_t min_jobs = jobs, max_jobs = 0;
    for (auto &l : loads) {
      min_s = std::min(min_s, l.elapsed * 1e-9);
      max_s = std::max(max_s, l.elapsed * 1e-9);
      cpu_s += l.cpu * 1e-9;
      min_jobs = std::min(min_jobs, l.jobs);
      max_jobs = std::max(max_jobs, l.jobs);
    }
    std::cerr << "worker jobs " << min_jobs << ".." << max_jobs << ", active "
              << min_s << ".." << max_s << " s, CPU " << cpu_s << " s\n";
  }
#endif
  if (error)
    std::rethrow_exception(error);
}
