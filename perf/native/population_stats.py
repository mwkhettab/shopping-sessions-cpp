"""Measure the number of timelines, time bins and sessions from actual winners."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

def describe(path):
    by_type={}
    for line in Path(path).read_text().splitlines():
        fields=line.split()
        if len(fields)!=9 or fields[0] not in ('rawcounts','personcounts','clicksessioncounts'):continue
        by_type.setdefault(fields[0],[]).append(tuple(map(int,fields[1:])))
    people=by_type['personcounts'];raw=by_type['rawcounts'];matrix=by_type['clicksessioncounts']
    def total(rows,col):return sum(r[col] for r in rows)
    return (f"{total(people,2)} people, {total(people,3)} winning events, {total(people,6)} sessions. "
            f"Occupied bins {total(raw,5)} per raw alias and {total(people,5)} per linked person in total. "
            f"People with at least 128 events account for {total([r for r in people if r[0]>=8],3)} events. "
            f"Single-session people {total([r for r in matrix if r[1]==1],2)} with "
            f"{total([r for r in matrix if r[1]==1],4)} clicks. "
            f"People with at most two clicks {total([r for r in matrix if r[0]<=2],2)}.")

def collect(root,area,note,run,env):
    with tempfile.TemporaryDirectory(dir=area) as temp:
        work=Path(temp)
        shutil.copytree(area/'base/cpp',work/'cpp')
        with (root/'perf/native/population-profile.patch').open('rb') as patch:
            subprocess.run(['patch','-s','-p1'],cwd=work,stdin=patch,check=True)
        run('g++-12','-std=c++20','-O3','-DNDEBUG','-pthread',work/'cpp/main.cpp','-o',work/'probe')
        for scale in ('practice','holdout'):
            report=area/'results'/('population-'+scale+'.txt')
            with tempfile.TemporaryDirectory(dir=work) as out, report.open('w') as log:
                subprocess.run([work/'probe',area/scale,out],stderr=log,check=True,env=env)
                run(os.sys.executable,root/'perf/native/compare_large.py',out,area/(scale+'-oracle'))
            note('Population structure '+scale,describe(report))
