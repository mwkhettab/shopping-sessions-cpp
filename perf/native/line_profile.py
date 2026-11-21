"""Map CPU samples from an optimized, independently profiled build to source lines."""
import os
import pwd
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

def collect(root,area,note,run,env,variant="base"):
    if not shutil.which('perf'):return
    with tempfile.TemporaryDirectory(dir=area) as temp:
        work=Path(temp)
        for name in ('cpp','perf'):shutil.copytree(area/variant/name,work/name)
        shutil.copy2(root/'perf/native/image-pgo-build.sh',work/'build.sh')
        wrapper=work/'cxx'
        wrapper.write_text('#!/bin/sh\nexec g++-12 -g "$@"\n')
        wrapper.chmod(0o755)
        run('sh','build.sh',cwd=work,extra={'CXX':str(wrapper),'PGO':'1'})
        exe=work/'build/shopping'
        who=pwd.getpwuid(os.getuid()).pw_name
        profile=work/'samples.data'
        task=['runuser','-u',who,'--',str(exe),str(area/'practice'),str(work/'output')]
        event=['sudo','-n','perf','record','-q','-F','2003','-e','cpu-clock','-o',str(profile),'--']
        got=subprocess.run(event+task,capture_output=True,text=True,env=env)
        if got.returncode:
            note('Line profile unavailable',got.stderr[-250:].strip());return
        report=subprocess.run(['sudo','-n','perf','report','-i',str(profile),'--stdio','--no-children','--percent-limit','0.25','--sort','srcline,dso'],text=True,capture_output=True,env=env)
        (area/'results'/('source-lines.txt' if variant=='base' else 'source-lines-'+variant+'.txt')).write_text(report.stdout+report.stderr)
        relevant=[line.strip() for line in report.stdout.splitlines() if re.match(r'^\s*\d+\.\d+%',line) and '.cpp:' in line or (re.match(r'^\s*\d+\.\d+%',line) and '.hpp:' in line)]
        if relevant:note('Sampled source lines '+variant,' / '.join(x[:145] for x in relevant[:16]))
        else:note('Source line mapping',report.stderr[-220:].strip() or 'Debug sample recorded, no resolved file locations in report.')
        counters=subprocess.run(['sudo','-n','perf','stat','-x','\t','-e','cycles,instructions,branches,branch-misses,task-clock,minor-faults','--',*task],text=True,capture_output=True,env=env)
        rows=[x.strip() for x in counters.stderr.splitlines() if '\t' in x]
        note('20M process PMU '+variant,' / '.join(x[:100] for x in rows)[:1000] or counters.stderr[-160:])

        for counter in ('branch-misses','cache-misses'):
            path=work/(counter+'.data')
            runner=['sudo','-n','perf','record','-q','-e',counter,'-c','10007','-o',str(path),'--']
            proc=subprocess.run(runner+task,capture_output=True,text=True,env=env)
            if proc.returncode: continue
            cmd=['sudo','-n','perf','report','-i',str(path),'--stdio','--no-children','--percent-limit','0.5','--sort','srcline,dso']
            report=subprocess.run(cmd,capture_output=True,text=True,env=env)
            lines=[line.strip() for line in report.stdout.splitlines() if re.match(r'^\s*[0-9.]+%',line)]
            if lines:
                (area/'results'/(counter+'-'+variant+'.txt')).write_text(report.stdout)
                note('Sampled '+counter+' '+variant,' / '.join(lines[:10])[:1700])
