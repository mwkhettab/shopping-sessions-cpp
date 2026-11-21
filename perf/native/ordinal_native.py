"""Check exact release sources and measure the ordinal-based engine on native Intel."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from run import root,area,run,env,note,python
from focused_marked_run import source,select
from reader_portfolio import measure,compare_tables
from native_images import verify_images
from extra_checks import race
from long_location_oracle import verify as skew
from complete_pipeline_portfolio import generate

native=root/'perf/native'
control='shipresident-pgo'
recent='release2rich-pgo'
def report(title,message):
    note(title,message)
    print('::notice file=BENCHMARKS.md,line=1,title='+title+'::'+message.replace('%','%25').replace('\n','%0A'),flush=True)
def main():
    bin=area/'bin';bin.mkdir(exist_ok=True)
    model=next(line.split(':')[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name'))
    report('Native processor',model+', pinned CPUs '+str(sorted(os.sched_getaffinity(0))))
    manifest=json.loads((native/'ordinal-files.json').read_text())
    candidates=[n for n in manifest if n.startswith('engine-')]
    participants=['base',control,recent,*candidates]
    for n in participants:
        src=source(n)
        conf=native/'build-settings'/n
        if conf.is_dir():
            for f,where in [('build.sh','build.sh'),('Dockerfile','Dockerfile'),('train.py','perf/train.py')]:shutil.copy2(conf/f,src/where)
        for file,digest in manifest.get(n,{}).items():
            assert hashlib.sha256((src/file).read_bytes()).hexdigest()==digest,(n,file)
        run('sh','build.sh',cwd=src,extra={'PGO':'1','CXX':'g++-12','PYTHON':python})
        out=bin/n;shutil.copy2(src/'build/shopping',out)
        run(python,root/'verify.py',out,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
        if n not in candidates:continue
        if (src/'cpp/ordinal_lanes.hpp').is_file():
            (src/'tests').mkdir(exist_ok=True);shutil.copy2(native/'ordinal_ids.cpp',src/'tests/ordinal_ids.cpp')
            unit=area/'id-unit';run('g++-12','-O2','-std=c++20','-fsanitize=undefined','-fno-sanitize-recover=all',src/'tests/ordinal_ids.cpp','-o',unit)
            run(unit)
        run(python,native/'ordinal_rules.py',out,bin/control)
        for suite in ('dest_oracle.py','ordered_ids_oracle.py','interval_oracle.py','page_oracle.py','random_rules.py'):
            run(python,native/suite,out)
    report('Engine correctness',f'All {len(candidates)} engines passed the 36 fixtures, both published tables and independently salted reordered IDs with the fast path active.')
    times={};winner=None
    for scale,seed in [('practice',23),('final',47),('holdout',733)]:
        generate(scale,seed);data=area/scale;answer=area/(scale+'-oracle');run(bin/control,data,answer)
        names=[control,recent,*candidates]
        for n in names[1:]:compare_tables(bin/n,data,answer)
        reps=5 if scale=='practice' else 3
        times[scale]=measure([bin/n for n in names],names,data,scale+'-automatic',reps)
        report('Automatic '+scale,' / '.join(f'{n} {times[scale][n]:.4f} s' for n in names))
        if scale=='practice':
            for n in candidates:
                with tempfile.TemporaryDirectory(dir=area) as out:
                    p=subprocess.run([bin/n,data,out],capture_output=True,text=True,env=env|{'PROFILE':'1','SHOP_ORDINAL_AUDIT':'1'},check=True)
                    assert 'ordinal engine ' in p.stderr
                    note('Phases '+n,' / '.join(p.stderr.splitlines()))
            select('Practice phase timings',('Phases ',))
        for cap,option in [('avx2',{'SHOP_TRAIN_BASE_READER':'1'}),('without-ifma',{'SHOP_ORDINAL_BASE_MULTIPLY':'1'})]:
            capped=[recent,*[n for n in candidates if n!='engine-portable-pgo']]
            for n in capped[1:]:compare_tables(bin/n,data,answer,option)
            vals=measure([bin/n for n in capped],capped,data,scale+'-'+cap,reps,option)
            note('Capped '+scale+' '+cap,' / '.join(f'{n} {vals[n]:.4f} s' for n in capped))
        if scale=='practice':continue
        if scale=='final':
            winner=min(candidates,key=lambda n:times['practice'][n]*times['final'][n]**2)
            report('Image selection',winner+' selected on seed 23 and seed 47 before generating seed 733.')
        verify_images(root,area,winner,run,note,env,[('practice',5),('final',3)] if scale=='final' else [('holdout',3)],control)
        select('Release images '+scale,('Image selection ','Release image pairs ','Image binary ','Docker fixture binary ','Docker practice ','Docker final ','Docker holdout '))
        if scale=='final':shutil.rmtree(data)
        else:
            with tempfile.TemporaryDirectory(dir=area) as out:
                path=area/'peak-rss';run('/usr/bin/time','-o',path,'-f','%M',bin/winner,data,out,extra={'NCPU':'8','SHOP_ORDINAL_AUDIT':'1'})
                run(python,native/'compare_large.py',out,answer)
                note('Eight worker RSS',winner+' '+path.read_text().strip()+' KiB. Both complete output tables matched.')
    gen=area/'skew-generator';run('g++-12','-O2','-std=c++20',native/'long_location_input.cpp','-o',gen);skew(area,gen,[bin/winner],note)
    race(root,area,winner,note,run,env)
    select('Final correctness',('Eight worker RSS ','ThreadSanitizer ','Skewed revision locations '))
if __name__=='__main__':
    try:main()
    except BaseException as e:report('Failure',repr(e));raise
