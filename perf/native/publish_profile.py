"""Expose the retained source samples and paired holdout results on public checks."""
import json
import re
import statistics
import sys
from pathlib import Path

def emit(title, message):
    text=message.replace('%','%25').replace('\r','%0D').replace('\n','%0A')
    print(f'::notice file=BENCHMARKS.md,line=1,title={title}::{text}',flush=True)

for root in map(Path,sys.argv[1:]):
    for source in root.rglob('source-lines*.txt'):
        folder=source.parent
        records=[]
        for line in source.read_text(errors='replace').splitlines():
            match=re.match(r'\s*([0-9.]+)%\s+(.+)',line)
            if match and len(records)<22:records.append(match.group(1)+'% '+match.group(2).strip()[:125])
        emit('Source samples '+folder.name+' '+source.stem,' / '.join(records) or 'Source line report contained no samples.')
    for profile in root.rglob('holdout-head-to-head.jsonl'):
        records={}
        for line in profile.read_text().splitlines():
            row=json.loads(line)
            if row['round']>=0:records.setdefault(Path(row['binary']).name,[]).append(row['wall_s'])
        emit('Five paired holdout medians '+profile.parent.name, ', '.join(f'{n} {statistics.median(v):.4f} s' for n,v in records.items()))
    for profile in root.rglob('holdout.jsonl'):
        groups={}
        for line in profile.read_text().splitlines():
            row=json.loads(line)
            if row['round'] >= 0:
                groups.setdefault(Path(row['binary']).name,[]).append(row)
        emit('All held-out entries '+profile.parent.name, ' / '.join(
            f"{name} {statistics.median(r['wall_s'] for r in rows):.4f} s at {max(r['peak_mib'] for r in rows):.0f} MiB"
            for name,rows in groups.items()))
    for report in root.rglob('cpu-samples.txt'):
        rows=[line.strip()[:135] for line in report.read_text(errors='replace').splitlines() if re.match(r'\s*[0-9.]+%',line)]
        if rows:emit('Top CPU symbols '+report.parent.name,' / '.join(rows[:9]))
    for report in root.rglob('all-notes.json'):
        notes=json.loads(report.read_text())
        for title,fields in [('Images',('Image selection','Image binary','Image optimized binary','Docker fixture binary','Docker practice','Docker final','Docker holdout')),
                             ('Release pairs',('Release pairs',)),('Population and race checks',('Population structure','ThreadSanitizer')),
                             ('Hardware counters',('20M process PMU',)),('Misses',('Sampled branch-misses','Sampled cache-misses'))]:
            selected=[re.sub(r' {2,}',' ',x) for x in notes if x.startswith(fields)]
            if selected:emit(title,' || '.join(selected)[:4000])
