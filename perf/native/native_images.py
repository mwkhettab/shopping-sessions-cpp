"""Measure the release image and official baseline on a native Docker engine."""
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from statistics import median

def verify_images(root,area,best,run,note,env):
    meter=area/'image-meter'
    run('gcc','-O2','-static',root/'perf/native/meter.c','-o',meter)
    tags=['shopping-native:baseline','shopping-native:candidate']
    src=area/best
    pgo=best.endswith('-pgo')
    release='pgo' if pgo else 'standard'
    for f in ('build.sh','Dockerfile','.dockerignore'):
        shutil.copy2(root/'perf/native'/('image-'+release+'-'+f.lstrip('.')),src/f)
    if pgo:
        (src/'perf').mkdir(exist_ok=True)
        shutil.copy2(root/'perf/native/train.py',src/'perf/train.py')
    control=area/'base'
    for f in ('build.sh','Dockerfile','.dockerignore'):
        shutil.copy2(root/'perf/native'/('image-pgo-'+f.lstrip('.')),control/f)
    (control/'perf').mkdir(exist_ok=True)
    shutil.copy2(root/'perf/native/train.py',control/'perf/train.py')
    run('docker','build','--platform','linux/amd64','-t',tags[0],control)
    run('docker','build','--platform','linux/amd64','-t',tags[1],src)
    fingerprint=subprocess.check_output(['docker','run','--rm','--platform','linux/amd64','--entrypoint','sha256sum',tags[1],'/app/shopping'],text=True).split()[0]
    note('Image binary '+best,f'sha256 {fingerprint}')
    for scale,repeats in [('practice',5),('final',3)]:
        data=area/scale
        totals={tag:[] for tag in tags}
        for round_num in range(-1,repeats):
            for tag in (tags if round_num%2==0 else tags[::-1]):
                for path in data.rglob('*.csv'):
                    with path.open('rb',buffering=0) as file:
                        block=bytearray(1<<20)
                        while file.readinto(block): pass
                with tempfile.TemporaryDirectory(dir=area) as tmp:
                    tmp=Path(tmp)
                    out=tmp/'out';out.mkdir();out.chmod(0o777)
                    scratch=tmp/'scratch';scratch.mkdir();scratch.chmod(0o777)
                    cmd=['docker','run','--rm','--platform','linux/amd64','--network','none','--read-only',
                        '--tmpfs','/tmp:rw,size=64m,mode=1777','--cap-drop','ALL','--security-opt','no-new-privileges',
                        '--pids-limit','4096','--cpuset-cpus',','.join(str(n) for n in sorted(os.sched_getaffinity(0))),
                        '--cpus',env['NCPU'],'--memory','12g','--memory-swap','12g','--user','1000:1000',
                        '-v',str(data)+':/data:ro','-v',str(out)+':/output','-v',str(scratch)+':/scratch',
                        '-v',str(meter)+':/meter:ro','-e','DATA_DIR=/data','-e','OUTPUT_DIR=/output','-e','SCRATCH_DIR=/scratch',
                        '-e','HOME=/scratch','-e','NCPU='+env['NCPU'],'-e','MEM_GB=12','--entrypoint','/meter',tag]
                    try:
                        start=time.monotonic()
                        proc=subprocess.run(cmd,capture_output=True,text=True,env=env,check=True)
                        external=time.monotonic()-start
                        m=re.search(r'wall ([.\d]+) s, process peak RSS ([.\d]+) MiB, cgroup peak ([.\d]+) MiB',proc.stdout)
                        if not m: raise ValueError(proc.stdout[-1500:])
                        if round_num>=0: totals[tag].append((*map(float,m.groups()),external))
                        else: run(os.sys.executable,root/'perf/native/compare_large.py',out,area/(scale+'-oracle'))
                    finally: subprocess.run(['sudo','-n','chmod','-R','a+rwX',out,scratch],check=True)
        for tag,obs in totals.items():
            note('Docker '+scale+' '+(best if tag==tags[1] else 'base'),
                 f'median {median(x[0] for x in obs):.3f} s, runs {[x[0] for x in obs]}, peak RSS {max(x[1] for x in obs):.1f} MiB, cgroup {max(x[2] for x in obs):.1f} MiB, median including docker run {median(x[3] for x in obs):.3f} s.')
