"""Compare decoding, samples, recovered keys and search steps for independently salted inputs."""
import argparse
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
MASK=(1<<40)-1
def spelling(n,key):
    m=((n*0x9e3779b97f4b)^key)&MASK
    x=((m^(m>>17))*0xd6e8feb86659)&MASK
    s=''
    while not s or x:
        x,d=divmod(x,36);s='0123456789abcdefghijklmnopqrstuvwxyz'[d]+s
    return s

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('rust_src',type=Path)
    opt=ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        w=Path(tmp)
        shutil.copy2(opt.rust_src/'keyrec.rs',w/'keyrec.rs')
        shutil.copy2(HERE/'keys_oracle.rs',w/'main.rs')
        subprocess.run([os.environ.get('RUSTC','rustc'),'--edition=2021','-O',w/'main.rs','-o',w/'rs'],check=True)
        subprocess.run([os.environ.get('CXX','c++'),'-O2','-std=c++20',HERE/'keys.cpp','-o',w/'cpp'],check=True)
        for seed in (51821,90377,114245,290021):
            rng=random.Random(seed)
            uk,ek,ck=(rng.randrange(1<<40) for _ in range(3))
            def make(i,click):
                group=0 if i%8==0 else 1+rng.randrange(600)
                uid=spelling(group*512,uk)
                if seed==290021:uid=spelling(rng.randrange(1<<39),uk)
                if i%97==0:uid='opaque-'+uid
                key=spelling(i,ck if click else ek)
                if click:return f'k{key},u{uid},c_13,2026-11-21T20:00:00Z\n'
                return f'e{key},u{uid},{i},2026-11-21T20:00:00Z,view,p1,\n'
            for name,click in [('e',False),('c',True)]:
                rows=[make(i,click) for i in range(3500)]
                rng.shuffle(rows);(w/name).write_text(''.join(rows))
            args=[w/'e',w/'c']
            a=subprocess.check_output([w/'rs',*args]);b=subprocess.check_output([w/'cpp',*args])
            assert a==b,(seed,a[-48:].hex(),b[-48:].hex())
            keys=struct.unpack('<6Q',a[-48:])[:3]
            assert keys==((uk,ek,ck) if seed!=290021 else (2**64-1,)*3),(seed,keys)
            print(seed,len(a),'identical bytes',flush=True)
if __name__=='__main__':main()
