"""Compare partition storage, validated clocks and partition-local session completion."""
import hashlib,json,os,shutil,statistics,subprocess,tempfile
from pathlib import Path
from run import root,area,run,env,note,python
from focused_marked_run import source,select
from reader_portfolio import measure,compare_tables
from native_images import verify_images
from extra_checks import race
from native_lifetime import measure_lifetime
from line_profile import collect
from long_location_oracle import verify as skew
from complete_pipeline_portfolio import generate
native=root/'perf/native'
old='shipresident-pgo';recent='release2rich-pgo';first='engine-lanes-pgo'
def report(k,v):
    note(k,v);print('::notice file=BENCHMARKS.md,line=1,title='+k+'::'+v.replace('%','%25').replace('\n','%0A'),flush=True)
def main():
    bin=area/'bin';bin.mkdir(exist_ok=True)
    model=next(s.split(':')[1].strip() for s in Path('/proc/cpuinfo').read_text().splitlines() if s.startswith('model name'))
    report('Native hardware',model+' '+str(sorted(os.sched_getaffinity(0))))
    manifest=json.loads((native/'ordinal-windows-files.json').read_text());names=list(manifest)
    for n in ['base',old,*names]:
        src=source(n);conf=native/'build-settings'/n
        if conf.is_dir():
            for a,b in [('build.sh','build.sh'),('Dockerfile','Dockerfile'),('train.py','perf/train.py')]:shutil.copy2(conf/a,src/b)
        for f,digest in manifest.get(n,{}).items():assert hashlib.sha256((src/f).read_bytes()).hexdigest()==digest,(n,f)
        if n=='base':continue
        run('sh','build.sh',cwd=src,extra={'PGO':'1','CXX':'g++-12','PYTHON':python});out=bin/n;shutil.copy2(src/'build/shopping',out)
        run(python,root/'verify.py',out,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
        if n in (old,recent):continue
        run(python,native/'ordinal_rules.py',out,bin/old)
        folder=src/'tests';folder.mkdir(exist_ok=True);shutil.copy2(native/'ordinal_ids.cpp',folder/'ordinal_ids.cpp')
        unit=area/'id-unit';run('g++-12','-O2','-std=c++20','-fsanitize=undefined','-fno-sanitize-recover=all',folder/'ordinal_ids.cpp','-o',unit);run(unit)
        if (src/'cpp/ordinal_clocks.hpp').exists() or (src/'cpp/ordinal_row_windows.hpp').exists():
            shutil.copy2(native/'ordinal_packets.cpp',folder/'ordinal_packets.cpp')
            run('g++-12','-O2','-std=c++20','-fsanitize=undefined','-fno-sanitize-recover=all',folder/'ordinal_packets.cpp','-o',unit);run(unit)
    times={};pool=[old,*names];winner=None
    for scale,seed in [('practice',23),('final',47),('holdout',733)]:
        generate(scale,seed);data=area/scale;answer=area/(scale+'-oracle');run(bin/old,data,answer)
        for n in pool[1:]:compare_tables(bin/n,data,answer)
        reps=5 if scale=='practice' else 3
        values=measure([bin/n for n in pool],pool,data,'storage-'+scale,reps);times[scale]=values
        report('Whole process '+scale,' / '.join(n+' '+f'{x:.4f}' for n,x in values.items()))
        if scale=='practice':
            for n in pool[2:]:
                with tempfile.TemporaryDirectory(dir=area) as out:
                    p=subprocess.run([bin/n,data,out],capture_output=True,text=True,env=env|{'SHOP_ORDINAL_AUDIT':'1','PROFILE':'1'},check=True)
                    assert 'ordinal engine ' in p.stderr
                    note('Phases '+n,' / '.join(p.stderr.splitlines()))
            select('Practice stage timings',('Phases ',))
            fast=sorted(names[1:],key=values.get)[:4]
            collect(root,area,note,run,env,fast[0])
            select('Sampled native instructions',('Sampled source lines ','Instruction counters ','Line profile unavailable '))
            pool=list(dict.fromkeys([old,recent,first,*fast]));continue
        if scale=='final':
            winner=min(pool[2:],key=lambda n:times['practice'][n]*values[n]**2)
            note('Selected storage',winner+' chosen before generating the second 60M seed.')
            report('Selected storage',winner)
            for suite in ('dest_oracle.py','ordered_ids_oracle.py','interval_oracle.py','page_oracle.py','random_rules.py'):
                run(python,native/suite,bin/winner)
            pool=list(dict.fromkeys([old,recent,first,winner]))
        for option,flag in [('avx2','SHOP_TRAIN_BASE_READER'),('dq','SHOP_ORDINAL_BASE_MULTIPLY')]:
            selected=[recent,winner];compare_tables(bin/winner,data,answer,{flag:'1'});v=measure([bin/n for n in selected],selected,data,'storage-'+scale+'-'+option,3,{flag:'1'})
            note('Capped '+scale+' '+option,str(v))
        subset=[('practice',5),('final',3)] if scale=='final' else [('holdout',3)]
        verify_images(root,area,winner,run,note,env,subset,old)
        select('Docker '+scale,('Image binary ','Docker fixture binary ','Docker practice ','Docker final ','Docker holdout ','Release image pairs '))
        measure_lifetime(area,'practice' if scale=='final' else 'holdout',env,note,native/'compare_large.py')
        select('Container command lifetime '+scale,('Container lifetime ',))
        if scale=='final':shutil.rmtree(data)
        else:
            with tempfile.TemporaryDirectory(dir=area) as out:
                path=area/'peak-rss';run('/usr/bin/time','-o',path,'-f','%M',bin/winner,data,out,extra={'NCPU':'8','SHOP_ORDINAL_AUDIT':'1'});run(python,native/'compare_large.py',out,answer)
                report('Eight worker memory',winner+' '+path.read_text().strip()+' KiB peak; both 60M tables matched.')
    gen=area/'skew-generator';run('g++-12','-O2','-std=c++20',native/'long_location_input.cpp','-o',gen);skew(area,gen,[bin/winner],note);race(root,area,winner,note,run,env)
    select('Rules and workers',('Eight worker memory ','ThreadSanitizer ','Skewed revision locations '))
if __name__=='__main__':
    try:main()
    except BaseException as e:report('Storage failure',repr(e));raise
