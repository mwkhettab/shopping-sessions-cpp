#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <string_view>

int main(int argc,char **argv){
  if(argc!=3)return 2;
  uint32_t count{};
  std::string_view arg(argv[2]);auto [last,error]=std::from_chars(arg.begin(),arg.end(),count);
  if(error!=std::errc{} || last!=arg.end() || count<64)return 2;
  std::array<char,1<<16> buffer;
  std::ofstream out;out.rdbuf()->pubsetbuf(buffer.data(),buffer.size());out.open(argv[1],std::ios::binary);
  out<<"event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents\n";
  constexpr std::string_view payload=",u00000001,2208988800000,2040-01-01T00:00:10Z,view,p,\n";
  constexpr char hex[]="0123456789abcdef";
  std::array<char,9> key{'e'};
  for(uint32_t i=0;i<count;++i){
    for(unsigned k=0;k<8;++k)key[8-k]=hex[(i>>(4*k))&15];
    out.write(key.data(),key.size());out.write(payload.data(),payload.size());
  }
  out.close();return out?0:1;
}
