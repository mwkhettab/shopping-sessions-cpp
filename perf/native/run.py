"""Run a serialized native-x64 comparison on public, generated inputs."""
import json
import os
import platform
import pwd
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

root=Path(__file__).resolve().parents[2]
area=Path(os.environ['RUNNER_TEMP'])/'shopping-trials'
area.mkdir(exist_ok=True)
outcomes=area/'results'
outcomes.mkdir(exist_ok=True)
python=sys.executable
env=dict(os.environ,NCPU=str(min(4,len(os.sched_getaffinity(0)))),POLARS_MAX_THREADS='2',PYTHONUNBUFFERED='1')
notes=[]
def run(*args,cwd=root,extra=None):
    return subprocess.run(args,check=True,cwd=cwd,env=env|(extra or {}))
def note(title,message):
    print(title,message,flush=True)
    notes.append(f'{title} {message}')
    with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as summary: print(f'- **{title}** {message}',file=summary)

def main():
    cpu=Path('/proc/cpuinfo').read_text().splitlines()
    model=next(x.split(':',1)[1].strip() for x in cpu if x.startswith('model name'))
    flags=next(x.split(':',1)[1].split() for x in cpu if x.startswith('flags'))
    note('Hardware',f'{model}, {env["NCPU"]} workers, affinity {sorted(os.sched_getaffinity(0))}, AVX-512BW {"avx512bw" in flags}, VBMI2 {"avx512_vbmi2" in flags}.')
    note("Control","Official 0.99-second commit 33fb788bf962900617e8c0455d366f7f0a5d7eeb, byte-checked sources. A second independently trained build measures build variance.")
    candidates=(root/'perf/native/candidates.txt').read_text().split()
    if 'avx512bw' not in flags: candidates=[n for n in candidates if not n.startswith('v512')]
    binaries=[]
    area.joinpath('bin').mkdir(exist_ok=True)
    for name in candidates:
        try:
            src=area/name
            src.mkdir(exist_ok=True)
            shutil.copytree(root/'cpp',src/'cpp',dirs_exist_ok=True)
            shutil.copy2(root/'build.sh',src/'build.sh')
            if name not in ('haswell','skylake'):
                revision='fin-slabkey-pgo' if name=='base' else name
                relative=root/'perf/native/from-099'/(name+'.patch')
                patches=[root/'perf/native/fin-slabkey-pgo.patch',relative] if relative.exists() else [root/f'perf/native/{revision}.patch']
                for path in patches:
                    with path.open('rb') as patch:
                        subprocess.run(['patch','-s','-p1'],cwd=src,stdin=patch,check=True)
            if name=='base': run('sha256sum','--check',root/'perf/native/official-099.sha256',cwd=src)
            compiler=shutil.which('g++-12') or shutil.which('g++')
            if name in ('haswell','skylake'):
                wrapper=src/'cxx'
                arch='haswell' if name=='haswell' else 'skylake-avx512'
                if name=='skylake' and 'avx512bw' not in flags: continue
                wrapper.write_text(f'#!/bin/sh\nexec {compiler} -march={arch} "$@"\n')
                wrapper.chmod(0o755)
                compiler=str(wrapper)
            if (src/'cpp/flat_parquet.hpp').exists():
                shutil.copy2(root/'perf/native/standalone-build.sh',src/'build.sh')
            mode='pgo' if name=='base' else name.rsplit('-',1)[-1]
            if mode in ('pgo','lto'):
                shutil.copy2(root/'perf/native/tuned-build.sh',src/'build.sh')
                (src/'perf').mkdir(exist_ok=True)
                shutil.copy2(root/'perf/native/train.py',src/'perf/train.py')
            settings=root/'perf/native/build-settings'/name
            if settings.is_dir():
                shutil.copy2(settings/'build.sh',src/'build.sh')
                shutil.copy2(settings/'train.py',src/'perf/train.py')
                if name.startswith('clang'):
                    compiler=shutil.which('clang++-16')
                    if not compiler: raise OSError('Clang 16 unavailable')
            run('sh','build.sh',cwd=src,extra={'CXX':compiler,'PYTHON':python,'COMPILER_TRIAL':mode,'PGO':'1' if mode=='pgo' else '0'})
            binary=area/'bin'/name
            shutil.copy2(src/'build/shopping',binary)
            unit=area/'bin'/('unit-'+name)
            localtests=src/'tests'
            shutil.copytree(root/'tests',localtests,dirs_exist_ok=True)
            run(compiler,'-std=c++20','-O2',str(localtests/'input.cpp'),'-o',unit)
            run(unit)
            run(python,root/'verify.py',binary,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
            run(python,root/'perf/native/random_rules.py',binary)
            run(python,root/'perf/native/interval_oracle.py',binary)
            binaries.append(binary)
        except (subprocess.CalledProcessError,OSError) as exc:
            if name=='base': raise
            note('Rejected '+name,repr(exc))
    if shutil.which('perf'):
        with tempfile.TemporaryDirectory(dir=area) as temp:
            try:
                probe=subprocess.run(['sudo','-n','perf','stat','-e','cycles,instructions,cache-misses,dTLB-load-misses,branch-misses',
                    'runuser','-u',pwd.getpwuid(os.getuid()).pw_name,'--',str(area/'bin/base'),str(area/'dev'),temp],text=True,capture_output=True,timeout=25,env=env)
                note('PMU',probe.stderr.strip()[-650:].replace('\n',' '))
            except (OSError,subprocess.TimeoutExpired) as error: note('PMU',str(error))
    for mode in ('read','lex'):
        floor=area/'floor'
        if not floor.exists():
            run('g++-12','-O3','-std=c++20','-pthread','-I',str(root/'cpp'),root/'perf/native/floor.cpp','-o',floor)
        times=[]
        for i in range(4):
            start=time.monotonic()
            run(floor,area/'practice',mode,env['NCPU'])
            if i: times.append(time.monotonic()-start)
        note('Floor '+mode,str([round(x,4) for x in times]))
    final_list=(root/'perf/native/final.txt').read_text().split()
    performance={}
    for scale in (['practice','final','holdout'] if final_list else ['practice']):
        if scale=='final':
            ranked=sorted((n for n in performance if n!='base'),key=performance.get)
            if '@best' in final_list: final_list+=ranked[:2]
            binaries=[b for b in binaries if b.name in final_list]
            # Retain the public generator's RNG order and shuffle. Bound peak
            # memory by materializing disjoint output files in separate passes.
            gen=area/'generator'
            with (root/'perf/native/generator-memory.patch').open('rb') as patch:
                subprocess.run(['patch','-s','-p1'],cwd=gen,stdin=patch,check=True)
            run('cargo','build','--release','--offline',cwd=gen)
            exe=gen/'target/release/generate'
            for part in range(3):
                run(exe,'--seed','1','--events','1000000','--clicks','250000','--threads','4','--output',area/'dev-check',extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
            run('diff','-qr',area/'dev',area/'dev-check')
        if scale in ('final', 'holdout'):
            seed = '47' if scale=='final' else '733'
            note('60M population '+scale, 'Public generator seed '+seed+', sixty million requested events and fifteen million requested clicks.')
            for part in range(3):
                run(exe,'--seed',seed,'--events','60000000','--clicks','15000000','--threads','4','--output',area/scale,extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
        data=area/scale
        ref=area/(scale+'-oracle')
        binary0=area/'bin/base'
        run(binary0,data,ref)
        for b in binaries:
            if b==binary0: continue
            with tempfile.TemporaryDirectory(dir=area) as d:
                run(b,data,d)
                run(python,root/'perf/native/compare_large.py',d,ref)
        import pyarrow.parquet as pq
        row_counts = {table:sum(pq.ParquetFile(file).metadata.num_rows for file in (ref/table).glob('*.parquet')) for table in ('user_daily','campaign_daily')}
        note('Exact '+scale,str(row_counts)+' compared by schema, unique daily keys, nulls and every value.')
        path=outcomes/(scale+'.jsonl')
        run(python,root/'perf/measure.py',data,*binaries,'--profile','--preload','--runs','5' if scale=='practice' else '3',
            '--warmup','1','--output-parent',area,'--log',path)
        for b in binaries:
            rows=[r for r in map(json.loads,path.read_text().splitlines()) if r['round']>=0 and r['binary']==str(b)]
            times=[round(r['wall_s'],4) for r in rows]
            performance[b.name]=statistics.median(times)
            phases={}
            for r in rows:
                for line in r['profile'].splitlines():
                    if line.endswith(' s'):
                        key,value,_=line.rsplit(' ',2)
                        phases.setdefault(key,[]).append(float(value))
            note(scale+' '+b.name,f'median {statistics.median(times):.4f} s, runs {times}, peak {max(r["peak_mib"] for r in rows):.0f} MiB, user {statistics.median(r["user_s"] for r in rows):.2f} s. '+str({k:round(statistics.median(v),3) for k,v in phases.items()}))

        best=min((b for b in binaries if b!=binary0),key=lambda b:performance[b.name])
        head=outcomes/(scale+'-head-to-head.jsonl')
        run(python,root/'perf/measure.py',data,binary0,best,'--runs','7' if scale=='practice' else '5','--warmup','1','--preload','--profile','--output-parent',area,'--log',head)
        pairs=list(map(json.loads,head.read_text().splitlines()))
        for binary in (binary0,best):
            times=[round(x['wall_s'],4) for x in pairs if x['round']>=0 and x['binary']==str(binary)]
            note('Alternating '+scale+' '+binary.name,f'median {statistics.median(times):.4f} s, runs {times}.')
        publish()
        # Request eviction only for this job's generated CSVs and record the
        # actual residency before the executable starts.
        selected=[b for b in binaries if b.name in ('base','fin-slabkey-pgo')]
        if best not in selected: selected.append(best)
        observations={b:[] for b in selected}
        for iteration in range(0 if scale=='holdout' else 3):
            start_index=iteration%len(selected)
            for b in selected[start_index:]+selected[:start_index]:
                os.sync()
                for csv in data.rglob('*.csv'):
                    with csv.open('rb') as stream: os.posix_fadvise(stream.fileno(),0,0,os.POSIX_FADV_DONTNEED)
                cold=outcomes/(scale+'-cold-'+b.name+'-'+str(iteration)+'.jsonl')
                run(python,root/'perf/measure.py',data,b,'--runs','1','--warmup','0','--residency','--profile','--output-parent',area,'--log',cold)
                rec=json.loads(cold.read_text())
                observations[b].append((round(rec['wall_s'],3),round(rec['resident_before'],3),rec['major']))
        for b,obs in observations.items():
            if obs: note('Eviction '+scale+' '+b.name,f"(seconds, starting residency, major faults) {obs}")
        if scale=='practice':
            practice_times=performance.copy()
            practice_winner=best.name
            src=area/best.name
            run('sh','build.sh',cwd=src,extra={'CXX':shutil.which('g++-12'),'PYTHON':python,'DIAGNOSTICS':'1'})
            with tempfile.TemporaryDirectory(dir=area) as temp:
                proc=subprocess.run([src/'build/shopping-profile',data,temp],env=env|{'PROFILE':'1'},capture_output=True,text=True,check=True)
                (outcomes/'profile.txt').write_text(proc.stderr)
                note('Profile '+best.name,' '.join(proc.stderr.splitlines()[-12:])[-1150:])
        if scale in ('final', 'holdout'):
            from native_images import verify_images
            if scale=='final':
                options=[b.name for b in binaries if b.name not in ('base','fin-slabkey-pgo')]
                if options:
                    practice_winner=min(options,key=lambda n: (practice_times[n]/practice_times['base'])*(performance[n]/performance['base'])**2)
                note('Image selection',practice_winner+' by the product of the 20M ratio and the squared first 60M ratio; seed 733 is held out.')
            scales = [('practice',5),('final',3)] if scale=='final' else [('holdout',3)]
            verify_images(root,area,practice_winner,run,note,env,scales)
            if scale=='final':
                # This job generated these input files and finished all seed-47 comparisons.
                shutil.rmtree(data)
        publish()
    from input_profile import collect
    collect(root,area,'base',note,run,env)
    from extra_checks import scaling, race
    scaling(root,area,practice_winner,note,run,env)
    race(root,area,practice_winner,note,run,env)
    from line_profile import collect as measure_lines
    measure_lines(root,area,note,run,env)

published=0
def publish():
    global published
    while published<len(notes):
        payload=' || '.join(notes[published:published+6]).replace('%','%25').replace('\r','%0D').replace('\n','%0A')
        print(f'::notice file=BENCHMARKS.md,line=1,title=Native results {published+1}::{payload}',flush=True)
        published+=min(6,len(notes)-published)
if __name__=='__main__':
    try: main()
    except BaseException as exc:
        note('Failure',repr(exc))
        raise
    finally: publish()
