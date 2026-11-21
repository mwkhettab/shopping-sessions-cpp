#define main unused_kernel_oracle
#include "kernel_rows.cpp"
#undef main
#ifdef SHOP_EVENT_PAIR_EMULATION
extern int general_event_oracle(int,char**);
extern "C" std::uint64_t event_pair_counts[4];
#endif
int main(int argc,char **argv) {
#ifdef SHOP_EVENT_PAIR_EMULATION
    if (general_event_oracle(argc,argv)) return 1;
#endif
    std::string collision;
    constexpr Bytes keys[]={"2024-05-02T23:34:21Z","2025-05-02T23:34:21Z","2024-02-29T02:00:00Z","2023-02-29T02:00:00Z","2026-11-02T11:13:17Z","1700000000123"};
    for(unsigned i=0;i<1800;++i) {
        auto date=std::string(keys[(i/3)%std::size(keys)]),ingest=std::string(keys[(i/4)%std::size(keys)]);
        const auto id=i%13?"eab705c83":"";
        collision+=std::string(id)+",u14b28a21,"+date+","+ingest+(i%7?",view,p359,\n":",purchase,\"p17\",81\n");
        if(i%61==0)collision+="\n";
        if(i%109==0) collision+=std::string(211,'x')+'\n';
    }
    for(unsigned start=0;start<NB;++start) {
        Rows ref,actual;
        auto seed_rows=[&](Rows& r){r.eb.n=start;for(unsigned i=0;i<start;++i){r.eb.eid[i]=17+i;r.eb.uid[i]=13+i;r.eb.ing[i]=47+i;r.eb.ts[i]=79+i;r.eb.pp[i]=33+i;}};
        seed_rows(ref);seed_rows(actual);
        collect<true,k_ref::Ref>(collision,ref);ref.ev_flush();
        collect<true,k_free::Free>(collision,actual);
        if(ref.rows!=actual.rows || ref.groups!=actual.groups) return 2;
    }
#ifdef SHOP_EVENT_PAIR_EMULATION
    std::printf("pair calls %llu, first row flushes %llu, first row dropped %llu, preparation misses %llu; all 64 initial occupancies and colliding dates matched\n",
        static_cast<unsigned long long>(event_pair_counts[0]),static_cast<unsigned long long>(event_pair_counts[1]),
        static_cast<unsigned long long>(event_pair_counts[2]),static_cast<unsigned long long>(event_pair_counts[3]));
    return !(event_pair_counts[0] && event_pair_counts[1] && event_pair_counts[2] && event_pair_counts[3]);
#else
    std::puts("all 64 initial occupancies and colliding dates matched reference rows and flush boundaries");
    return 0;
#endif
}
