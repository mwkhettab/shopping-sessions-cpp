"""Measure the effect of worker count and exercise shared graph mutations."""
import json
import os
import shlex
import statistics
import subprocess
from pathlib import Path


def scaling(root, area, name, note, run, env):
    for threads in ('1','2','4'):
        record = area/'results'/('workers-'+threads+'.jsonl')
        paths = [area/'bin/base', area/'bin'/name]
        run(os.sys.executable,root/'perf/measure.py',area/'practice',*paths,
            '--runs','3','--warmup','1','--preload','--output-parent',area,
            '--log',record,extra={'NCPU':threads})
        observations = [json.loads(x) for x in record.read_text().splitlines()]
        for path in paths:
            samples = [round(x['wall_s'],4) for x in observations if x['round']>=0 and x['binary']==str(path)]
            note('Workers '+threads+' '+path.name,f'median {statistics.median(samples):.4f} s, runs {samples}')
    topology={}
    for cpu in sorted(os.sched_getaffinity(0)):
        folder=Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
        if not (folder/'core_id').exists():return
        key=tuple((folder/f).read_text().strip() for f in ('physical_package_id','core_id'))
        topology.setdefault(key,[]).append(cpu)
    note('Runner CPU topology',str(list(topology.values()))+' are the visible CPU IDs sharing each reported core.')
    subsets={}
    for cpus in topology.values():
        if len(cpus)>1:subsets['same-core']=cpus[:2];break
    if len(topology)>1:subsets['distinct-cores']=[cpus[0] for cpus in list(topology.values())[:2]]
    for label,cpus in subsets.items():
        record=area/'results'/('affinity-'+label+'.jsonl')
        paths=list(dict.fromkeys([area/'bin/shipreader-pgo',area/'bin'/name]))
        paths=[p for p in paths if p.is_file()]
        run('taskset','-c',','.join(map(str,cpus)),os.sys.executable,root/'perf/measure.py',area/'practice',*paths,
            '--runs','3','--warmup','1','--preload','--output-parent',area,'--log',record,extra={'NCPU':'2'})
        for path in paths:
            obs=[r['wall_s'] for r in map(json.loads,record.read_text().splitlines()) if r['round']>=0 and r['binary']==str(path)]
            note('Two pinned CPUs '+label+' '+path.name, f'CPUs {cpus}, median {statistics.median(obs):.4f} s.')


def race(root, area, name, note, run, env):
    flags=['g++-12','-std=c++20','-O1','-g','-fno-omit-frame-pointer','-fno-pie','-no-pie','-pthread','-fsanitize=thread']
    probe=area/'thread-probe.cpp'
    probe.write_text('#include <thread>\nint main(){int x=0;std::thread t([&]{x=1;});t.join();return x!=1;}\n')
    run(*flags,probe,'-o',area/'thread-probe')
    prefixes=[['setarch','x86_64','-R'],[]]
    for prefix in prefixes:
        p=subprocess.run([*prefix,area/'thread-probe'],capture_output=True,text=True,env=env)
        if p.returncode==0:
            break
    else:
        note('ThreadSanitizer unavailable',p.stderr.strip()[-300:])
        return
    run(*flags,area/name/'cpp/main.cpp','-o',area/'race-check')
    wrapper=area/'run-race'
    wrapper.write_text('#!/bin/sh\nexec '+shlex.join([*prefix,str(area/'race-check')])+' "$@"\n')
    wrapper.chmod(0o755)
    run(os.sys.executable,root/'tests/catalog.py',wrapper,extra={'TSAN_OPTIONS':'halt_on_error=1','NCPU':'8'})
    run(os.sys.executable,root/'perf/native/interval_oracle.py',wrapper,extra={'TSAN_OPTIONS':'halt_on_error=1','NCPU':'8'})
    note('ThreadSanitizer '+name,'All extended regressions passed at eight workers, including the linked-graph and interval-attribution cases.')
