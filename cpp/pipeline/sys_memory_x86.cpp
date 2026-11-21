#include <cstddef>
#include <cstdint>

#if defined(__linux__) && __has_include(<features.h>)
#include <features.h>
#endif

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#include <immintrin.h>
#define SHOP_MEMORY_X86 1
#define SHOP_MEMORY_AVX2 __attribute__((target("avx2")))
#else
#define SHOP_MEMORY_X86 0
#endif

namespace {
using Byte = unsigned char;
using Size = std::size_t;
using Address = std::uintptr_t;
using CopyFn = void* (*)(void*, const void*, Size) noexcept;
using SetFn = void* (*)(void*, int, Size) noexcept;

inline bool forward_safe(const void* dest, const void* source, Size n) noexcept {
    return reinterpret_cast<Address>(dest) - reinterpret_cast<Address>(source) >= n;
}

// Volatile byte accesses keep this implementation usable inside the C runtime
// itself. Replacing either loop by a call to memcpy or memset would recurse.
void* portable_move(void* dest, const void* source, Size n) noexcept {
    auto* d = static_cast<volatile Byte*>(dest);
    const auto* s = static_cast<const volatile Byte*>(source);
    if (forward_safe(dest, source, n)) {
        for (Size i = 0; i < n; ++i) d[i] = s[i];
    } else {
        while (n != 0) { --n; d[n] = s[n]; }
    }
    return dest;
}

[[maybe_unused]] void* portable_set(void* dest, int c, Size n) noexcept {
    auto* d = static_cast<volatile Byte*>(dest);
    for (Size i = 0; i < n; ++i) d[i] = static_cast<Byte>(c);
    return dest;
}

#if SHOP_MEMORY_X86
// A baseline x86-64 host always supports REP MOVSB/STOSB. ERMS changes their
// speed. AVX2 and an enabled OS YMM save state are needed for the short paths.
__attribute__((always_inline)) inline bool has_avx2() noexcept {
    unsigned eax, ebx, ecx, edx;
    if (__get_cpuid_max(0, nullptr) < 7) return false;
    __cpuid(1, eax, ebx, ecx, edx);
    if ((ecx & (bit_OSXSAVE | bit_AVX)) != (bit_OSXSAVE | bit_AVX)) return false;
    __asm__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    if ((eax & 6) != 6) return false;
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    return (ebx & bit_AVX2) != 0;
}

inline void rep_copy(Byte* d, const Byte* s, Size n) noexcept {
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
}
inline void rep_move_back(Byte* d, const Byte* s, Size n) noexcept {
    d += n - 1;
    s += n - 1;
    __asm__ volatile("std\n\trep movsb\n\tcld"
                     : "+D"(d), "+S"(s), "+c"(n) : : "memory", "cc");
}
inline void rep_set(Byte* d, Byte b, Size n) noexcept {
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(b) : "memory");
}

template<class T> struct __attribute__((packed, may_alias)) Word { T value; };
template<class T> inline void store_word(Byte* d, T x) noexcept {
    reinterpret_cast<Word<T>*>(d)->value = x;
}
template<class T> inline void copy_ends(Byte* d, const Byte* s, Size n) noexcept {
    const T head = reinterpret_cast<const Word<T>*>(s)->value;
    const T tail = reinterpret_cast<const Word<T>*>(s + n - sizeof(T))->value;
    store_word(d, head);
    store_word(d + n - sizeof(T), tail);
}
inline auto load16(const Byte* p) noexcept {
    return _mm_loadu_si128(reinterpret_cast<const __m128i_u*>(p));
}
inline void store16(Byte* p, __m128i v) noexcept {
    _mm_storeu_si128(reinterpret_cast<__m128i_u*>(p), v);
}
SHOP_MEMORY_AVX2 inline auto load32(const Byte* p) noexcept {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i_u*>(p));
}
SHOP_MEMORY_AVX2 inline void store32(Byte* p, __m256i v) noexcept {
    _mm256_storeu_si256(reinterpret_cast<__m256i_u*>(p), v);
}

// n <= 256. Load every source block before storing any overlapping destination.
SHOP_MEMORY_AVX2 __attribute__((always_inline)) inline
void copy_small(Byte* d, const Byte* s, Size n) noexcept {
    if (n <= 16) {
        if (n >= 8) copy_ends<std::uint64_t>(d, s, n);
        else if (n >= 4) copy_ends<std::uint32_t>(d, s, n);
        else if (n >= 2) copy_ends<std::uint16_t>(d, s, n);
        else if (n == 1) *d = *s;
    } else if (n <= 32) {
        const auto a = load16(s), b = load16(s + n - 16);
        store16(d, a); store16(d + n - 16, b);
    } else if (n <= 64) {
        const auto a = load32(s), b = load32(s + n - 32);
        store32(d, a); store32(d + n - 32, b);
    } else if (n <= 128) {
        const auto a = load32(s),          b = load32(s + 32);
        const auto c = load32(s + n - 64), e = load32(s + n - 32);
        store32(d, a);          store32(d + 32, b);
        store32(d + n - 64, c); store32(d + n - 32, e);
    } else {
        const auto a = load32(s),           b = load32(s + 32);
        const auto c = load32(s + 64),      e = load32(s + 96);
        const auto f = load32(s + n - 128), g = load32(s + n - 96);
        const auto h = load32(s + n - 64),  i = load32(s + n - 32);
        store32(d, a);           store32(d + 32, b);
        store32(d + 64, c);      store32(d + 96, e);
        store32(d + n - 128, f); store32(d + n - 96, g);
        store32(d + n - 64, h);  store32(d + n - 32, i);
    }
}

