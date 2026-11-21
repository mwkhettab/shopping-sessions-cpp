#include "../cpp/input.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>

using Fields = std::array<Str, 7>;

struct CheckRows {
  std::vector<Fields> scalar;
  size_t next{}, marked{};
  bool accept{};
  void operator()(const Fields &row) {
    if (next == scalar.size() || scalar[next++] != row)
      std::abort();
  }
  bool rich(const char *id, unsigned iw, const char *user, unsigned uw, Str ts,
            Str arrival, Str kind, Str product, Str price) {
    const Fields row{Str(id, iw), Str(user, uw), ts,   arrival,
                     kind,        product,       price};
    if (iw < 2 || iw > 9 || uw < 2 || uw > 9 || arrival.size() != 20 ||
        next == scalar.size() || scalar[next] != row)
      std::abort();
    ++marked;
    if (accept)
      (*this)(row);
    return accept;
  }
};

size_t check(Str text) {
  CheckRows rows;
  scalar_rows<7>(text, [&](const Fields &row) { rows.scalar.push_back(row); });
  for (bool accept : {false, true}) {
    rows.next = 0;
    rows.accept = accept;
    parse_rows<7>(text, rows);
    if (rows.next != rows.scalar.size())
      std::abort();
  }
  return rows.marked;
}

int main() {
  size_t marked = 0;
  const Fields base{"e7654321",
                    "u98765432",
                    "0000000000001",
                    "2025-11-01T00:00:01Z",
                    "purchase",
                    "p",
                    "123"};
  std::string clean;
  for (unsigned quotes = 0; quotes < 128; ++quotes) {
    for (size_t k = 0; k != base.size(); ++k) {
      if (k)
        clean += ',';
      if (quotes >> k & 1)
        clean += '"';
      if (!(k >= 4 && quotes % 11 == 0))
        clean += base[k];
      if (quotes >> k & 1)
        clean += '"';
    }
    clean += '\n';
  }
  for (unsigned leading = 0; leading < 128; ++leading) {
    for (unsigned width :
         {63u, 64u, 127u, 255u, 511u, 1023u, 1024u, 1025u, 2048u}) {
      std::string input = std::string(leading, 'z') + ",,,,,,\n";
      input += '"' + std::string(width, 'x') + "\",,,,,,\n" + clean;
      if (leading & 1)
        input.pop_back();
      marked += check(input);
    }
  }
  std::mt19937 random(309811);
  for (unsigned i = 0; i < 1400; ++i) {
    std::string input = clean;
    size_t pos = random() % input.size();
    if (i % 3 == 0)
      input.erase(pos, 1);
    else if (i % 3 == 1)
      input.insert(pos, 1, ",\"\n\r\0"[i % 5]);
    else
      input[pos] = ",\"\n\r\0"[i % 5];
    marked += check(input);
  }
#ifdef SHOPPING_X86_CSV
  if (wide_csv_supported() && __builtin_cpu_supports("bmi2") &&
      __builtin_cpu_supports("pclmul") &&
      __builtin_cpu_supports("avx512vbmi2") &&
      __builtin_cpu_supports("avx512bw") && !marked)
    std::abort();
#endif
  std::printf("2552 CSV populations matched the scalar reader, %zu marked "
              "callbacks checked\n",
              marked);
}
