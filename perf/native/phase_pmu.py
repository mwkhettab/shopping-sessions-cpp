"""Count native PMU samples between the executable's monotonic phase boundaries."""
import bisect
import json
import os
import pwd
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def instrument(path):
    source = path.read_text()
    begin = source.index('struct Timer {')
    end = source.index('\n};', begin)
    timer = source[begin:end]
    trace = '''
  bool trace = std::getenv("SHOP_PHASE_TRACE") != nullptr;
  void boundary(const char *label) const {
    if (!trace) return;
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) throw std::runtime_error("clock_gettime");
    std::cerr << "SHOP_PHASE " << int64_t(t.tv_sec)*1000000000+t.tv_nsec << " " << label << "\\n";
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
      auto us=[](timeval v){return int64_t(v.tv_sec)*1000000+v.tv_usec;};
      std::cerr << "SHOP_USAGE " << usage.ru_minflt << " " << usage.ru_majflt << " " << us(usage.ru_utime) << " " << us(usage.ru_stime) << " " << usage.ru_maxrss << " " << label << "\\n";
    }
  }
  Timer() { boundary("begin"); }
'''
    timer = timer.replace('struct Timer {', 'struct Timer {' + trace)
    mark = 'void mark(const char *name) {'
    assert mark in timer
    timer = timer.replace(mark, mark + '\n    boundary(name);')
    from reader_duty import add_timing
    path.write_text('#include <sys/resource.h>\n#include <time.h>\n' + add_timing(source[:begin] + timer + source[end:]))


def bucket(stderr, samples):
    boundaries = []
    for line in stderr.splitlines():
        match = re.fullmatch(r'SHOP_PHASE (\d+) (.+)', line)
        if match:
            boundaries.append((int(match[1]), match[2]))
    if len(boundaries) < 3 or boundaries[0][1] != 'begin':
        raise ValueError('Missing process phase markers')
    times = [t for t, name in boundaries]
    names = ['startup', *[name for t, name in boundaries[1:]], 'teardown']
    counts = [0] * len(names)
    for line in samples.splitlines():
        match = re.fullmatch(r'\s*shopping\s+(\d+)\.(\d{1,9}):?\s*', line)
        if match:
            ns = int(match[1]) * 1_000_000_000 + int(match[2].ljust(9, '0'))
            counts[bisect.bisect_right(times, ns)] += 1
    return list(zip(names, counts)), [(name,(t-times[i]) / 1e9) for i,(t,name) in enumerate(boundaries[1:])]


