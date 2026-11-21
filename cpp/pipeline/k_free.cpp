#include "k_free.hpp"
#if defined(__x86_64__)
#include <cpuid.h>
#endif

namespace pipeline::k_free::detail {
bool available() {
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
    static const bool enabled=[]{
        unsigned a,b,c,d;
        const bool lzcnt=__get_cpuid(0x80000001,&a,&b,&c,&d) && (c&(1u<<5));
        return lzcnt && __builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi") &&
               __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("popcnt") &&
               __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") &&
               __builtin_cpu_supports("avx512vl") && __builtin_cpu_supports("avx512dq");
    }();
    return enabled;
#else
    return false;
#endif
}
}
