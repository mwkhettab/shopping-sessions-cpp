#pragma once
#include "iface.hpp"
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace pipeline::k_ref {
using Bytes = std::string_view;
using Bounds = std::pair<std::size_t, std::size_t>;

template<class Fn> void for_each_line(Bytes buf, Fn &&f) {
    for (std::size_t start = 0; start < buf.size();) {
        auto end = buf.find('\n', start);
        if (end == Bytes::npos) end = buf.size();
        f(start, end);
        start = end + 1;
    }
}
inline std::optional<Bounds> field_value(Bytes buf, std::size_t s, std::size_t e) {
    const auto raw = buf.substr(s, e - s);
    const auto quotes = std::count(raw.begin(), raw.end(), '"');
    if (quotes == 0) return Bounds{s, e};
    if (quotes == 2 && raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        return Bounds{s + 1, e - 1};
    return {};
}
template<std::size_t N> std::optional<std::array<Bounds, N>>
split_line(Bytes buf, std::size_t start, std::size_t end) {
    if (start == end) return {};
    std::array<Bounds, N> out{};
    std::size_t count = 0, field = start;
    for (auto i = start;; ++i) {
        if (i == end || buf[i] == ',') {
            if (count == N) return {};
            auto v = field_value(buf, field, i);
            if (!v) return {};
            out[count++] = *v;
            if (i == end) break;
            field = i + 1;
        }
    }
    if (count == N) return out;
    return {};
}
inline bool digit(char c) { return c >= '0' && c <= '9'; }
inline u64 parse_id(Bytes buf, Bounds p, char prefix) {
    const auto [s, e] = p;
    if (s == e) return ID_EMPTY;
    auto d = buf.substr(s + 1, e - s - 1);
    if (buf[s] != prefix || d.empty() || d.size() > 8 || (d[0] == '0' && d.size() > 1))
        return odd_id(s, e - s);
    u64 value = 0;
    for (char c : d) {
        if (!digit(c) && !(c >= 'a' && c <= 'z')) return odd_id(s, e - s);
        value = value * 36 + (digit(c) ? c - '0' : c - 'a' + 10);
    }
    return value;
}
inline std::optional<u32> canonical_decimal(Bytes d) {
    if (d.empty() || d.size() > 8 || (d[0] == '0' && d.size() > 1)) return {};
    u32 value = 0;
    for (char c : d) {
        if (!digit(c)) return {};
        value = value * 10 + u32(c - '0');
    }
    return value;
}
inline u32 parse_product(Bytes s) {
    if (s.empty()) return PROD_EMPTY;
    if (!s.starts_with('p')) return PROD_ODD;
    return canonical_decimal(s.substr(1)).value_or(PROD_ODD);
}
inline u32 parse_campaign(Bytes s) {
    if (s.empty()) return CAMP_EMPTY;
    if (!s.starts_with("c_")) return CAMP_ODD;
    return canonical_decimal(s.substr(2)).value_or(CAMP_ODD);
}
inline u8 parse_kind(Bytes s) {
    if (s == "view") return KIND_VIEW;
    if (s == "cart") return KIND_CART;
    if (s == "purchase") return KIND_PURCHASE;
    return KIND_OTHER;
}
inline u32 parse_price(Bytes s) {
    if (s.empty() || !std::all_of(s.begin(), s.end(), digit)) return 0;
    auto first = s.find_first_not_of('0');
    if (first == Bytes::npos || s.size() - first > 8) return 0;
    u32 n = 0;
    for (char c : s.substr(first)) n = n * 10 + u32(c - '0');
    return n <= 10000000 ? n : 0;
}
inline bool is_leap(i64 y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
inline i64 days_in_month(i64 y, i64 m) {
    constexpr int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 0;
    return days[m] + int(m == 2 && is_leap(y));
}
inline i64 days_from_civil(i64 y, i64 m, i64 d) {
    if (m <= 2) --y;
    const auto era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = y - era * 400;
    const auto doy = (153 * ((m + 9) % 12) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
inline std::optional<i64> digits_value(Bytes v) {
    i64 n = 0;
    for (char c : v) {
        if (!digit(c)) return {};
        n = n * 10 + (c - '0');
    }
    return n;
}
inline std::optional<i64> civil_ms(Bytes v) {
    if (v.size() != 19 || v[4] != '-' || v[7] != '-' || v[10] != 'T' ||
        v[13] != ':' || v[16] != ':') return {};
    auto y = digits_value(v.substr(0,4)), m = digits_value(v.substr(5,2));
    auto d = digits_value(v.substr(8,2)), h = digits_value(v.substr(11,2));
    auto mi = digits_value(v.substr(14,2)), s = digits_value(v.substr(17,2));
    if (!y || !m || !d || !h || !mi || !s || *m < 1 || *m > 12 || *d < 1 ||
        *d > days_in_month(*y,*m) || *h > 23 || *mi > 59 || *s > 59) return {};
    return days_from_civil(*y,*m,*d)*86400000 + ((*h*60+*mi)*60+*s)*1000;
}
inline i64 parse_f1(Bytes v) {
    return v.size() == 20 && v[19] == 'Z' ? civil_ms(v.substr(0,19)).value_or(TS_BAD) : TS_BAD;
}
inline i64 parse_ts(Bytes v) {
    if (!v.empty() && std::all_of(v.begin(), v.end(), digit)) {
        u64 n = 0;
        for (char c : v) {
            const auto d = u64(c - '0');
            if (n > (u64(INT64_MAX) - d) / 10) return TS_HUGE;
            n = n * 10 + d;
        }
        return i64(n);
    }
    if (v.size() == 20) return parse_f1(v);
    if (v.size() == 25 && (v[19] == '+' || v[19] == '-') && v[22] == ':') {
        auto local = civil_ms(v.substr(0,19));
        auto oh = digits_value(v.substr(20,2)), om = digits_value(v.substr(23,2));
        if (!local || !oh || !om || *oh > 23 || *om > 59) return TS_BAD;
        auto offset = (*oh*60+*om)*60000;
        return v[19] == '+' ? *local - offset : *local + offset;
    }
    return TS_BAD;
}
template<class Fn> void events_with(Bytes buf, Fn &&f) {
    for_each_line(buf, [&](auto start, auto end) {
        auto v = split_line<7>(buf, start, end);
        if (!v || (*v)[0].first == (*v)[0].second) return;
        auto val = [&](int i) { auto [s,e]=(*v)[i]; return buf.substr(s,e-s); };
        auto kind = parse_kind(val(4));
        f(EvRow{parse_id(buf,(*v)[0],'e'), parse_id(buf,(*v)[1],'u'), parse_ts(val(2)),
                parse_f1(val(3)), kind, parse_product(val(5)), kind == KIND_PURCHASE ? parse_price(val(6)) : 0},start,end);
    });
}
template<class Fn> void clicks_with(Bytes buf, Fn &&f) {
    for_each_line(buf, [&](auto start, auto end) {
        auto v = split_line<4>(buf, start, end);
        if (!v || (*v)[0].first == (*v)[0].second) return;
        auto val = [&](int i) { auto [s,e]=(*v)[i]; return buf.substr(s,e-s); };
        f(ClRow{parse_id(buf,(*v)[0],'k'), parse_id(buf,(*v)[1],'u'), parse_campaign(val(2)),
                parse_ts(val(3))},start,end);
    });
}
inline std::vector<Bounds> event_lines(Bytes buf) {
    std::vector<Bounds> v; events_with(buf,[&](auto const&,auto s,auto e){v.emplace_back(s,e);}); return v;
}
inline std::vector<Bounds> click_lines(Bytes buf) {
    std::vector<Bounds> v; clicks_with(buf,[&](auto const&,auto s,auto e){v.emplace_back(s,e);}); return v;
}
struct Ref {
    template<class Sink> static void events(Bytes b,Sink &sink) {
        events_with(b,[&](const EvRow &r,auto,auto){event(sink,r);});
    }
    template<class Sink> static void clicks(Bytes b,Sink &sink) {
        clicks_with(b,[&](const ClRow &r,auto,auto){click(sink,r);});
    }
};
}