SHOP_MEMORY_AVX2 void* avx_copy(void* dest, const void* source, Size n) noexcept {
    auto* d = static_cast<Byte*>(dest);
    auto* s = static_cast<const Byte*>(source);
    if (n <= 256) copy_small(d, s, n);
    else rep_copy(d, s, n);
    return dest;
}

SHOP_MEMORY_AVX2 void* avx_move(void* dest, const void* source, Size n) noexcept {
    auto* d = static_cast<Byte*>(dest);
    auto* s = static_cast<const Byte*>(source);
    if (n <= 256) copy_small(d, s, n);
    else if (forward_safe(d, s, n)) rep_copy(d, s, n);
    else rep_move_back(d, s, n);
    return dest;
}

SHOP_MEMORY_AVX2 void* avx_set(void* dest, int c, Size n) noexcept {
    auto* d = static_cast<Byte*>(dest);
    const auto b = static_cast<Byte>(c);
    if (n <= 32) {
        const auto v = std::uint64_t{0x0101010101010101} * b;
        if (n >= 16) {
            store_word(d, v);
            store_word(d + 8, v);
            store_word(d + n - 16, v);
            store_word(d + n - 8, v);
        } else if (n >= 8) {
            store_word(d, v); store_word(d + n - 8, v);
        } else if (n >= 4) {
            store_word(d, std::uint32_t(v)); store_word(d + n - 4, std::uint32_t(v));
        } else if (n >= 2) {
            store_word(d, std::uint16_t(v)); store_word(d + n - 2, std::uint16_t(v));
        } else if (n == 1) {
            *d = b;
        }
    } else if (n <= 128) {
        const auto v = _mm256_set1_epi8(static_cast<char>(b));
        store32(d, v);
        store32(d + n - 32, v);
        if (n > 64) {
            store32(d + 32, v);
            store32(d + n - 64, v);
        }
    } else {
        rep_set(d, b, n);
    }
    return dest;
}

void* x86_copy(void* d, const void* s, Size n) noexcept {
    if (n <= 256) return portable_move(d, s, n);
    rep_copy(static_cast<Byte*>(d), static_cast<const Byte*>(s), n);
    return d;
}
void* x86_move(void* d, const void* s, Size n) noexcept {
    if (n <= 256) return portable_move(d, s, n);
    if (forward_safe(d, s, n)) rep_copy(static_cast<Byte*>(d), static_cast<const Byte*>(s), n);
    else rep_move_back(static_cast<Byte*>(d), static_cast<const Byte*>(s), n);
    return d;
}
void* x86_set(void* d, int c, Size n) noexcept {
    rep_set(static_cast<Byte*>(d), static_cast<Byte>(c), n);
    return d;
}
#endif
} // namespace

#if SHOP_MEMORY_X86 && defined(__linux__) && defined(__ELF__) && defined(__GLIBC__)
// Static glibc resolves IFUNCs before normal constructors and TLS initialization.
// These resolvers access only CPUID, XGETBV and link-time function addresses.
#define SHOP_MEMORY_RESOLVER __attribute__((used, no_stack_protector, visibility("hidden")))
extern "C" SHOP_MEMORY_RESOLVER CopyFn shopping_resolve_memcpy() noexcept {
    return has_avx2() ? avx_copy : x86_copy;
}
extern "C" SHOP_MEMORY_RESOLVER CopyFn shopping_resolve_memmove() noexcept {
    return has_avx2() ? avx_move : x86_move;
}
extern "C" SHOP_MEMORY_RESOLVER SetFn shopping_resolve_memset() noexcept {
    return has_avx2() ? avx_set : x86_set;
}
#define SHOP_MEMORY_IFUNC(resolver) __attribute__((ifunc(resolver), used, visibility("default")))
extern "C" SHOP_MEMORY_IFUNC("shopping_resolve_memcpy") void* memcpy(void*, const void*, Size) noexcept;
extern "C" SHOP_MEMORY_IFUNC("shopping_resolve_memmove") void* memmove(void*, const void*, Size) noexcept;
extern "C" SHOP_MEMORY_IFUNC("shopping_resolve_memset") void* memset(void*, int, Size) noexcept;
#elif SHOP_MEMORY_X86
namespace {
bool dispatched_avx2() noexcept {
    static unsigned char saved = 0;
    static_assert(__atomic_always_lock_free(sizeof(saved), nullptr));
    const auto test = __atomic_load_n(&saved, __ATOMIC_RELAXED);
    if (test) return test == 2;
    const bool avx = has_avx2();
    __atomic_store_n(&saved, avx ? 2 : 1, __ATOMIC_RELAXED);
    return avx;
}
}
extern "C" void* memcpy(void* d, const void* s, Size n) noexcept {
    return dispatched_avx2() ? avx_copy(d, s, n) : x86_copy(d, s, n);
}
extern "C" void* memmove(void* d, const void* s, Size n) noexcept {
    return dispatched_avx2() ? avx_move(d, s, n) : x86_move(d, s, n);
}
extern "C" void* memset(void* d, int c, Size n) noexcept {
    return dispatched_avx2() ? avx_set(d, c, n) : x86_set(d, c, n);
}
#else
extern "C" void* memcpy(void* d, const void* s, Size n) noexcept { return portable_move(d, s, n); }
extern "C" void* memmove(void* d, const void* s, Size n) noexcept { return portable_move(d, s, n); }
extern "C" void* memset(void* d, int c, Size n) noexcept { return portable_set(d, c, n); }
#endif
