#include "../cpp/input.hpp"
#include <cassert>
#include <cstdio>
#include <random>

template <size_t N> void compare_rows(Str data) {
  std::vector<std::array<Str, N>> expected, actual;
  scalar_rows<N>(data, [&](auto row) { expected.push_back(row); });
  rows<N>(data, [&](auto row) { actual.push_back(row); });
  assert(actual == expected);
}

int main() {
  for (const std::string data :
       {"", "header", "header\n", "a\n0\n12\n\n34", "a\n\n\n",
        "long header\nlong first record\nx\nlong last record\n"}) {
    const auto nl = data.find('\n');
    const auto expected = nl == std::string::npos ? "" : data.substr(nl + 1);
    for (size_t step = 1; step <= data.size() + 1; ++step) {
      std::string joined;
      for (size_t off = 0; off < data.size(); off += step)
        joined +=
            InputChunk{data, off, std::min(data.size(), off + step)}.body();
      assert(joined == expected);
    }
  }
  for (size_t n : {31, 32, 33, 63, 64, 65, 127, 128, 129, 8191, 8192, 8193,
                   524295}) {
    for (char fill : {'a', ',', '\n', '"', '\0'}) {
      std::string s = "a,b\nc,d\n\"";
      s.append(n, fill);
      s += "\",z\n1,2,3,4\n5,6,7,8,9,10,11";
      compare_rows<2>(s);
      compare_rows<4>(s);
      compare_rows<7>(s);
    }
  }
  std::string epoch = "1791071890000";
  for (size_t i = 0; i < epoch.size(); ++i) {
    auto s = epoch;
    for (unsigned c = 0; c < 256; ++c) {
      s[i] = char(c);
      assert(timestamp(s) == number(s, INT64_MAX));
    }
  }
  for (int n = 0; n < 86400; ++n) {
    char clock[21];
    std::snprintf(clock, sizeof(clock), "1970-01-01T%02d:%02d:%02dZ", n / 3600,
                  n / 60 % 60, n % 60);
    assert(timestamp(clock) == Time(n) * 1000);
    assert(ingest_time(clock) == Time(n) * 1000);
  }
  std::string instant = "1970-01-01T10:24:36Z";
  for (size_t i = 11; i < 19; ++i) {
    for (unsigned c = 0; c < 256; ++c) {
      auto s = instant;
      s[i] = char(c);
      int h = pair_digits(s.data() + 11), m = pair_digits(s.data() + 14),
          sec = pair_digits(s.data() + 17);
      bool valid = s[13] == ':' && s[16] == ':' && unsigned(h) < 24 &&
                   unsigned(m) < 60 && unsigned(sec) < 60;
      assert(timestamp(s) ==
             (valid ? Time((h * 60 + m) * 60 + sec) * 1000 : bad));
    }
  }
  // Exercise vector block boundaries, missing terminal LF, malformed quotes,
  // variable field counts and arbitrary bytes. Compare every accepted field.
  std::mt19937 rng(173);
  const char alphabet[] = {'a',  'u',        ' ',        '\0',
                           '\r', char(0xc3), char(0xa9), '1'};
  for (int trial = 0; trial < 200; ++trial) {
    std::string data;
    for (int line = 0; line < 120; ++line) {
      int fields = rng() % 4 == 0 ? rng() % 10 : 7;
      for (int i = 0; i < fields; ++i) {
        if (i)
          data += ',';
        bool quoted = rng() % 3 == 0;
        if (quoted)
          data += '"';
        for (unsigned n = rng() % 35; n; --n) {
          data += rng() % 80 == 0 ? '"' : alphabet[rng() % sizeof(alphabet)];
        }
        if (quoted)
          data += '"';
      }
      if (line < 119 || trial % 2 == 0)
        data += '\n';
    }
    compare_rows<7>(data);
  }
  // Two separate pools exercise indirect-key equality and hashing.
  std::deque<std::string> names;
  KeyPool first, second;
  std::unordered_map<Key, Str, KeyHash> seen;
  for (int i = 0; i < 12000; ++i) {
    std::string name = i % 4 ? "u" : "U";
    for (unsigned n = rng() % 20; n; --n)
      name += char(rng() % 256);
    auto &stable = names.emplace_back(name);
    auto key = first.encode(stable, 'u');
    std::array<char, 9> buf;
    assert(key.user_text(buf) == stable);
    auto [it, inserted] = seen.emplace(key, stable);
    assert(inserted || it->second == stable);
    assert(seen.at(second.encode(stable, 'u')) == stable);
    const auto &peer = names[rng() % names.size()];
    assert(key.user_less(second.encode(peer, 'u')) == (stable < peer));
  }
}
