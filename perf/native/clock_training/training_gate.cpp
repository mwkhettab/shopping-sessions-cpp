// This object is linked only into the instrumented training executable.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

extern "C" void __llvm_profile_set_filename(const char *);
extern "C" int __llvm_profile_write_file();
extern "C" [[noreturn]] void __real__exit(int);
extern "C" int __real_execv(const char *, char *const[]);
extern "C" bool reader_hardware() asm("_ZN8pipeline6k_free6detail9availableEv");
static std::atomic<unsigned> wide{}, portable{};

// Full LTO inlines the CPU query. Probe the dispatch at the two actual field
// call sites in the training source instead of wrapping an inlinable symbol.
extern "C" bool shop_training_reader(bool hardware) {
    static const bool force = [] {
        const char *value = std::getenv("SHOP_PGO_FIELD_SCALAR");
        return value && !std::strcmp(value, "1");
    }();
    const bool enabled = hardware && !force;
    (enabled ? wide : portable).fetch_add(1, std::memory_order_relaxed);
    return enabled;
}

static int parked_peers() {
    DIR *tasks = ::opendir("/proc/self/task");
    if (!tasks) return -1;
    int count = 0;
    while (auto *e = ::readdir(tasks)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        const int tid = std::atoi(e->d_name);
        if (tid == ::getpid()) continue;
        char path[128], line[512];
        std::snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
        FILE *f = std::fopen(path, "r");
        if (!f) continue;
        const auto *got = std::fgets(line, sizeof(line), f);
        std::fclose(f);
        const auto *end = got ? std::strrchr(line, ')') : nullptr;
        if (!end || end[2] != 'S') { count = -1; break; }
        ++count;
    }
    ::closedir(tasks);
    return count;
}

static void write_profile(const char *stage) {
    const char *dir = std::getenv("SHOP_PROFILE_DIR");
    const char *name = std::getenv("SHOP_PGO_CASE");
    char path[1024];
    if (!dir || !name || !*name || std::strspn(name, "abcdefghijklmnopqrstuvwxyz") != std::strlen(name))
        __real__exit(81);
    const int bytes = std::snprintf(path, sizeof(path), "%s/%s.%s.profraw", dir, name, stage);
    if (bytes <= 0 || std::size_t(bytes) >= sizeof(path)) __real__exit(81);
    __llvm_profile_set_filename(path);
    if (__llvm_profile_write_file()) __real__exit(82);
}

extern "C" [[noreturn]] void __wrap__exit(int code) {
    if (!code) {
        int waiting = -1;
        for (int i = 0; i < 2000 && (waiting = parked_peers()) < 0; ++i) {
            const timespec pause{0, 1000000};
            ::nanosleep(&pause, nullptr);
        }
        std::fprintf(stderr, "[pgo] exit hardware=%u wide=%u portable=%u parked=%d\n",
                     unsigned(reader_hardware()), wide.load(), portable.load(), waiting);
        if (waiting < 0) __real__exit(80);
        write_profile("exit");
    }
    __real__exit(code);
}

extern "C" int __wrap_execv(const char *path, char *const argv[]) {
    if (argv && argv[0] && argv[1] && !std::strcmp(argv[1], "--generic"))
        write_profile("exec");
    return __real_execv(path, argv);
}
