"""Read native Docker lifecycle timestamps for the exact image command and an empty control."""
from datetime import datetime
import os,shutil,statistics,subprocess,tempfile
from pathlib import Path

def measure_lifetime(area,scale,env,note,compare,rounds=3):
    data=area/scale;names=['release','candidate','empty'];records={k:[] for k in names}
    cpus=','.join(map(str,sorted(os.sched_getaffinity(0))))
    for i in range(-1,rounds):
        k=i%3
        for name in names[k:]+names[:k]:
            with tempfile.TemporaryDirectory(dir=area) as path:
                tmp=Path(path);out=tmp/'out';out.mkdir();out.chmod(0o777);scratch=tmp/'scratch';scratch.mkdir();scratch.chmod(0o777)
                if name!='empty':
                    for f in data.rglob('*.csv'):
                        with f.open('rb',buffering=0) as fp:
                            buf=bytearray(1<<20)
                            while fp.readinto(buf):pass
                cmd=['docker','create','--platform','linux/amd64','--network','none','--read-only','--user','1000:1000','--cpuset-cpus',cpus,'--cpus',env['NCPU'],'--memory','12g','--memory-swap','12g','--cap-drop','ALL','--security-opt','no-new-privileges','--tmpfs','/tmp:rw,size=64m,mode=1777','--pids-limit','4096','-e','NCPU='+env['NCPU'],'-e','HOME=/scratch','-v',str(data)+':/data:ro','-v',str(out)+':/output','-v',str(scratch)+':/scratch']
                if name=='empty':cmd+=['--entrypoint','/bin/true','shopping-native:candidate']
                else:cmd+=['shopping-native:'+name]
                cid=subprocess.check_output(cmd,text=True).strip()
                try:
                    subprocess.run(['docker','start','--attach',cid],check=True)
                    a,b,code=subprocess.check_output(['docker','inspect','-f','{{.State.StartedAt}} {{.State.FinishedAt}} {{.State.ExitCode}}',cid],text=True).split()
                    assert code=='0';delta=(datetime.fromisoformat(b.replace('Z','+00:00'))-datetime.fromisoformat(a.replace('Z','+00:00'))).total_seconds()
                    if i>=0:records[name].append(delta)
                    elif name!='empty':subprocess.run([os.sys.executable,compare,out,area/(scale+'-oracle')],check=True)
                finally:subprocess.run(['docker','rm','-f',cid],check=True,stdout=subprocess.DEVNULL)
    note('Container lifetime '+scale,' / '.join(f'{name} median {statistics.median(times):.4f}s, runs {times}' for name,times in records.items()))
