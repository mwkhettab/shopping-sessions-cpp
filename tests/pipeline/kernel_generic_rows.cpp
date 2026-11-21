#include "generic/driver.hpp"
#include <cstdio>

using namespace pipeline::generic;
std::string hex(B s) {
    static constexpr char digit[]="0123456789abcdef";
    std::string out;
    out.reserve(2*s.size());
    for(unsigned char c:s) {out+=digit[c>>4];out+=digit[c&15];}
    return out;
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    run_tables(argv[1],std::stoul(argv[2]),[](const UserShards &users,const Acc &acc,const DatesAndDimensions &d,std::size_t) {
        for(auto &sh:users) for(auto &r:sh) {
            std::printf("U\t%s\t%d\t%llu\t%llu\t%llu\t%s\n",hex(r.name).c_str(),r.day,
                        (unsigned long long)r.sessions,(unsigned long long)r.events,(unsigned long long)r.revenue,
                        r.top?hex(d.cat_names[*r.top]).c_str():"-");
        }
        for(auto &[k,v]:acc) {
            auto [c,day]=unkey(k);
            std::printf("C\t%s\t%s\t%d\t%llu\t%llu\t%llu\t%llu\n",hex(d.camp_ids[c]).c_str(),
                        hex(d.channels[c]).c_str(),day,(unsigned long long)v[0],(unsigned long long)v[1],
                        (unsigned long long)v[2],(unsigned long long)v[3]);
        }
    });
}
