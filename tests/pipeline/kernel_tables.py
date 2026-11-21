"""Check every logical field in the scalar, vector and assembler lookup tables."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

TYPES = {
    "IdShape":"ctrl subv lz orv", "JidRow":"ctrl subv lz orv",
    "DecShape":"ctrl subv lz", "TsA":"ctrl base lim w1",
    "TsB":"w2 w3 kmaskv limv", "TsC":"koff soff dmask me mf",
    "TjA":"ctrl base", "TjB":"lim w1", "TjC":"w2 w3", "TjD":"limv kmask2 kmask3",
    "TjE":"kme kmf", "TjF":"ksel koff1 pmask pmask3", "TjRow":"a b c d e f",
    "KindRow":"cand maskv k","KindRow2":"cand maskv pp",
    "TailRow":"sel m pat pp",
    "Tables":"tsa tsb tsc tj kind kind2 tail eid kid uid eid1 jid kid1 uid1 prod camp camp4",
    "CtsRow":"ctrl base lim w1 w2 w3 limv kmask2 kme kmf ksel koff1 pmask"
}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("rust_src",type=Path)
    a=p.parse_args()
    source=(a.rust_src/"k_free.rs").read_text()
    structs=source[source.rindex("#[repr(C, align(64))]",0,source.index("struct IdShape")):
                   source.rindex("#[repr(C, align(32))]",0,source.index("struct DateEnt"))]
    clicks=source[source.rindex("#[repr(C, align(64))]",0,source.index("struct CtsRow")):
                  source.index("const C_CAMP:")]
    with tempfile.TemporaryDirectory() as td:
        work=Path(td)
        rs = ('#![allow(dead_code,long_running_const_eval)]\nuse std::io::Write;\n'
              'const PP_SOME:u64=u32::MAX as u64-1; const PP_NONE:u64=1u64<<63 | u32::MAX as u64;'
              'const DATE_PEXT:u64=0x0f03;\n'+structs+clicks+
              'trait Dump {fn dump(&self,w:&mut Vec<u8>);}\n')
        for t in ('u8','i8','u16','i16','u32','i32','u64','i64'):
            rs+='impl Dump for '+t+'{fn dump(&self,w:&mut Vec<u8>){w.extend_from_slice(&self.to_le_bytes());}}\n'
        rs+='impl<T:Dump,const N:usize> Dump for [T;N]{fn dump(&self,w:&mut Vec<u8>){for v in self{v.dump(w);}}}\n'
        for t,fs in TYPES.items():
            rs+=f'impl Dump for {t}{{fn dump(&self,w:&mut Vec<u8>){{'+''.join(f'self.{f}.dump(w);' for f in fs.split())+'}}\n'
        rs+='fn main(){ let mut b=Vec::new();T.dump(&mut b);CTS.dump(&mut b);CAMP5R.dump(&mut b);std::io::stdout().write_all(&b).unwrap();}'
        (work/'tables.rs').write_text(rs)
        cc = ('#include "k_free_tables.hpp"\n#include <cstdio>\n#include <concepts>\n'
              'using namespace pipeline::k_free::detail;\n'
              'template<std::integral T> void dump(T v){using U=std::make_unsigned_t<T>;for (unsigned i=0;i<sizeof(T);++i)std::putchar((U(v)>>(8*i))&255);}\n')
        for t in TYPES: cc+=f'void dump(const {t}&);\n'
        cc+='template<class T,std::size_t N> void dump(const std::array<T,N>& a){for(auto &v:a)dump(v);}\n'
        for t,fs in TYPES.items():
            cc+=f'void dump(const {t}& r){{'+''.join(f'dump(r.{f});' for f in fs.split())+'}\n'
        cc+='int main(){dump(T);dump(CTS);dump(CAMP5R);}\n'
        (work/'tables.cpp').write_text(cc)
        subprocess.run([os.environ.get("RUSTC","rustc"),"--edition=2021","-O",work/'tables.rs','-o',work/'reference'],check=True)
        inc=Path(__file__).resolve().parents[2]/'cpp/pipeline'
        subprocess.run([os.environ.get("CXX","c++"),"-std=c++20","-O2","-I",inc,work/'tables.cpp','-o',work/'candidate'],check=True)
        expect=subprocess.check_output([work/'reference'])
        got=subprocess.check_output([work/'candidate'])
        if expect!=got:
            at=next((i for i,(v,w) in enumerate(zip(expect,got)) if v!=w),min(len(expect),len(got)))
            raise ValueError(f"Table byte {at} differs, lengths {len(expect)} and {len(got)}")
        print(len(got),"identical table bytes,",hashlib.sha256(got).hexdigest())

if __name__=="__main__":
    main()
