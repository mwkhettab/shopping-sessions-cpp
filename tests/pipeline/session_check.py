"""Run the session state machine and inspect every emitted column and campaign cell."""
import argparse
import os
from pathlib import Path
import platform
import random
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
GAP=1800000
DAY=86400000
WEEK=7*DAY

def data(seed):
    rnd=random.Random(seed)
    def event(t,price=0,category=0,alias=0):
        return (t<<33) | (price<<9) | (category<<4) | alias
    def clicks(ts):
        return sorted([((t<<32)|(c<<4)|a,rank) for t,c,a,rank in ts],key=lambda x:(x[0]>>32,-x[1]))
    people=[]
    def add(mode,u,mask,ev,ck,arrays=()):
        words=[mode,u,mask,len(ev),len(ck),*ev]
        for x in ck:words.extend(x)
        for a in arrays:words.extend([len(a),*a])
        people.append(words)
    ev=[event(0,20,2),event(GAP,20,1),event(2*GAP+1,9,8),event(DAY,3,1)]
    ck=clicks([(0,2,0,1),(WEEK,2,4,5),(WEEK,5,8,1),(WEEK+1,7,1,4),
               (WEEK+2*GAP+1,4,6,2),(DAY+WEEK+1,9,8,7)])
    add(0,1,0,ev,ck);add(1,2,3,ev,ck)
    add(3,3,1,[event(DAY),event(3*DAY,9,1),event(DAY+2000,10,5)],[])
    se=[(3<<32)|GAP,2*GAP,(2<<32)|DAY,DAY+200]
    pu=[(40<<5)|3,(1<<32)|(25<<5)|2,(1<<32)|(15<<5)|1]
    ex=[event(0,1,1,3),event(GAP,7,4,8),event(GAP+9,0,0,2),event(3*GAP+1),event(DAY+200),event(2*DAY,44,1)]
    add(2,4,0,[ex[0]],ck,[se,pu,ex])
    for u in range(5,185):
        n=rnd.choice([1,7,8,9,16,40,192])
        ev=[]; t=rnd.randrange(3*DAY)
        for _ in range(n):
            t+=rnd.choice([0,5,700,GAP-1,GAP,GAP+1,4000000])
            ev.append(event(t,rnd.choice([0,0,1,774,100000]),rnd.randrange(32),rnd.randrange(14)))
        ck=clicks([(rnd.randrange(WEEK+t+1),rnd.randrange(14),rnd.randrange(14),j)
                   for j in range(rnd.choice([0,1,16,370]))])
        add(u%2,u,rnd.randrange(1<<14),ev,ck)
    header=[123456891012,15,24,DAY-10000,20817,len(people)]
    return ' '.join(map(str,header+[n for person in people for n in person])).encode()


def main():
    p=argparse.ArgumentParser()
    p.add_argument('rust_src',type=Path)
    args=p.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        w=Path(folder)
        src=(args.rust_src/'core.rs').read_text()
        if platform.machine() not in ('x86_64','amd64','AMD64'):
            # The recorded source builds exclusively for x86. On other architectures,
            # its own scalar tail covers the entire slice, preserving event-by-event order.
            src=src.replace('use std::arch::x86_64::*;', '#[cfg(target_arch="x86_64")] use std::arch::x86_64::*;')
            src=src.replace('unsafe {\n            if n >= 8 {','#[cfg(target_arch="x86_64")] unsafe {\n            if n >= 8 {')
        (w/'session.rs').write_text(src)
        shutil.copy2(args.rust_src/'keyrec.rs',w)
        main_src=(args.rust_src/'main.rs').read_text()
        (w/'names.rs').write_text(main_src[main_src.index('static PAIR36:'):main_src.index('#[inline(always)]\npub fn shard')])
        driver=(HERE/'session_core.rs').read_text()
        if platform.machine() in ('x86_64','amd64','AMD64'):
            driver='#![feature(stdarch_x86_avx512)]\n'+driver
        (w/'run.rs').write_text(driver)
        env=os.environ.copy()
        env['RUSTC_BOOTSTRAP']='1'
        rust=[os.environ.get('RUSTC','rustc'),'--edition=2021','-Awarnings','-O']
        if platform.machine() in ('x86_64','amd64','AMD64'):
            rust.extend(['-C','target-feature=+avx512f,+avx512dq'])
        subprocess.run([*rust,w/'run.rs','-o',w/'rust'],check=True,env=env)
        subprocess.run([os.environ.get('CXX','c++'),'-O2','-std=c++20',HERE/'session_core.cpp','-o',w/'cpp'],check=True)
        for seed in (771,19063,800721):
            blob=data(seed)
            a=subprocess.check_output([w/'rust'],input=blob)
            b=subprocess.check_output([w/'cpp'],input=blob)
            if a!=b: raise RuntimeError(f'Session columns differ on seed {seed}')
            print(f'seed {seed}: {len(a)} matching bytes')
if __name__=='__main__':main()
