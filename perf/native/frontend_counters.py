"""Inspect hot code size and any instruction-fetch counters exposed by this VM."""
from pathlib import Path
import json,os,pwd,statistics,subprocess,tempfile

def run_study(root,area,labels,note,env):
    who=pwd.getpwuid(os.getuid()).pw_name
    catalog=subprocess.run(['perf','list','--raw-dump'],text=True,capture_output=True).stdout.lower().split()
    targets=[('instruction cache',('frontend_retired.l1i_miss','icache_data.stalls','icache_64b.iftag_stall','L1-icache-load-misses')),
             ('instruction translations',('itlb_misses.walk_completed','iTLB-load-misses')),
             ('front-end delivery',('idq_uops_not_delivered.core','stalled-cycles-frontend'))]
    logs={}
    for label in labels:
        binary=area/'bin'/label
        if not binary.exists():continue
        syms=subprocess.check_output(['nm','-S','-C','--size-sort',binary],text=True)
        sizes={}
        for line in syms.splitlines():
            fields=line.split(maxsplit=3)
            if len(fields)!=4 or fields[2] not in ('t','T','W','w'):continue
            name=fields[3]
            for marker in ('void summarize<WordLayout>','void tiled_lines<7','void ordinal16_wide<7','void ordinal16_standard<7','void process_people<WordLayout>'):
                if name.startswith(marker):
                    sizes[marker]=max(sizes.get(marker,0),int(fields[1],16))
        note('Hot symbol sizes '+label,', '.join(f'{k} {v:,} bytes' for k,v in sizes.items()))
        with tempfile.TemporaryDirectory(dir=area) as work:
            work=Path(work)
            notes=[]
            for description,choices in targets:
                event=next((k for k in choices if k.lower() in catalog),choices[-1])
                series=[]
                for iteration in range(3):
                    with tempfile.TemporaryDirectory(dir=work) as out:
                        p=subprocess.run(['sudo','-n','perf','stat','-x','\t','-e','instructions,cycles,'+event,'--',
                                          'runuser','-u',who,'--',str(binary),str(area/'practice'),out],capture_output=True,text=True,env=env)
                    fields={}
                    for line in p.stderr.splitlines():
                        cells=line.split('\t')
                        if len(cells)>=3 and cells[0].strip().isdigit():fields[cells[2].strip().partition(':')[0]]=int(cells[0])
                    if p.returncode or not fields.get('instructions') or event not in fields:
                        break
                    series.append(fields)
                logs[label+'/'+event]=series
                if series:
                    notes.append(f'{description} ({event}) {statistics.median(r[event]*1000/r["instructions"] for r in series):.3f} counts per 1,000 instructions ({len(series)} runs)')
                else:notes.append(f'{event} unavailable')
            note('Instruction supply '+label,' / '.join(notes))
    (area/'results/instruction-supply.json').write_text(json.dumps(logs))
