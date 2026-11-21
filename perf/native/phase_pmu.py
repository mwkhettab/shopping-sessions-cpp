"""Count native PMU samples between the executable's monotonic phase boundaries."""
import bisect
import json
import os
import pwd
import re
import shutil
import subprocess
import sys
import tempfile
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
  }
  Timer() { boundary("begin"); }
'''
    timer = timer.replace('struct Timer {', 'struct Timer {' + trace)
    mark = 'void mark(const char *name) {'
    assert mark in timer
    timer = timer.replace(mark, mark + '\n    boundary(name);')
    path.write_text('#include <time.h>\n' + source[:begin] + timer + source[end:])


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
        checked = False
        for event, period in [('cycles',750001), ('instructions',1000003), ('branch-misses',7001), ('cache-misses',7001), ('cpu-clock',250001)]:
            samples = work/(event+'.data')
            cmd = ['sudo','-n','perf','record','-q','--clockid','mono','-e',event,'-c',str(period),'-o',str(samples),'--',*task]
            proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
            if proc.returncode:
                note('Phase counter unavailable '+variant+' '+event, proc.stderr.strip()[-200:]); continue
            if not checked:
                run(sys.executable,root/'perf/native/compare_large.py',output,area/'practice-oracle')
                checked = True
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
