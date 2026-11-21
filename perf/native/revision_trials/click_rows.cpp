#define main original_rows_main
#include "kernel_rows.cpp"
#undef main

int main() {
    if (!pipeline::k_free::detail::available()) return 77;
    cases<false>();
    const std::string good="kabcdef12,u12345678,c_4321,2024-02-29T03:04:05Z\n";
    std::string data;
    for (const auto &raw : {good, std::string("\"kabcdef12\",\"u12345678\",\"c_4321\",\"2024-02-29T03:04:05+23:59\"\n"),
                            std::string("k1,u0,c_0,1700000000123\n")}) {
        for (std::size_t p=0;p<raw.size();++p) {
            data.clear();
            for (unsigned i=0;i<80;++i) data+=good;
            for (unsigned b=0;b<256;++b) {
                auto m=raw; m[p]=char(b);
                if (b&1) data+=good;
                data+=m;
                if (b%5==0) data+=m;
            }
            guarded<false>(data);
        }
    }
    data.clear();
    for (unsigned k=0; k<=64; ++k) for (unsigned u=0;u<=64;++u) {
        data+=std::string(k,'k')+","+std::string(u,'u')+",c_1,2024-02-29T18:09:01Z\n";
        if ((k+u)%4==0) data+=good;
    }
    guarded<false>(data);
    for(unsigned i=0;i<64;++i) {
        std::string packet;
        for (unsigned j=0;j<193;++j) {
            if(j==i || j==64+i) packet+="k,+,C,2024-02-29T12:34:56+24:00\n";
            else packet+=good;
        }
        guarded<false>(packet);
    }
    std::puts("Native paired click rows, arbitrary structure, clock mutations, guarded boundaries and all 64 flush offsets matched.");
    return 0;
}
