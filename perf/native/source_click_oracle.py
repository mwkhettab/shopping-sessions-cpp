"""Whole-pipeline click locators, mixed time encodings, and tie comparisons."""
import json,os,subprocess,sys,tempfile
from pathlib import Path
import pyarrow.parquet as pq
sys.path.insert(0,str(Path(__file__).resolve().parent))
from random_rules import parsed,clock,day,reach,oracle,user_cols,camp_cols,click_cols,event_cols

def build(path,mode):
    lo=parsed('2028-02-22T00:00:00Z');hi=lo+(65 if mode==2 else 40)*day
    products={'p':'category'}
    campaigns={'c_'+str(i):'channel'+str(i%3) for i in range(2055 if mode==1 else 19)}
    for name in ['events','clicks','links']:(path/name).mkdir()
    (path/'manifest.json').write_text(json.dumps({'window_start':clock(lo),'window_end':clock(hi)}))
    for name,cols,table in [('products','product_id,category',products),('campaigns','campaign_id,channel',campaigns)]:
        (path/(name+'.csv')).write_text(cols+'\n'+''.join(k+','+v+'\n' for k,v in table.items()))
    events=[];clicks=[[],[],[]]
    for i in range(39):
        start=lo+(i%39)*day+(1 if i%3==0 else 0)
        alias='u'+str(i%4);raw='alias'+str(i%4)
        events.append((f'e{i}',alias,str(start),clock(start+1000),'purchase','p',str(i+1)))
        instant=start-(i%3)
        prefix=('k'*65520 if i in (2,13) else 'prefix ') + str(i)
        for j,suffix in enumerate(('x','x\0','x\0a','x\r','xx','é','語')):
            when=instant if j!=6 else start-reach
            campaign='c_'+str((3*i+j)%len(campaigns))
            row=(prefix+suffix,raw,campaign,str(when))
            clicks[j%3].append(row)
            if j==1:clicks[(j+1)%3].append(row)
    (path/'links/a.csv').write_text('user_id_a,user_id_b\n'+''.join(f'u{i},alias{i}\n' for i in range(4)))
    (path/'events/a.csv').write_text(event_cols+'\n'+''.join(','.join(r)+'\n' for r in events))
    for i,file in enumerate(clicks):
        lines=[]
        for j,row in enumerate(file):
            lines.append(','.join('"'+text+'"' if (j+k+i)%2 else text for k,text in enumerate(row)))
        (path/'clicks'/f'{i}.csv').write_bytes((click_cols+'\n'+'\n'.join(lines)).encode())
    return oracle(path,lo,hi,products,campaigns)

for case in range(3):
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp);refs=build(p,case)
        for i,exe in enumerate(sys.argv[1:]):
            out=p/('out'+str(i));subprocess.run([exe,p,out],check=True,env=os.environ|{'NCPU':'8'})
            for table,cols,want in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),refs):
                got=[tuple(r[c] for c in cols) for part in (out/table).glob('*.parquet') for r in pq.read_table(part,columns=cols).to_pylist()]
                assert sorted(got)==want,(case,exe,table,len(got),len(want))
print('Whole-second and fractional ties, embedded NUL, cross-page IDs, missing final newline, unsupported catalog and time ranges matched all scalar outputs.')
