#define main baseline_kernel_tests
#include "kernel_rows.cpp"
#undef main
#include <fstream>
#include <sstream>
int main(int argc,char** argv) {
    cases<true>();
    const std::string plain="eab705c83,u14b28a21,2024-05-02T23:34:21Z,2024-05-03T00:01:02Z,view,p359,\n";
    const auto pos=plain.find("23:34");
    std::string dirty=plain;dirty.replace(pos,5,"25:90");
    std::string dataset;
    for(unsigned repeat=0;repeat<25;++repeat) {
        for(unsigned i=0;i<129;++i) dataset+=plain;
        for(int len:{0,1,2,15,31,32,33,34,62,63,64,65,94,95,96,97,1023,1024,1025,4097}) {
            dataset+=std::string(len,',')+'\n';
            dataset+=std::string(len,'"')+'\n';
            dataset+=std::string(len,'\0')+'\n';
        }
        dataset+=dirty;
        dataset+="\"embedded,comma\",u1,1,1,view,p1,\n\"x\"\"y\",u,1,2,view,\"weird,product\",\"1,2\"\r\n";
        dataset+=std::string(4096,'x')+'\n';
    }
    guarded<true>(dataset);
    for (unsigned i=0;i<64;++i) {
        auto split=std::string_view(dataset).substr(i,20000+i*127);
        guarded<true>(split);
    }
    for(int i=1;i<argc;++i) {
        std::ifstream f(argv[i],std::ios::binary);std::ostringstream s;s<<f.rdbuf();
        if(!f)return 2;
        guarded<true>(s.str());
    }
    std::puts("event rows and every 64-row flush matched the independent reference, including guard edges, cold restarts, all 64 alignments, and input samples");
}
