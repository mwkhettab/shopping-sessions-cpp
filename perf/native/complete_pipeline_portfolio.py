"""Compare complete person pipelines and their individual components on native Linux."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import tempfile

from run import root,area,run,env,note,python
from focused_marked_run import source,select
from reader_portfolio import measure,compare_tables
from native_images import verify_images
from extra_checks import race
from long_location_oracle import verify as skew

native=root/'perf/native'
control='shipresident-pgo'
trained='release3rich-pgo'
older={'SHOP_TRAIN_BASE_READER':'1'}
whole={'denseendtoend-pgo','denseroutepages-pgo','fullperson-pgo','fullpersonpages-pgo'}

def build(label,files):
    tree=source(label)
    config=native/'build-settings'/label
    if config.exists():
        for file,dest in [('build.sh','build.sh'),('Dockerfile','Dockerfile'),('train.py','perf/train.py')]:
            shutil.copy2(config/file,tree/dest)
    for file,sha in files.items():
        assert hashlib.sha256((tree/file).read_bytes()).hexdigest()==sha,(label,file)
    run('sh','build.sh',cwd=tree,extra={'PGO':'1','CXX':'g++-12','PYTHON':python})
    binary=area/'bin'/label
    shutil.copy2(tree/'build/shopping',binary)
    run(python,root/'verify.py',binary,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
    for header,suite in [('counted_alias_slots.hpp','counted_alias_test.cpp'),('cache_revision_table.hpp','cache_revision_test.cpp')]:
        if (tree/'cpp'/header).exists():
            check=area/'exact-keys-test'
            run('g++-12','-O2','-std=c++20','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all','-I',tree/'cpp',native/suite,'-o',check)
            run(check)
    if label in whole or label in ('batchdailyfast-pgo','densepersonspans-pgo'):
        for suite in ('dest_oracle.py','interval_oracle.py','page_oracle.py','random_rules.py'):
            run(python,native/suite,binary)

def generate(scale,seed):
    gen=area/'generator'
    executable=gen/'target/release/generate'
    if scale=='final':
        with (native/'generator-memory.patch').open('rb') as patch:
            subprocess.run(['patch','-s','-p1'],cwd=gen,stdin=patch,check=True)
        run('cargo','build','--release','--offline',cwd=gen)
        for part in range(3):
            run(executable,'--seed','1','--events','1000000','--clicks','250000','--threads','4','--output',area/'dev-check',extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
        run('diff','-qr',area/'dev',area/'dev-check')
    if scale!='practice':
        for part in range(3):
            run(executable,'--seed',str(seed),'--events','60000000','--clicks','15000000','--threads','4','--output',area/scale,extra={'PART_CLASSES':'3','PART_CLASS':str(part)})

def main():
    flags=Path('/proc/cpuinfo').read_text().splitlines()
    model=next(x.split(':',1)[1].strip() for x in flags if x.startswith('model name'))
    bits=next(x.split(':',1)[1].split() for x in flags if x.startswith('flags'))
    note('Hardware',f'{model}, pinned CPUs {sorted(os.sched_getaffinity(0))}, AVX512BW {"avx512bw" in bits}, VBMI2 {"avx512_vbmi2" in bits}.')
    manifest=json.loads((native/'complete-release-files.json').read_text())
    bin=area/'bin';bin.mkdir(exist_ok=True)
    names=[]
    for n in ['base',control,'portablerich-pgo',*manifest]:
        try:
            build(n,manifest.get(n,{}))
            names.append(n)
        except (subprocess.CalledProcessError,AssertionError) as exc:
            if n in ('base',control,trained):raise
            note('Rejected '+n,repr(exc))
    note('Correctness',f'{len(names)} builds passed all 36 fixtures and both published tables. Complete designs also passed independent graph, clock, page and attribution checks.')
    select('Native C++ fixture results',('Hardware ','Rejected ','Correctness '))
    scores={}
    finalists=[]
    winner=None
    for scale,seed in [('practice',23),('final',47),('holdout',733)]:
        generate(scale,seed)
        data=area/scale;answer=area/(scale+'-oracle')
        run(bin/control,data,answer)
        contestants=names if scale=='practice' else finalists
        repeats=5 if scale=='practice' else 3
        for n in contestants:
            if n!=control:compare_tables(bin/n,data,answer)
        scores[scale]=measure([bin/n for n in contestants],contestants,data,scale+'-auto',repeats)
        for offset in range(0,len(contestants),5):
            group=contestants[offset:offset+5]
            note('Automatic '+scale+' '+str(offset),' / '.join(f'{n} {scores[scale][n]:.4f} s' for n in group))
        select('Automatic '+scale,('Automatic '+scale+' ',))
        if scale=='practice':
            available=scores[scale]
            chosen={control,trained,'release3lean-pgo'}
            chosen.update(sorted(manifest.keys() & available.keys(),key=available.get)[:3])
            for prefixes in [('countslot','cacheroute'),('cacheevent',),('userpage','daypage','twoclick','batchdaily'),('fullperson',),('denseendtoend','denseroutepages','denseperson')]:
                options=[n for n in names if n.startswith(prefixes)]
                if options:chosen.add(min(options,key=available.get))
            finalists=[n for n in names if n in chosen]
        older_names=finalists
        for n in older_names:
            if n!=control:compare_tables(bin/n,data,answer,older)
        scores[scale+'-avx2']=measure([bin/n for n in older_names],older_names,data,scale+'-avx2',repeats,older)
        for offset in range(0,len(older_names),5):
            group=older_names[offset:offset+5]
            note('AVX2 '+scale+' '+str(offset),' / '.join(f'{n} {scores[scale+"-avx2"][n]:.4f} s' for n in group))
        select('AVX2 '+scale,('AVX2 '+scale+' ',))
        if scale=='practice':continue
        if scale=='final':
            def rank(n):
                ratios=[(scores['practice'+s][n]/scores['practice'+s][trained]*(scores['final'+s][n]/scores['final'+s][trained])**2)**(1/3) for s in ('','-avx2')]
                return max(ratios),statistics.mean(ratios)
            winner=min((n for n in finalists if n in manifest),key=rank)
            note('Selected candidate',winner+' selected before seed 733, weighting 60M twice.')
            diagnostics={control,trained,winner}
            new=[n for n in finalists if n in whole]
            if new:diagnostics.add(min(new,key=scores[scale].get))
            for n in sorted(diagnostics):
                with tempfile.TemporaryDirectory(dir=area) as tmp:
                    p=subprocess.run([bin/n,data,tmp],capture_output=True,text=True,check=True,env=env|{'PROFILE':'1'})
                    note('Phases '+n,' / '.join(p.stderr.strip().splitlines())[:1200])
            select('60M phase timings',('Selected candidate ','Phases '))
        verify_images(root,area,winner,run,note,env,[('practice',5),('final',3)] if scale=='final' else [('holdout',3)],control)
        select('Native release Docker '+scale,('Selected candidate ','Release image pairs ','Image binary ','Docker fixture binary ','Docker '+scale+' ','Docker practice '))
        if scale=='final':
            shutil.rmtree(data)
        else:
            memory_targets={winner}
            complete=[f for f in finalists if f in whole]
            if complete:memory_targets.add(min(complete,key=scores[scale].get))
            for n in sorted(memory_targets):
                with tempfile.TemporaryDirectory(dir=area) as tmp:
                    rss=area/'eight-worker-rss';run('/usr/bin/time','-o',rss,'-f','%M',bin/n,data,tmp,extra={'NCPU':'8'})
                    run(python,native/'compare_large.py',tmp,answer)
                    note('Eight workers '+n,rss.read_text().strip()+' KiB peak RSS and both tables matched.')
    target=area/'skew-input';run('g++-12','-O2','-std=c++20',native/'long_location_input.cpp','-o',target)
    skew(area,target,[bin/winner],note);race(root,area,winner,note,run,env)
    select('Final reliability',('Eight workers ','Skewed revision locations ','ThreadSanitizer '))

if __name__=='__main__':
    try:main()
    except BaseException as e:
        note('Failure',repr(e));select('Native whole-pipeline failure',('Failure ',));raise
