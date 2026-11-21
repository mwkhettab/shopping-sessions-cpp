#pragma once
#include <chrono>
#include <climits>
#include <mutex>
#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif
namespace pipeline::sys {
inline u64 ticks() {
#if defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#else
    return u64(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}
inline void spin_loop() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}
struct WorkerPool {
    std::atomic<u32> generation{};
    std::atomic<std::size_t> done{}, sleepers{}, count{};
    void (*invoke)(void*,std::size_t){};
    void *job{};
    std::size_t job_nt{};
    std::vector<std::size_t> cpus;
};
inline WorkerPool &worker_pool() {
    // The production process exits directly after its last output write.
    // Detached workers can still be parked while static objects are destroyed in a unit test.
    static WorkerPool *pool=new WorkerPool;
    return *pool;
}
inline void wake_workers(WorkerPool &p) {
#if defined(__linux__)
    static_assert(std::atomic<u32>::is_always_lock_free && sizeof(std::atomic<u32>)==sizeof(u32));
    (void)::syscall(SYS_futex,&p.generation,FUTEX_WAKE_PRIVATE,INT_MAX,nullptr,nullptr,0);
#else
    p.generation.notify_all();
#endif
}
inline void wait_worker(WorkerPool &p,u32 seen) {
#if defined(__linux__)
    (void)::syscall(SYS_futex,&p.generation,FUTEX_WAIT_PRIVATE,seen,nullptr,nullptr,0);
#else
    p.generation.wait(seen,std::memory_order_acquire);
#endif
}
inline void worker(std::size_t tid,std::size_t nt) {
    auto &p=worker_pool();
    for (auto child:{2*tid,2*tid+1}) if (child<nt) std::thread(worker,child,nt).detach();
    if (PIN_AT_START.load(relaxed) && tid<PINNED.size() && !p.cpus.empty() && pin_to(p.cpus[tid%p.cpus.size()]))
        PINNED[tid].store(true,std::memory_order_release);
    if (PREFAULT.load(relaxed)) prefault_chunk();
    const auto spin=SPIN_TICKS.load(relaxed);
    u32 seen=0;
    for (;;) {
        const auto start=ticks();
        for (;;) {
            u32 current=p.generation.load(std::memory_order_acquire);
            if (current!=seen) {seen=current; break;}
            if (ticks()-start<spin) {spin_loop(); continue;}
            p.sleepers.fetch_add(1);
            wait_worker(p,seen);
            p.sleepers.fetch_sub(1);
        }
        if (tid<p.job_nt) p.invoke(p.job,tid);
        p.done.fetch_add(1,std::memory_order_acq_rel);
    }
}
inline void pool_start(std::size_t nt) {
    auto &p=worker_pool();
    if (nt<2 || p.count.load()!=0) return;
    WORKERS.store(true); p.count.store(nt-1);
    std::thread(worker,1,nt).detach();
}
inline std::size_t pool_size() {return worker_pool().count.load();}
template<class Fn> void run_par(std::size_t nt,Fn &&work) {
    WORKERS.store(true);
    auto &p=worker_pool();
    auto n=p.count.load();
    if (!n || nt>n+1) {
        std::vector<std::thread> workers;
        workers.reserve(nt?nt-1:0);
        for (std::size_t tid=1;tid<nt;++tid) workers.emplace_back(std::ref(work),tid);
        work(0);
        for (auto &w:workers) w.join();
        return;
    }
    using F=std::remove_reference_t<Fn>;
    p.job=const_cast<void*>(static_cast<const void*>(std::addressof(work)));
    p.invoke=[](void *job,std::size_t tid){(*static_cast<F*>(job))(tid);};
    p.job_nt=nt;
    p.done.store(0); p.generation.fetch_add(1);
    if (p.sleepers.load()!=0) wake_workers(p);
    work(0);
    while (p.done.load(std::memory_order_acquire)!=n) spin_loop();
}
using Instant=std::chrono::steady_clock;
inline auto last_lap=Instant::now();
inline std::vector<std::pair<std::string,double>> laps;
inline void lap(const char *name) {
    auto now=Instant::now();
    laps.emplace_back(name,std::chrono::duration<double,std::milli>(now-last_lap).count());
    last_lap=now;
}
}
