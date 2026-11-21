"""Compare release event decoders, immutable scatter and linked-session algorithms."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile

from focused_marked_run import source, select
from run import root,area,env,python,run,note
from native_images import verify_images
from extra_checks import race
from long_location_oracle import verify as check_skew

native=root/'perf/native'
control='shipresident-pgo'
previous='portablerich-pgo'
def measure(arguments,names,data,label,count,extra=None):
    log=area/'results'/(label+'.jsonl')
    run(python,root/'perf/measure.py',data,*arguments,'--runs',str(count),'--warmup','1','--preload',
        '--output-parent',area,'--log',log,extra=extra)
    rows=[x for x in map(json.loads,log.read_text().splitlines()) if x['round']>=0]
    medians={}
    for name,binary in zip(names,arguments):
        chosen=[x for x in rows if x['binary']==str(binary)]
        values=[x['wall_s'] for x in chosen]
        assert len(values)==count
        medians[name]=statistics.median(values)
        note(label+' '+name, f'median {medians[name]:.4f} s, runs {[round(x,4) for x in values]}, '
             f'peak {max(x["peak_mib"] for x in chosen):.0f} MiB, median user CPU {statistics.median(x["user_s"] for x in chosen):.2f} s.')
    return medians

def compare_tables(binary,data,answer,extra=None):
    with tempfile.TemporaryDirectory(dir=area) as tmp:
        run(binary,data,tmp,extra=extra)
        run(python,native/'compare_large.py',tmp,answer)

def main():
    cpu=Path('/proc/cpuinfo').read_text().splitlines()
    brand=next(v.split(':')[1].strip() for v in cpu if v.startswith('model name'))
    flags=next(v.split(':')[1].split() for v in cpu if v.startswith('flags'))
    note('Hardware',f'{brand}, {env["NCPU"]} pinned workers {sorted(os.sched_getaffinity(0))}, AVX-512BW {"avx512bw" in flags}, VBMI2 {"avx512_vbmi2" in flags}.')
    names=[n for n in (native/'candidates.txt').read_text().split() if n!='fin-slabkey-pgo']
    (area/'bin').mkdir(exist_ok=True)
    def validate(name):
        home=source(name);settings=native/'build-settings'/name
        if settings.is_dir():
            for f,target in [('build.sh','build.sh'),('train.py','perf/train.py'),('Dockerfile','Dockerfile')]:
                shutil.copy2(settings/f,home/target)
        release_manifest=json.loads((native/'batch-release-files.json').read_text())
        if name in release_manifest:
            for filename,digest in release_manifest[name].items():
                assert hashlib.sha256((home/filename).read_bytes()).hexdigest()==digest,(name,filename)
        if name==previous:
            before=json.loads((native/'official-088-files.json').read_text())
            for f,digest in before.items():
                assert hashlib.sha256((home/f).read_bytes()).hexdigest()==digest,(name,f)
            note('Git source','25a940f0d0a8f4a38ab8c5c96a47bb8927b3e4e3 matched all release sources and build inputs.')
        run('sh','build.sh',cwd=home,extra={'PGO':'1','PYTHON':python,'CXX':'g++-12'})
        binary=area/'bin'/name
        shutil.copy2(home/'build/shopping',binary)
        run(python,root/'verify.py',binary,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
        unit=area/'reader-unit'
        if (home/'cpp/marked_events.hpp').exists():
            (home/'tests').mkdir(exist_ok=True)
            shutil.copy2(native/'marked_events.cpp',home/'tests/marked_events.cpp')
            run('g++-12','-std=c++20','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',home/'tests/marked_events.cpp','-o',unit)
            run(unit)
        focused=[]
        if (home/'cpp/marked_event_tile.inc').exists():focused.append(('release_event_batches.cpp',[]))
        if (home/'cpp/stream_marks.hpp').exists():focused.append(('stream_event_test.cpp',['-DCHECK_STREAM_BATCH']))
        if (home/'cpp/session_click_masks.hpp').exists():focused.append(('session_masks_test.cpp',[]))
        if (home/'cpp/vertical_fields.hpp').exists() and not (home/'cpp/stream_marks.hpp').exists():
            focused.append(('vertical_event_test.cpp',[]))
        for check,arguments in focused:
            run('g++-12','-std=c++20','-O2',*arguments,'-fsanitize=undefined','-fno-sanitize-recover=all','-I',home/'cpp',native/check,'-o',unit)
            run(unit)
        if name.startswith(('span','vecmask','rank')):
            run(python,native/'dest_oracle.py',binary)
            run(python,native/'interval_oracle.py',binary)
        print('End-to-end fixtures and focused checks passed for',name,flush=True)
    valid=[]
    for name in names:
        try:
            validate(name);valid.append(name)
        except (subprocess.CalledProcessError,AssertionError) as exc:
            if name in (control,previous,"base"):raise
            note("Rejected "+name,repr(exc))
    names=valid
    note('Correctness',f'All {len(names)} compilers/readers passed 36 supplied fixtures, both published 1M tables, and applicable scalar oracles.')
    select('Fixtures and source identity',('Hardware ','Correctness ','Git source '))
    gen=area/'generator';generator=gen/'target/release/generate';fullnames=names[:]
    times={};winner=None
    for scale,seed in [('practice',23),('final',47),('holdout',733)]:
        if scale=='final':
            with (native/'generator-memory.patch').open('rb') as patch:subprocess.run(['patch','-s','-p1'],cwd=gen,stdin=patch,check=True)
            run('cargo','build','--release','--offline',cwd=gen)
            for part in range(3):run(generator,'--seed','1','--events','1000000','--clicks','250000','--threads','4','--output',area/'dev-check',extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
            run('diff','-qr',area/'dev',area/'dev-check')
            # Carry the practice leaders in each family and a forced minimum-ISA
            # reader from each architecture. Seed 733 is kept out of selection.
            sizes=times['practice']
            finals={control,previous,'base'}
            finals.add('vertshape2-pgo')
            for group in (('batch',),('span','vecmask'),('rank',)):
                options=[n for n in fullnames if n.startswith(group)]
                finals.update(sorted(options,key=sizes.get)[:2])
            finals.update(n for n in fullnames if n.startswith('ship8'))
            names=[n for n in fullnames if n in finals]
        if scale!='practice':
            for part in range(3):run(generator,'--seed',str(seed),'--events','60000000','--clicks','15000000','--threads','4','--output',area/scale,extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
        data=area/scale;answer=area/(scale+'-oracle')
        run(area/'bin'/control,data,answer)
        for name in names:
            if name!=control:compare_tables(area/'bin'/name,data,answer)
        note('Exact '+scale,f'Both full Parquet tables matched the official 0.87 s implementation in {len(names)-1} comparisons.')
        runs=5 if scale=='practice' else 3
        times[scale]=measure([area/'bin'/n for n in names],names,data,scale,runs)
        select('Process comparisons '+scale,(scale+' ','Exact '+scale))
        if scale=='practice':continue
        if scale=='final':
            candidates=[n for n in names if n.startswith('ship8')]
            def score(n):return times['practice'][n]/times['practice'][control]*(times['final'][n]/times['final'][control])**2
            winner=min(candidates,key=score)
            overall=min([n for n in names if n not in ('base',previous,control)],key=score)
            note('Image selection',winner+' fastest clean release candidate, '+overall+' fastest overall by 20M ratio times squared 60M ratio. Seed 733 held out.')
        verify_images(root,area,winner,run,note,env,[('practice',5),('final',3)] if scale=='final' else [('holdout',3)],control)
        select('Docker '+scale,('Image selection ','Release image pairs ','Docker practice ','Docker final ','Docker holdout ','Docker fixture binary ','Image binary '))
        if scale=='final':shutil.rmtree(data)
        else:
            # Only four CPUs are available. Eight workers here test final memory
            # and parallel correctness, separately from every timed rotation.
            with tempfile.TemporaryDirectory(dir=area) as tmp:
                meter=area/'eight-worker-rss.txt'
                run('/usr/bin/time','-o',meter,'-f','%M',area/'bin'/winner,data,tmp,extra={'NCPU':'8'})
                run(python,native/'compare_large.py',tmp,answer)
                note('Eight worker memory',winner+' exact 60M outputs at '+meter.read_text().strip()+' KiB peak RSS on four pinned CPUs.')
    # Capture CPU-stage times outside the paired measurements.
    profile_data=area/'practice'
    for name in [control,previous,winner]:
        with tempfile.TemporaryDirectory(dir=area) as tmp:
            p=subprocess.run([area/'bin'/name,profile_data,tmp],env={**env,'PROFILE':'1'},text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
            note('Release phases '+name,p.stdout[-4500:].replace('\n',' / '))
    select('Release profiles',('Release phases ',))
    check=area/'generate-skewed-alias';run('g++-12','-std=c++20','-O2',native/'long_location_input.cpp','-o',check)
    check_skew(area,check,[area/'bin'/winner],note)
    race(root,area,winner,note,run,env)
    select('Race, skew and memory',('ThreadSanitizer ','Eight worker memory ','Skewed revision locations '))

if __name__=='__main__':
    try:main()
    except BaseException as exc:
        note('Failure',repr(exc));select('Portfolio failure',('Failure ',));raise
