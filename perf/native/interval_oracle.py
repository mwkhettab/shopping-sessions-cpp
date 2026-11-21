"""Exact temporal reductions across assist-mask words, day edges and bridges."""
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path
import pyarrow.parquet as pq
from random_rules import clock,parsed,day,gap,reach,oracle,user_cols,camp_cols,event_cols,click_cols

def make(path,wide):
    first=parsed('2028-02-25T07:00:00Z')
    last=first+(70 if wide else 16)*day
    products={'p0':'z','p1':'a','p2':'💡'}
    campaigns={'cam'+str(i):'channel '+str(i%5) for i in range(97)}
    files={'events':[],'clicks':[],'links':[]}
    for kind in files:(path/kind).mkdir()
    (path/'manifest.json').write_text(json.dumps(dict(window_start=clock(first),window_end=clock(last))))
    for n,header,items in [('products','product_id,category',products),('campaigns','campaign_id,channel',campaigns)]:
        (path/(n+'.csv')).write_text(header+'\n'+''.join(k+','+v+'\n' for k,v in items.items()))
    for g,count in enumerate((1,2,3,4,8,31,32,63,64,65,127,128,129,340)):
        u='u'+str(g);alias='alias '+str(g);ghost='!'+str(g)
        files['links'].extend([(u,alias),(ghost,alias),(u,u)])
        for i in range(count):
            t=first+g*101+i*(2*gap+10003)
            amounts=(t,t+gap,t+gap+3) if not i%5 else (t,t+3,t+999)
            for j,ts in enumerate(amounts):
                key='e'+str(g)+'_'+str(i)+'_'+str(j)
                owner=u if j%2==0 else alias
                files['events'].append((key,owner,str(ts),clock(ts), 'purchase', 'p'+str(j%3), str((j+1)*103)))
                if j==1 and i%7==0:
                    files['events'].append((key,'unrelated',str(ts),clock(ts+10000),'purchase','p1','50000'))
                if j==2 and i%11==0:
                    files['events'].append((key,owner,'bad',clock(ts-10000),'cart','p1',''))
            for j,ct in enumerate((t,t,t-1,t+1,t-reach,t-reach-1,t-reach+1,t-gap)):
                c='cam'+str((i*7+j)%97)
                key=('!','A','k','é')[j%4]+f'{g}.{i}.{j}'
                row=(key,alias if j%2 else u,c,str(ct))
                files['clicks'].append(row)
                if not j%3:files['clicks'].append(row)
    r=random.Random(7781+wide)
    heads={'events':event_cols,'clicks':click_cols,'links':'user_id_a,user_id_b'}
    for kind,rows in files.items():
        r.shuffle(rows)
        for n in range(5):
            data=[''.join([','.join(('"'+f+'"') if r.randrange(4)==0 else f for f in row),'\n']) for row in rows[n::5]]
            (path/kind/(str(n)+'.csv')).write_text(heads[kind]+'\n'+''.join(data))
    return oracle(path,first,last,products,campaigns)

def main(binaries):
    for case in range(2):
        with tempfile.TemporaryDirectory() as tmp:
            data=Path(tmp);refs=make(data,case)
            for i,binary in enumerate(binaries):
                out=data/('output-'+str(i))
                subprocess.run([binary,data,out],check=True,env=os.environ|{'NCPU':'8'})
                for name,cols,expected in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),refs):
                    rows=[tuple(row[c] for c in cols) for f in (out/name).glob('*.parquet') for row in pq.read_table(f,columns=cols).to_pylist()]
                    assert sorted(rows)==expected,(binary,case,name,len(rows),len(expected))
    print('Both multiword, session-bridge and inclusive-boundary populations match the scalar interpreter.',flush=True)
if __name__=='__main__':main(sys.argv[1:])
