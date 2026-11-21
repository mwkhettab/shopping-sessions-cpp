from pathlib import Path
import json,os,statistics,subprocess,tempfile

def collect(root,area,best,note,run,env):
    names=[n for n in dict.fromkeys(['shipresident-pgo','resordinal-pgo','vertkeys2-pgo',best]) if (area/n/'cpp').is_dir()]
    if not any(n.startswith('vert') for n in names):return
    train=area/'projected-training'
    if not train.exists():run(os.sys.executable,root/'perf/native/train.py',train)
    results={};signatures={}
    with tempfile.TemporaryDirectory(dir=area) as work:
        work=Path(work);programs={}
        for n in names:
            path=work/n;path.mkdir();exe=path/'probe';profile=path/'counters'
            cmd=['g++-12','-std=c++20','-O3','-DNDEBUG','-pthread','-I'+str(area/n/'cpp'),str(root/'perf/native/vertical_probe.cpp'),'-o',str(exe)]
            run(*cmd,'-fprofile-generate='+str(profile),'-fprofile-update=atomic',cwd=path)
            for dataset in ('short','wide'):
                for mode in range(3):
                    subprocess.run([exe,train/dataset,env['NCPU'],str(mode)],cwd=path,env=env,stdout=subprocess.DEVNULL,check=True)
            run(*cmd,'-fprofile-use='+str(profile),'-fprofile-correction','-Werror=missing-profile',cwd=path)
            programs[n]=exe
        for mode,label in enumerate(('accepted fields','fields and keys','fields, keys and both times')):
            digests={};obs={name:[] for name in programs}
            for cycle in range(-1,5):
                order=names[cycle%len(names):]+names[:cycle%len(names)]
                for name in order:
                    raw=subprocess.check_output([programs[name],area/'practice',env['NCPU'],str(mode)],env=env,text=True)
                    elapsed,*vals=raw.strip().split()
                    signature=tuple(map(int,vals))
                    if digests.setdefault(name,signature)!=signature:raise ValueError('non-deterministic reader digest '+name)
                    if cycle>=0:obs[name].append(float(elapsed))
            expected=digests[names[0]][:4]
            if any(v[:4]!=expected for v in digests.values()):raise ValueError('unequal reader projections '+str(digests))
            results[label]=obs;signatures[label]=digests
            note('Event projection '+label,' / '.join(f'{n} {statistics.median(times):.4f} s, {digests[n][4]}/{digests[n][5]} direct/batched' for n,times in obs.items()))
    (area/'results/projected-events.json').write_text(json.dumps({'times':results,'digests':signatures}))
