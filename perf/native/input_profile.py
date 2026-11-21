import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time

def collect(root,area,variant,note,run,env):
    source=area/variant
    probe=area/'input-probe'
    cmd=['g++-12','-std=c++20','-O3','-DNDEBUG','-pthread','-I',source/'cpp',root/'perf/native/input_probe.cpp','-o',probe]
    def measure(build,destination):
        times=[[],[],[]];checks={}
        for cycle in range(6):
            for mode in [(i+cycle)%3 for i in range(3)]:
                start=time.monotonic()
                sig=subprocess.check_output([probe,area/'practice',str(mode),env['NCPU']],env=env,text=True).strip()
                elapsed=time.monotonic()-start
                if cycle:times[mode].append(round(elapsed,4))
                if checks.setdefault(mode,sig)!=sig:raise ValueError('non-deterministic input probe')
        for i,values in enumerate(times):
            note('Input probe '+variant+' '+('field_lengths','semantic_validation','validation_and_keys')[i],f'{build}, checksummed scan, median {statistics.median(values):.4f} s, runs {values}, digest {checks[i]}')
        destination.write_text(json.dumps({'variant':variant,'cycles':times,'digests':checks}))
        return checks
    run(*cmd)
    ordinary=measure('GCC -O3 without PGO',area/'results'/('input-probe-'+variant+'.json'))
    with tempfile.TemporaryDirectory(dir=area) as folder:
        profile_dir=Path(folder)/'counters'
        data=area/'training-input-probe'
        if not data.exists():run(os.sys.executable,root/'perf/native/train.py',data)
        run(*cmd,'-fprofile-generate='+str(profile_dir),'-fprofile-update=atomic')
        for mode in range(3):
            for form in ('short','wide'):run(probe,data/form,str(mode),env['NCPU'])
        run(*cmd,'-fprofile-use='+str(profile_dir),'-fprofile-correction','-Werror=missing-profile')
        trained=measure('synthetic GCC PGO',area/'results'/('input-probe-pgo-'+variant+'.json'))
        if trained!=ordinary:raise ValueError('PGO changed input checksums')
    if not shutil.which('perf'):return
    with tempfile.TemporaryDirectory(dir=area) as directory:
        directory=Path(directory)
        output=directory/'output';output.mkdir()
        profile=directory/'profile.data'
        who=__import__('pwd').getpwuid(os.getuid()).pw_name
        cmd=['sudo','-n','perf','record','-q','-F','997','-e','cpu-clock','-o',profile,'--','runuser','-u',who,'--',area/'bin'/variant,area/'practice',output]
        p=subprocess.run(cmd,text=True,capture_output=True,env=env)
        if p.returncode:
            note('Native sampling unavailable',p.stderr.strip()[-260:])
            return
        rep=subprocess.run(['sudo','-n','perf','report','--stdio','--no-children','--percent-limit','1','--sort','symbol','-i',profile],capture_output=True,text=True,env=env)
        (area/'results'/('cpu-samples.txt' if variant=='base' else 'cpu-samples-'+variant+'.txt')).write_text(rep.stdout)
        import re
        top=[line.strip() for line in rep.stdout.splitlines() if re.match(r'^\s*\d+\.\d+%',line)]
        note('Native CPU samples '+variant,' / '.join(line[:155] for line in top[:7]))
