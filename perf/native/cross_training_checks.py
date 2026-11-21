"""Exercise identical machine code under different CPU-feature ceilings."""
from pathlib import Path
import json,os,statistics,tempfile
def compare(root,area,scale,names,run,note):
    selected=[area/'bin'/name for name in names if name.startswith('train') and (area/'bin'/name).is_file()]
    if not selected:return
    data=area/scale;ref=area/(scale+'-oracle');python=os.sys.executable
    for cap in ((2,1,0) if scale=='practice' else (2,)):
        options=selected if scale=='practice' and cap==2 else [p for p in selected if p.name in ('trainauto-pgo','trainboth-pgo','trainpartial-pgo','trainlimited-pgo')]
        extra={'SHOP_CSV_CEILING':str(cap)}
        for binary in options:
            with tempfile.TemporaryDirectory(dir=area) as output:
                run(binary,data,output,extra=extra)
                run(python,root/'perf/native/compare_large.py',output,ref)
        path=area/'results'/f'cross-profile-{scale}-{cap}.jsonl'
        run(python,root/'perf/measure.py',data,*options,'--runs','5' if scale=='practice' else '3','--warmup','1','--preload','--output-parent',area,'--log',path,extra=extra)
        rows=[x for x in map(json.loads,path.read_text().splitlines()) if x['round']>=0]
        stats=[]
        for p in options:
            vals=[x['wall_s'] for x in rows if x['binary']==str(p)]
            stats.append(f'{p.name} {statistics.median(vals):.4f} s [{min(vals):.4f},{max(vals):.4f}]')
        note('Cross-profile '+scale+' capability '+str(cap),', '.join(stats)+'. All complete tables matched the scored baseline.')
