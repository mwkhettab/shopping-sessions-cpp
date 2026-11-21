"""Reproduce the exact marked-reader release and compare it with the scored Git source."""
import hashlib, json, os, shutil, statistics, subprocess, sys, tempfile
from pathlib import Path
from run import root, area, outcomes, python, env, run, note, notes
from native_images import verify_images
from extra_checks import race
from long_location_oracle import verify as check_skew

native=root/'perf/native'
candidate='shiprichwide-pgo'
control='shipresident-pgo'
def source(name):
    src=area/name
    shutil.copytree(root/'cpp',src/'cpp',dirs_exist_ok=True)
    patches=[native/'fin-slabkey-pgo.patch']
    if name not in ('base','fin-slabkey-pgo'):
        from_resident=native/'from-resident'/(name+'.patch')
        patches+=([native/'from-099/shipresident-pgo.patch',from_resident] if from_resident.exists()
                  else [native/'from-099'/(name+'.patch')])
    for patch in patches:
        if patch.stat().st_size:
            with patch.open('rb') as stream:subprocess.run(['patch','-s','-p1'],cwd=src,stdin=stream,check=True)
    for f in ('build.sh','Dockerfile','.dockerignore'):
        shutil.copy2(native/('image-pgo-'+f.lstrip('.')),src/f)
    (src/'perf').mkdir(exist_ok=True)
    shutil.copy2(native/'train.py',src/'perf/train.py')
    if name==candidate:
        want=json.loads((native/'marked-release-fingerprint.json').read_text())
        for fp,digest in want['files'].items():
            if hashlib.sha256((src/fp).read_bytes()).hexdigest()!=digest:raise ValueError(fp)
        note('Git source',want['commit']+' matched all release source and build fingerprints')
    if name==control:run('sha256sum','--check',native/'resident-control.sha256',cwd=src)
    if name=='base':run('sha256sum','--check',native/'official-099.sha256',cwd=src)
    return src

def select(title, keys):
    wanted=[x for x in notes if x.startswith(keys)]
    if wanted:
        message=' || '.join(wanted)
        print(f'::notice file=BENCHMARKS.md,line=1,title={title}::'+message.replace('%','%25').replace('\n','%0A')[:5500],flush=True)
    (outcomes/'all-notes.json').write_text(json.dumps(notes))

def measure():
    names=[n for n in (native/'candidates.txt').read_text().split() if n!='fin-slabkey-pgo']
    (area/'bin').mkdir(exist_ok=True)
    cpu=Path('/proc/cpuinfo').read_text().splitlines()
    model=next(v.split(':')[1].strip() for v in cpu if v.startswith('model name'))
    flags=next(v.split(':')[1].split() for v in cpu if v.startswith('flags'))
    note('Hardware',f'{model}, {env["NCPU"]} pinned logical CPUs {sorted(os.sched_getaffinity(0))}, AVX-512BW {"avx512bw" in flags}, VBMI2 {"avx512_vbmi2" in flags}.')
    dirs={}
    for name in names:
        home=dirs[name]=source(name)
        run('sh','build.sh',cwd=home,extra={'PGO':'1','PYTHON':python,'CXX':'g++-12'})
        binary=area/'bin'/name
        shutil.copy2(home/'build/shopping',binary)
        run(python,root/'verify.py',binary,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
        if (home/'cpp/marked_events.hpp').exists():
            (home/'tests').mkdir(exist_ok=True)
            shutil.copy2(native/'marked_events.cpp',home/'tests/marked_events.cpp')
            unit=area/'marked-test'
            run('g++-12','-std=c++20','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',home/'tests/marked_events.cpp','-o',unit)
            run(unit)
        if name==candidate:
            for suite in ('random_rules.py','interval_oracle.py','page_oracle.py','prefix_oracle.py','ordered_ids_oracle.py'):
                run(python,native/suite,binary)
    note('Correctness',f'All {len(names)} native builds passed all 36 fixtures and both published 1M tables. Five independent suites and both direct and declined marked-row callbacks passed for the clean candidate.')
    select('Source identity and fixture results',('Hardware ','Git source ','Correctness '))
    gen=area/'generator'
    generator=gen/'target/release/generate'
    for scale,seed in [('practice',23),('final',47),('holdout',733)]:
        if scale=='final':
            with (native/'generator-memory.patch').open('rb') as patch:subprocess.run(['patch','-s','-p1'],cwd=gen,stdin=patch,check=True)
            run('cargo','build','--release','--offline',cwd=gen)
            for part in range(3):
                run(generator,'--seed','1','--events','1000000','--clicks','250000','--threads','4','--output',area/'dev-check',extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
            run('diff','-qr',area/'dev',area/'dev-check')
        if scale!='practice':
            for part in range(3):
                run(generator,'--seed',str(seed),'--events','60000000','--clicks','15000000','--threads','4','--output',area/scale,extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
        data=area/scale
        answer=area/(scale+'-oracle')
        run(area/'bin'/control,data,answer)
        compared=[n for n in names if n!=control]
        for name in compared:
            with tempfile.TemporaryDirectory(dir=area) as out:
                run(area/'bin'/name,data,out)
                run(python,native/'compare_large.py',out,answer)
        note('Exact '+scale,f'All {len(compared)} other binaries matched the scored source in every field and unique daily key.')
        binaries=[area/'bin'/n for n in names]
        log=outcomes/(scale+'.jsonl')
        run(python,root/'perf/measure.py',data,*binaries,'--runs','5' if scale=='practice' else '3','--warmup','1','--preload','--output-parent',area,'--log',log)
        rotations=[json.loads(x) for x in log.read_text().splitlines()]
        for n in names:
            times=[x['wall_s'] for x in rotations if x['round']>=0 and Path(x['binary']).name==n]
            note(scale+' '+n,f'median {statistics.median(times):.4f} s, runs {[round(v,4) for v in times]}')
        verify_images(root,area,candidate,run,note,env,[(scale,5 if scale=='practice' else 3)],control)
        select('Intel release '+scale,('Exact '+scale,scale+' ','Release image pairs '+scale,'Docker '+scale,'Image binary '+candidate))
        if scale=='final':shutil.rmtree(data)
    gen_skew=area/'generate-skewed-alias'
    run('g++-12','-std=c++20','-O2',native/'long_location_input.cpp','-o',gen_skew)
    check_skew(area,gen_skew,[area/'bin'/control,area/'bin'/candidate],note)
    race(root,area,candidate,note,run,env)
    select('Eight-worker sanitizer and skew results',('Skewed revision locations','ThreadSanitizer '))

if __name__=='__main__':
    try:measure()
    except BaseException as error:
        note('Failure',repr(error))
        select('Failure in clean release qualification',('Failure',))
        raise