def collect(root, area, variant, note, run, env):
    if not shutil.which('perf'):
        return
    with tempfile.TemporaryDirectory(dir=area) as temp:
        work = Path(temp)
        for part in ('cpp', 'perf'):
            shutil.copytree(area/variant/part, work/part)
        settings = root/'perf/native/build-settings'/variant
        shutil.copy2(settings/'build.sh' if settings.is_dir() else root/'perf/native/image-pgo-build.sh', work/'build.sh')
        instrument(work/'cpp/main.cpp')
        run('sh', 'build.sh', cwd=work, extra={'CXX':'g++-12','PGO':'1'})
        output = work/'output'
        who = pwd.getpwuid(os.getuid()).pw_name
        task = ['runuser', '-u', who, '--', 'env', 'SHOP_PHASE_TRACE=1', str(work/'build/shopping'), str(area/'practice'), str(output)]
        results = {}
        for scale,repeats in [('practice',3),('holdout',1)]:
            if not (area/scale).is_dir():continue
            duties={};ranges=[];resources=[]
            for sample in range(repeats):
                with tempfile.TemporaryDirectory(dir=work) as tempout:
                    begin=time.monotonic_ns()
                    proc=subprocess.run([work/'build/shopping',area/scale,tempout],env=env|{'SHOP_PHASE_TRACE':'1','SHOP_LAYOUT_TRACE':'1'},capture_output=True,text=True,check=True)
                    end=time.monotonic_ns()
                    phases=[int(x) for x in re.findall(r'SHOP_PHASE (\d+) ',proc.stderr)]
                    if len(phases)<3:raise ValueError('absent unprofiled stage clocks')
                    ranges.append(((phases[0]-begin)/1e9,(end-phases[-1])/1e9))
                    usage=[]
                    for hit in re.finditer(r'SHOP_USAGE (\d+) (\d+) (\d+) (\d+) (\d+) ([^\n]+)',proc.stderr):
                        usage.append((hit[6],*[int(v) for v in hit.groups()[:5]]))
                    resources.append(usage)
                    for kind,ns,jobs in re.findall(r'SHOP_INPUT (\w+) (\d+) (\d+)',proc.stderr):
                        duties.setdefault(kind,[]).append((int(ns)/1e9,int(jobs)))
                    if sample==0:
                        run(sys.executable,root/'perf/native/compare_large.py',tempout,area/(scale+'-oracle'))
                        mix=re.search(r'SHOP_REVISION_LAYOUT (\d+) (\d+)',proc.stderr)
                        if mix:note('Revision storage '+variant+' '+scale,f'{mix[1]} row and {mix[2]} column revisions.')
            note('Reader CPU by input '+variant+' '+scale,', '.join(f'{k} {statistics.median(x[0] for x in v):.3f} core-s over {v[0][1]} chunks' for k,v in duties.items()))
            note('Process boundaries '+variant+' '+scale,f'{repeats} unsampled run(s), process start to initialization {statistics.median(a for a,b in ranges):.4f} s, output close to process return {statistics.median(b for a,b in ranges):.4f} s.')
            if resources and len(resources[0])>1:
                summary=[]
                for i in range(1,len(resources[0])):
                    d=[(r[i][1]-r[i-1][1],r[i][2]-r[i-1][2],(r[i][4]-r[i-1][4])/1e6,r[i][5]/1024) for r in resources]
                    summary.append(f'{resources[0][i][0][:25]} {statistics.median(x[0] for x in d):.0f}/{statistics.median(x[1] for x in d):.0f} faults, {statistics.median(x[2] for x in d):.3f} sys core-s, {max(x[3] for x in d):.0f} MiB high water')
                note('Per-stage resources '+variant+' '+scale,' / '.join(summary))
            (area/'results'/('phase-usage-'+variant+'-'+scale+'.json')).write_text(json.dumps(dict(ranges=ranges,resources=resources,duties=duties)))
        for event, period in [('cycles',750001), ('instructions',1000003), ('branch-misses',7001), ('cache-misses',7001), ('cpu-clock',250001)]:
            samples = work/(event+'.data')
            cmd = ['sudo','-n','perf','record','-q','--clockid','mono','-e',event,'-c',str(period),'-o',str(samples),'--',*task]
            proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
            if proc.returncode:
                note('Phase counter unavailable '+variant+' '+event, proc.stderr.strip()[-200:]); continue
            raw = subprocess.run(['sudo','-n','perf','script','--ns','-F','comm,time','-i',str(samples)], env=env,capture_output=True,text=True)
            if raw.returncode:
                note('Phase samples unavailable '+variant+' '+event,raw.stderr.strip()[-200:]); continue
            counts, elapsed = bucket(proc.stderr, raw.stdout)
            total = sum(n for name,n in counts)
            if not total:
                note('Phase clock diagnostic '+variant+' '+event, repr(raw.stdout[:220])); continue
            results[event] = dict(period=period, counts=counts, wall_seconds=elapsed)
            shares = ', '.join(f'{name[:30]} {100*n/total:.1f}%' for name,n in counts if n*100 >= total)
            note('Phase PMU '+variant+' '+event, f'{total} sampled events at period {period}. '+shares)
        (area/'results'/('phase-counters-'+variant+'.json')).write_text(json.dumps(results))
