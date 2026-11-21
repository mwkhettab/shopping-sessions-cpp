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
    # GitHub displays ten annotations per step. Lead with full release images
    # from each runner and aggregate their other conclusions separately.
    for report in sorted(root.rglob('all-notes.json')):
        notes=json.loads(report.read_text())
        images=[x for x in notes if x.startswith(('Image selection','Image binary','Image optimized binary','Docker fixture binary','Docker practice','Docker final','Docker holdout','Release image pairs'))]
        if images:emit('Release images '+report.parent.name,' || '.join(images)[:4000])
    for report in sorted(root.rglob('all-notes.json')):
        notes=json.loads(report.read_text())
        ranked=[]
        for scale in ('practice','final','holdout'):
            values=[]
            for entry in notes:
                m=re.match(scale+r' (\S+) median ([0-9.]+) s, runs ([^]]+)]',entry)
                if m:values.append((float(m[2]),m[1]))
            if values:ranked.append(scale+' '+', '.join(f'{name} {value:.4f}' for value,name in sorted(values)))
        if ranked:emit('Full-process medians '+report.parent.name,' || '.join(ranked))
        details=[]
        for entry in notes:
            if entry.startswith('Workers '): details.append(entry.partition(', runs')[0])
            elif entry.startswith('Input probe '):
                head,_,tail=entry.partition(', runs');details.append(head+' digest '+tail.partition('digest ')[2])
            elif entry.startswith(('Floor read','Floor lex','Hardware ','Runner CPU topology','Two pinned CPUs','Skewed revision locations','ThreadSanitizer ','Failure','Rejected ')):details.append(entry)
        if details:emit('Workers and cumulative input probes '+report.parent.name,' || '.join(details)[:4000])
    for report in sorted(root.rglob('all-notes.json')):
        notes=json.loads(report.read_text())
        calibration=[x for x in notes if x.startswith('Cross-profile ')]
        if calibration:emit('Compiler training by CPU capability '+report.parent.name,' || '.join(calibration)[:4100])
        balance=[x for x in notes if x.startswith('Per-worker tail ')]
        if balance:emit('Per-worker CPU and stage drain '+report.parent.name,' || '.join(balance)[:4100])
        minimum=[x for x in notes if x.startswith('Minimum Intel ISA ')]
        if minimum:emit('AVX-512F and BW reader audit '+report.parent.name,' || '.join(minimum)[:3000])
        projection=[x for x in notes if x.startswith(('Event projection ','Hot symbol sizes ','Instruction supply '))]
        if projection:emit('Event projections and instruction delivery '+report.parent.name,' || '.join(projection)[:3900])
        keep=[re.sub(r' {2,}',' ',x) for x in notes if x.startswith(('Release pairs','Population structure','ThreadSanitizer','20M process PMU','Sampled branch-misses','Sampled cache-misses','Input probe'))]
        if keep:emit('Paired comparisons and profiles '+report.parent.name,' || '.join(keep)[:4000])
        phase=[x for x in notes if x.startswith(('Reader CPU by input','Process boundaries','Revision storage','Phase PMU','Phase clock diagnostic'))]
        if phase:emit('Hardware samples by execution phase '+report.parent.name,' || '.join(phase)[:4000])
        isolated=[x for x in notes if x.startswith(('Isolated release input','Isolated release digests'))]
        if isolated:emit('Release input write costs '+report.parent.name,' || '.join(isolated)[:4000])
        revision_counts=[x for x in notes if x.startswith('Revision counts ')]
        if revision_counts:emit('Revision sieve and arena counts '+report.parent.name,' || '.join(revision_counts)[:4000])
        memory=[x for x in notes if x.startswith(('Per-stage resources','Timeline storage'))]
        if memory:emit('Memory allocation and phase faults '+report.parent.name,' || '.join(memory)[:4000])
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
