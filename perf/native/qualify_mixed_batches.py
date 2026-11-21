"""Release qualification across native, AVX-512F/BW, and AVX2 CPU paths."""
import hashlib, json, os, shutil, statistics, subprocess, tempfile
from pathlib import Path
from run import root,area,env,python,run,note
from reader_portfolio import measure,compare_tables
from focused_marked_run import source,select
from extra_checks import race
from long_location_oracle import verify as skew
from native_images import verify_images
native=root/'perf/native'
control='shipresident-pgo';old='portablerich-pgo';prototype='shapemix-pgo'
caps={'auto':{},'bw':{'SHOP_TRAIN_BW_READER':'1'},'avx2':{'SHOP_TRAIN_BASE_READER':'1'}}
def main():
    cp=Path('/proc/cpuinfo').read_text().splitlines()
    model=next(x.split(':',1)[1].strip() for x in cp if x.startswith('model name'))
    flags=next(x.split(':',1)[1].split() for x in cp if x.startswith('flags'))
    note('Hardware',f'{model}; pinned CPUs {sorted(os.sched_getaffinity(0))}; AVX512BW {"avx512bw" in flags}; VBMI2 {"avx512_vbmi2" in flags}.')
    chosen=json.loads((native/'mixed-release-files.json').read_text())
    release=list(chosen);names=['base',control,old,prototype,*release]
    bin=area/'bin';bin.mkdir(exist_ok=True)
    for n in names:
        home=source(n);settings=native/'build-settings'/n
        if settings.exists():
            for f,t in [('build.sh','build.sh'),('Dockerfile','Dockerfile'),('train.py','perf/train.py')]:shutil.copy2(settings/f,home/t)
        if n in chosen:
            for f,h in chosen[n].items():assert hashlib.sha256((home/f).read_bytes()).hexdigest()==h,(n,f)
        if n==old:
            for f,h in json.loads((native/'official-088-files.json').read_text()).items():assert hashlib.sha256((home/f).read_bytes()).hexdigest()==h,(n,f)
        run('sh','build.sh',cwd=home,extra={'PGO':'1','PYTHON':python,'CXX':'g++-12'})
        exe=bin/n;shutil.copy2(home/'build/shopping',exe)
        run(python,root/'verify.py',exe,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
        if n in release:
            unit=area/'field-regression'
            run('g++-12','-O2','-std=c++20','-fsanitize=undefined','-fno-sanitize-recover=all','-I',home/'cpp',native/'release_event_batches.cpp','-o',unit)
            for extra in caps.values():run(unit,extra=extra)
    note('Correctness',f'{len(names)} builds passed all 36 fixtures and both published tables. All clean releases matched their source manifests and the scalar event parser.')
    select('Build identity and fixtures',('Hardware ','Correctness '))
    times={};winner=None
    gen=area/'generator';generator=gen/'target/release/generate'
    for scale,seed in [('practice',23),('final',47),('holdout',733)]:
        if scale=='final':
            with (native/'generator-memory.patch').open('rb') as p:subprocess.run(['patch','-s','-p1'],cwd=gen,stdin=p,check=True)
            run('cargo','build','--release','--offline',cwd=gen)
            for part in range(3):run(generator,'--seed','1','--events','1000000','--clicks','250000','--threads','4','--output',area/'dev-check',extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
            run('diff','-qr',area/'dev',area/'dev-check')
        data=area/scale;answer=area/(scale+'-oracle')
        if scale!='practice':
            for part in range(3):run(generator,'--seed',str(seed),'--events','60000000','--clicks','15000000','--threads','4','--output',data,extra={'PART_CLASSES':'3','PART_CLASS':str(part)})
        run(bin/control,data,answer)
        for cap,extra in caps.items():
            contestants=names if cap=='auto' else [control,*release]
            for n in contestants:
                if n!=control:compare_tables(bin/n,data,answer,extra)
            totals=measure([bin/n for n in contestants],contestants,data,cap+'-'+scale,5 if scale=='practice' else 3,extra)
            times[(scale,cap)]=totals
            note('Compact '+cap+' '+scale,' / '.join(f'{n} {totals[n]:.4f}' for n in contestants))
            select('Same-binary '+cap+' comparison '+scale,('Compact '+cap+' '+scale,))
        if scale=='practice':continue
        if scale=='final':
            def score(n):
                vals=[]
                for cap in caps:
                    practice=times['practice',cap];final=times['final',cap]
                    vals.append((practice[n]/practice[control]*(final[n]/final[control])**2)**(1/3))
                return max(vals),statistics.mean(vals)
            winner=min(release,key=score)
            note('Image selection',winner+' lowest worst-path ratio across automatic, F/BW and AVX2 modes using 20M and seed 47. Seed 733 held out.')
        verify_images(root,area,winner,run,note,env,[('practice',5),('final',3)] if scale=='final' else [('holdout',3)],control)
        select('Release Docker '+scale,('Image selection','Image binary ','Release image pairs ','Docker fixture binary ','Docker practice ','Docker final ','Docker holdout '))
        # Repeat the reduced-ISA comparisons using the actual Debian Docker ELFs.
        if scale=='final':
            for tag,filename in [('candidate',winner),('release',control)]:
                cid=subprocess.check_output(['docker','create','shopping-native:'+tag],text=True).strip()
                try:run('docker','cp',cid+':/app/shopping',bin/(filename+'-docker'))
                finally:run('docker','rm',cid)
            for cap in ('bw','avx2'):
                run(python,root/'verify.py',bin/(winner+'-docker'),area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer',extra=caps[cap])
        labels=[control+'-docker',winner+'-docker']
        for other in (['practice','final'] if scale=='final' else ['holdout']):
            for cap in ('bw','avx2'):
                compare_tables(bin/labels[1],area/other,area/(other+'-oracle'),caps[cap])
                result=measure([bin/n for n in labels],labels,area/other,'elf-'+other+'-'+cap,5 if other=='practice' else 3,caps[cap])
                note('Image ELF '+other+' '+cap, f'{control} {result[labels[0]]:.4f} s; {winner} {result[labels[1]]:.4f} s. Full tables matched.')
        select('Debian executable, older Intel paths '+scale,('Image ELF ',))
        if scale=='final':shutil.rmtree(data)
        else:
            with tempfile.TemporaryDirectory(dir=area) as tmp:
                mem=area/'eight-worker-memory';run('/usr/bin/time','-o',mem,'-f','%M',bin/labels[1],data,tmp,extra={'NCPU':'8'})
                run(python,native/'compare_large.py',tmp,answer)
                note('Eight worker memory',f'{winner}, {mem.read_text().strip()} KiB maximum RSS for the exact Docker executable on seed 733, full outputs matched.')
    generator=area/'long-alias-generator';run('g++-12','-std=c++20','-O2',native/'long_location_input.cpp','-o',generator)
    skew(area,generator,[bin/(winner+'-docker')],note)
    race(root,area,winner,note,run,env)
    select('Race, skew and final memory',('ThreadSanitizer ','Skewed revision locations ','Eight worker memory '))
if __name__=='__main__':
    try:main()
    except BaseException as ex:
        note('Failure',repr(ex));select('Mixed release qualification failure',('Failure ',));raise
