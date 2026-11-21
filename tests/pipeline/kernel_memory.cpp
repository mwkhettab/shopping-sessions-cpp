#include "../../cpp/pipeline/sys_memory_x86.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <sys/mman.h>
#include <unistd.h>

// The oracle derives each byte from its original position, including overlapping
// moves. It never uses memcpy, memset or memmove to construct expected results.
namespace {
struct Operations { const char* name; CopyFn copy, move; SetFn fill; };
struct Guarded {
    Byte* map;
    Byte* data;
    Size size;
    Guarded() : size(static_cast<Size>(sysconf(_SC_PAGESIZE))) {
        map = static_cast<Byte*>(mmap(nullptr, 3*size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0));
        if (map == MAP_FAILED) std::abort();
        data = map + size;
        if (mprotect(data, size, PROT_READ|PROT_WRITE)) std::abort();
    }
    ~Guarded() { munmap(map, 3*size); }
};

const char* label;
Size checks;
[[noreturn]] void fail(const char* op, Size n, Size from, Size to, Size at) {
    std::fprintf(stderr, "%s: %s n=%zu from=%zu to=%zu mismatch at %zu\n", label, op, n, from, to, at);
    std::abort();
}
Byte sample(Size at, unsigned seed) {
    std::uint32_t x = static_cast<std::uint32_t>(at) + seed;
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b;
    return static_cast<Byte>(x ^ (x >> 16));
}
void seed_region(Byte* data, Size size, unsigned seed) {
    for (Size i = 0; i < size; ++i) data[i] = sample(i, seed);
}

void copy_case(CopyFn fn, const Guarded& input, Guarded& out, Size s, Size d, Size n) {
    const Size start = d > 32 ? d - 32 : 0;
    const Size end = d + n + 32 < out.size ? d + n + 32 : out.size;
    for (Size i = start; i < end; ++i) out.data[i] = sample(i, 11);
    if (fn(out.data + d, input.data + s, n) != out.data + d) fail("copy return", n, s, d, 0);
    for (Size i = start; i < end; ++i) {
        Byte want = (i >= d && i - d < n) ? sample(s + (i - d), 73) : sample(i, 11);
        if (out.data[i] != want) fail("copy", n, s, d, i);
    }
    ++checks;
}
void move_case(CopyFn fn, Guarded& page, Size s, Size d, Size n) {
    const Size lo = s < d ? s : d, hi = (s > d ? s : d) + n;
    const Size start = lo > 32 ? lo - 32 : 0, end = hi + 32 < page.size ? hi + 32 : page.size;
    for (Size i = start; i < end; ++i) page.data[i] = sample(i, 37);
    if (fn(page.data + d, page.data + s, n) != page.data + d) fail("move return", n, s, d, 0);
    for (Size i = start; i < end; ++i) {
        Byte want = sample(i >= d && i - d < n ? s + (i - d) : i, 37);
        if (page.data[i] != want) fail("move", n, s, d, i);
    }
    ++checks;
}
void set_case(SetFn fn, Guarded& out, Size d, Size n, int value) {
    const Size start = d > 32 ? d - 32 : 0;
    const Size end = d + n + 32 < out.size ? d + n + 32 : out.size;
    for (Size i = start; i < end; ++i) out.data[i] = sample(i, 11);
    if (fn(out.data + d, value, n) != out.data + d) fail("set return", n, 0, d, 0);
    for (Size i = start; i < end; ++i) {
        Byte want = (i >= d && i - d < n) ? static_cast<Byte>(value) : sample(i, 11);
        if (out.data[i] != want) fail("set", n, 0, d, i);
    }
    ++checks;
}

void check(Operations op) {
    Guarded input, out;
    label = op.name;
    checks = 0;
    seed_region(input.data, input.size, 73);
    for (Size n = 0; n <= 1100; ++n) {
        for (Size j = 0; j < 32; ++j) {
            copy_case(op.copy, input, out, 64 + j, 32 + ((5*j+7) % 32), n);
            for (int value : {0, -98, 255, 256})
                set_case(op.fill, out, 32 + j, n, value);
        }
        for (Size k : {0,1,2,3,7,15,16,17,31,32,33,63,64,65,95,127,128,129,255,256,257}) {
            move_case(op.move, out, 3, 3+k, n);
            move_case(op.move, out, 3+k, 3, n);
        }
    }
    for (Size n = 0; n <= input.size; ++n) {
        if (n > 300 && n < input.size-5 && n % 129 != 0) continue;
        for (Size s : {Size(0), input.size-n}) for (Size d : {Size(0), out.size-n})
            copy_case(op.copy, input, out, s, d, n);
        for (Size pos : {Size(0), out.size-n}) {
            move_case(op.move, out, pos, pos, n);
            for (int c : {-1, 0, 0x180}) set_case(op.fill, out, pos, n, c);
        }
        const Size remainder = out.size-n;
        Size shift = remainder < 17 ? remainder : 17;
        move_case(op.move, out, remainder-shift, remainder, n);
        move_case(op.move, out, remainder, remainder-shift, n);
        move_case(op.move, out, 0, shift, n);
        move_case(op.move, out, shift, 0, n);
    }
    for (Size i = 0; i < input.size; ++i)
        if (input.data[i] != sample(i, 73)) fail("source modified", input.size, 0, 0, i);
    std::printf("%s: %zu checked copy, move and fill operations\n", op.name, checks);
}
} // namespace

int main() {
    check({"public symbols", &memcpy, &memmove, &memset});
    check({"portable", portable_move, portable_move, portable_set});
#if SHOP_MEMORY_X86
    check({"baseline x86 REP", x86_copy, x86_move, x86_set});
    if (has_avx2()) check({"AVX2", avx_copy, avx_move, avx_set});
    else std::puts("AVX2 unavailable on this host");
#endif
}
