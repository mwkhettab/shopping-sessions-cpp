"""Measure the release image and official baseline on a native Docker engine."""
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from statistics import median

def verify_images(root,area,best,run,note,env,scales):
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
    settings=root/'perf/native/build-settings'/best
    if settings.is_dir():
        shutil.copy2(settings/'build.sh',src/'build.sh')
        shutil.copy2(settings/'Dockerfile',src/'Dockerfile')
        shutil.copy2(settings/'train.py',src/'perf/train.py')
    control=area/'base'
    for f in ('build.sh','Dockerfile','.dockerignore'):
        shutil.copy2(root/'perf/native'/('image-pgo-'+f.lstrip('.')),control/f)
    (control/'perf').mkdir(exist_ok=True)
    shutil.copy2(root/'perf/native/train.py',control/'perf/train.py')
    run('docker','build','--platform','linux/amd64','-t',tags[0],control)
    run('docker','build','--platform','linux/amd64','-t',tags[1],src)
    labels={tags[0]:'base',tags[1]:best}
    released=area/'shipreader-pgo'
    if released.is_dir() and best!='shipreader-pgo':
        for f in ('build.sh','Dockerfile','.dockerignore'):
            shutil.copy2(root/'perf/native'/('image-pgo-'+f.lstrip('.')),released/f)
        (released/'perf').mkdir(exist_ok=True)
        shutil.copy2(root/'perf/native/train.py',released/'perf/train.py')
        tags.append('shopping-native:release')
        labels[tags[-1]]='shipreader-pgo'
        run('docker','build','--platform','linux/amd64','-t',tags[-1],released)

    fingerprint=subprocess.check_output(['docker','run','--rm','--platform','linux/amd64','--entrypoint','sha256sum',tags[1],'/app/shopping'],text=True).split()[0]
    note('Image binary '+best,f'sha256 {fingerprint}')
    fast=subprocess.run(['docker','run','--rm','--platform','linux/amd64','--entrypoint','sha256sum',tags[1],'/app/shopping.opt'],capture_output=True,text=True)
    if fast.returncode==0:note('Image optimized binary '+best,'sha256 '+fast.stdout.split()[0])
    if any(scale=='practice' for scale,_ in scales):
        with tempfile.TemporaryDirectory(dir=area) as tmp:
            binary=Path(tmp)/'shopping'
            container=subprocess.check_output(['docker','create',tags[1]],text=True).strip()
            try:
                run('docker','cp',container+':/app/shopping',binary)
                if fast.returncode==0:run('docker','cp',container+':/app/shopping.opt',str(binary)+'.opt')
            finally:run('docker','rm',container)
            import hashlib
            assert hashlib.sha256(binary.read_bytes()).hexdigest()==fingerprint
            run(os.sys.executable,root/'verify.py',binary,area/'dev','--starter',area/'starter-pyspark','--answer',area/'dev-answer')
            for suite in ('random_rules.py','interval_oracle.py','page_oracle.py','prefix_oracle.py','ordered_ids_oracle.py'):
                run(os.sys.executable,root/'perf/native'/suite,binary)
            note('Docker fixture binary '+best,f'{fingerprint} passed 36 fixtures, published 1M tables and all five independent differential suites.')
    for scale,repeats in scales:
        data=area/scale
        totals={tag:[] for tag in tags}
        for round_num in range(-1,repeats):
            offset=round_num%len(tags)
            for tag in tags[offset:]+tags[:offset]:
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
        if len(tags)==3:
            pairs=[a[0]/b[0]-1 for a,b in zip(totals[tags[1]],totals[tags[2]])]
            note('Release image pairs '+scale,f'{best} vs shipreader. {sum(x<0 for x in pairs)}/{len(pairs)} faster and {median(pairs):.2%} median paired change.')
        for tag,obs in totals.items():
            note('Docker '+scale+' '+labels[tag],
                 f'median {median(x[0] for x in obs):.3f} s, runs {[x[0] for x in obs]}, peak RSS {max(x[1] for x in obs):.1f} MiB, cgroup {max(x[2] for x in obs):.1f} MiB, median including docker run {median(x[3] for x in obs):.3f} s.')
