#include "input.hpp"
#include <cstdio>
int main(int argc,char **argv) {
  if (argc != 4) return 2;
  Input in;
  const fs::path data=argv[1];
  const Str mode=argv[2];
  const unsigned workers=std::stoul(argv[3]);
  std::vector<std::pair<InputChunk,unsigned>> chunks;
  for (auto [name,fields] : {std::pair{"events",7u},{"clicks",4u},{"links",2u}})
    for (auto part:in.parts(data/name)) chunks.emplace_back(part,fields);
  for (auto name:{"products.csv","campaigns.csv"}) {
    auto bytes=in.map(data/name);
    if (!bytes.empty()) chunks.emplace_back(InputChunk{bytes,0,bytes.size()},2);
  }
  struct alignas(64) Count { uint64_t sum{}, bytes{}; };
  std::vector<Count> results(workers);
  parallel(chunks.size(),workers,[&](size_t i,unsigned t) {
    const auto &[chunk,n]=chunks[i];
    auto bytes=chunk.file.substr(chunk.begin,chunk.size());
    uint64_t local=0;
    if (mode=="read") {
      size_t j=0;
      for (;j+8<=bytes.size();j+=8) {
        uint64_t word;
        std::memcpy(&word,bytes.data()+j,8);
        local+=word;
      }
      for (;j<bytes.size();++j) local+=uint8_t(bytes[j]);
    } else {
      auto count=[&]<size_t N>() {
        rows<N>(chunk.body(),[&](const auto &row) {
          for (size_t k=0;k<N;++k) local+=row[k].size();
        });
      };
      if (n==7) count.template operator()<7>();
      else if (n==4) count.template operator()<4>();
      else count.template operator()<2>();
    }
    results[t].sum+=local;
    results[t].bytes+=bytes.size();
  });
  Count all{};
  for (auto r:results) {all.sum+=r.sum;all.bytes+=r.bytes;}
  std::cout<<all.bytes<<" "<<all.sum<<"\n";
}
