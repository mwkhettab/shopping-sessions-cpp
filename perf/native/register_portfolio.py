"""Compare register layouts and lossless compact clicks against the trained reader."""
import hashlib,json,os,shutil,statistics,subprocess,tempfile
from run import root,area,run,env,note,python
from focused_marked_run import source,select
from reader_portfolio import measure,compare_tables
from native_images import verify_images
from extra_checks import race
from long_location_oracle import verify as skew
native=root/'perf/native';control='shipresident-pgo';trained='release3rich-pgo'
caps={'auto':{},'avx2':{'SHOP_TRAIN_BASE_READER':'1'}}
def main():
    flags=open('/proc/cpuinfo').read().splitlines()
    model=next(x.split(':',1)[1].strip() for x in flags if x.startswith('model name'))
    bits=next(x.split(':',1)[1].split() for x in flags if x.startswith('flags'))
    note('Hardware',f'{model}, pinned CPUs {sorted(os.sched_getaffinity(0))}. AVX512BW {"avx512bw" in bits}, VBMI2 {"avx512_vbmi2" in bits}.')
    manifest=json.loads((native/'register-release-files.json').read_text())
    bin=area/'bin';bin.mkdir(exist_ok=True);names=['base',control,'portablerich-pgo',*manifest]
    checked=[]
    for n in names:
        try:
            src=source(n)
            config=native/'build-settings'/n
            if config.exists():
                for first,last in [('build.sh','build.sh'),('Dockerfile','Dockerfile'),('train.py','perf/train.py')]:shutil.copy2(config/first,src/last)
            for f,h in manifest.get(n,{}).items():
                assert hashlib.sha256((src/f).read_bytes()).hexdigest()==h,(n,f)
            run('sh','build.sh',cwd=src,extra={'PGO':'1','CXX':'g++-12','PYTHON':python})
            out=bin/n;shutil.copy2(src/'build/shopping',out)
            run(python,root/'verify.py',out,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
            tests=[]
            if n.startswith(('perm8','pair8')):tests.append('release_event_batches.cpp')
            if n.startswith('trainedclick'):
                tests.append('source_click_test.cpp')
                run(python,native/'source_click_oracle.py',out)
            if n.startswith('trainedepoch'):tests.append('epoch_prefix_test.cpp')
            for test in tests:
                exe=area/'edge-case'
                run('g++-12','-O2','-std=c++20','-fsanitize=undefined','-fno-sanitize-recover=all','-I',src/'cpp',native/test,'-o',exe)
                run(exe)
            checked.append(n)
        except (subprocess.CalledProcessError,AssertionError) as e:
            if n in ('base',control,trained):raise
            note('Rejected '+n,repr(e))
    names=checked
    note('Correctness',f'{len(names)} builds passed 36 fixtures, both published reference tables and applicable byte-level scalar checks.')
    select('Correctness and platform',('Hardware ','Correctness ','Rejected '))
    finalists=None;scores={};winner=None
    generator=area/'generator/target/release/generate'
    for scale,seed in [('practice',23),('final',47),('holdout',733)]:
        data=area/scale;answer=area/(scale+'-oracle')
        if scale=='final':
            with (native/'generator-memory.patch').open('rb') as pin:subprocess.run(['patch','-s','-p1'],cwd=area/'generator',stdin=pin,check=True)
            run('cargo','build','--release','--offline',cwd=area/'generator')
            for p in range(3):run(generator,'--seed','1','--events','1000000','--clicks','250000','--threads','4','--output',area/'dev-check',extra={'PART_CLASSES':'3','PART_CLASS':str(p)})
            run('diff','-qr',area/'dev',area/'dev-check')
        if scale!='practice':
            for p in range(3):run(generator,'--seed',str(seed),'--events','60000000','--clicks','15000000','--threads','4','--output',data,extra={'PART_CLASSES':'3','PART_CLASS':str(p)})
        run(bin/control,data,answer)
        for cap,extra in caps.items():
            contestants=names if finalists is None else finalists
            if cap!='auto':contestants=[n for n in contestants if n not in ('base','portablerich-pgo')]
            for n in contestants:
                if n!=control:compare_tables(bin/n,data,answer,extra)
            medians=measure([bin/n for n in contestants],contestants,data,scale+'-'+cap,5 if scale=='practice' else 3,extra)
            scores[scale,cap]=medians
            note('Complete '+scale+' '+cap,' / '.join(f'{n} {medians[n]:.4f}' for n in contestants))
            select('Paired processes '+scale+' '+cap,('Complete '+scale+' '+cap,))
        if scale=='practice':
            chosen={control,trained}
            for cap in caps:
                valid=scores[scale,cap]
                chosen.update(sorted((n for n in valid if n in manifest),key=valid.get)[:4])
            # Keep one member of each independent hypothesis into the final-scale test.
            for prefix in ('trainedclick','trainedepoch','perm8','pair8'):
                group=[n for n in names if n.startswith(prefix)]
                if group:chosen.add(min(group,key=scores[scale,'auto'].get))
            finalists=[n for n in names if n in chosen]
            continue
        if scale=='final':
            def rank(n):
                ratios=[(scores['practice',c][n]/scores['practice',c][trained]*(scores['final',c][n]/scores['final',c][trained])**2)**(1/3) for c in caps]
                return max(ratios),statistics.mean(ratios)
            winner=min((n for n in finalists if n in manifest),key=rank)
            note('Selection',winner+', weighted twice at 60M and selected before seed 733, relative to the three-path reader.')
        verify_images(root,area,winner,run,note,env,[('practice',5),('final',3)] if scale=='final' else [('holdout',3)],control)
        select('Actual Docker '+scale,('Selection ','Release image pairs ','Image binary ','Docker fixture binary ','Docker '+scale+' '))
        if scale=='final':shutil.rmtree(data)
        else:
            with tempfile.TemporaryDirectory(dir=area) as tmp:
                rss=area/'eight-worker-rss';run('/usr/bin/time','-o',rss,'-f','%M',bin/winner,data,tmp,extra={'NCPU':'8'})
                run(python,native/'compare_large.py',tmp,answer);note('Eight workers',winner+', '+rss.read_text().strip()+' KiB peak RSS with exact whole tables.')
    target=area/'skew-input';run('g++-12','-O2','-std=c++20',native/'long_location_input.cpp','-o',target)
    skew(area,target,[bin/winner],note);race(root,area,winner,note,run,env)
    select('Final reliability',('Eight workers ','Skewed revision locations ','ThreadSanitizer '))
if __name__=='__main__':
    try:main()
    except BaseException as e:
        note('Failure',repr(e));select('Register-layout run failure',('Failure ',));raise
