// Training-only linker wrappers. The released executable contains none of these symbols.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
extern "C" void __gcov_dump();
extern "C" [[noreturn]] void __real__exit(int);
extern "C" int __real_execv(const char*,char* const[]);
extern "C" bool actual_reader() asm("__real__ZN8pipeline6k_free6detail9availableEv");
extern "C" bool training_reader() asm("__wrap__ZN8pipeline6k_free6detail9availableEv");
static std::atomic<unsigned> wide{},portable{};
// The release intentionally leaves its worker pool parked at process exit. Let
// detached workers reach their futexes before libgcov snapshots their counters.
static int parked() {
    DIR* tasks=::opendir("/proc/self/task");
    if (!tasks) return -1;
    int count=0;
    while (auto* e=::readdir(tasks)) {
        if (e->d_name[0]<'0' || e->d_name[0]>'9' || std::atoi(e->d_name)==::getpid()) continue;
        FILE* f;
        char path[128],line[512];
        std::snprintf(path,sizeof(path),"/proc/self/task/%d/stat",std::atoi(e->d_name));
        if (!(f=std::fopen(path,"r"))) continue;
        const auto* got=std::fgets(line,sizeof(line),f); std::fclose(f);
        const auto* end=got ? std::strrchr(line,')') : nullptr;
        if (!end || end[2]!='S') {count=-1;break;}
        ++count;
    }
    ::closedir(tasks); return count;
}
extern "C" bool training_reader() {
    static const bool force=[] { auto* p=std::getenv("SHOP_PGO_FIELD_SCALAR"); return p && !std::strcmp(p,"1"); }();
    const bool enabled=actual_reader() && !force;
    (enabled?wide:portable).fetch_add(1,std::memory_order_relaxed);
    return enabled;
}
extern "C" [[noreturn]] void __wrap__exit(int code) {
    if (!code) {
        int waiting=-1;
        for (int i=0;i<2000 && (waiting=parked())<0;++i) {
            const timespec pause{0,1000000}; ::nanosleep(&pause,nullptr);
        }
        std::fprintf(stderr,"[pgo] exit hardware=%u wide=%u portable=%u parked=%d\n",
                     unsigned(actual_reader()),wide.load(),portable.load(),waiting);
        if (waiting<0) __real__exit(80);
        __gcov_dump();
    }
    __real__exit(code);
}
extern "C" int __wrap_execv(const char* path,char* const argv[]) {
    if (argv && argv[0] && argv[1] && !std::strcmp(argv[1],"--generic")) __gcov_dump();
    return __real_execv(path,argv);
}
