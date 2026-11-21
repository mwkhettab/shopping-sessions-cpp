// Shared by the x86 target-specific entry points in marked_events.hpp.
  constexpr unsigned Bytes = 2048, N = 7;
  constexpr unsigned Words = Bytes / 64;
  alignas(64) std::array<uint16_t, Bytes + 64> positions;
  positions[0] = 0;
  uint16_t *cuts = positions.data() + 1;
  while (data.size() >= Bytes) {
    const char *text = data.data();
    std::array<OrdinalMasks, Words> masks;
    for (unsigned k = 0; k < Words; ++k) {
      masks[k] = classify(text + 64 * k);
    }
    unsigned words = Words;
    while (words && !masks[words - 1].newlines)
      --words;
    if (!words) {
      auto lf = data.find('\n', Bytes);
      size_t take = lf == Str::npos ? data.size() : lf + 1;
      scalar_rows<N>(data.substr(0, take), fn);
      data.remove_prefix(take);
      continue;
    }
    auto &last = masks[words - 1];
    unsigned length = words * 64 - std::countl_zero(last.newlines);
    uint64_t tail = mask_to(length - (words - 1) * 64);
    last.separators &= tail;
    last.quotes &= tail;
    uint64_t inside = 0, previous = 1, invalid = 0;
    unsigned nc = 0;
    for (unsigned b = 0; b < words; ++b) {
      auto [sep, q, lf] = masks[b];
      unsigned count = std::popcount(sep);
      uint64_t want = ordinal_newlines<N>[nc % N] & mask_to(count);
      invalid |= _pext_u64(lf, sep) ^ want;
      uint64_t closed = (q << 1) | (b ? (masks[b - 1].quotes >> 63) : 0);
      uint64_t parity = quote_prefix(q) ^ inside;
      uint64_t next = b + 1 < words ? (masks[b + 1].separators & 1) : 1;
      invalid |= ((q & parity) & ~((sep << 1) | previous)) |
                 ((q & ~parity) & ~((sep >> 1) | (next << 63))) |
                 (sep & parity);
      inside = uint64_t(0) - (parity >> 63);
      previous = sep >> 63;
      save_positions(cuts + nc, 64 * b, sep, closed);

      nc += count;
    }
    if (invalid || inside || nc % N) {
      tiled_lines<N, 16>(data.substr(0, length), fn);
    } else {
      unsigned i=0;
      if constexpr (requires { fn.common(text, 2u, text, 2u, Str{}, Str{}); }) {
        for (; nc-i >= 8*N; i+=8*N)
          emit_event_batch(text, cuts+i, fn);
      }
      for (; i < nc; i+=N)
        emit_marked_event(MarkedEventFields{text,cuts+i},fn);
    }
    data.remove_prefix(length);
  }
  tiled_lines<N, 16>(data, fn);
