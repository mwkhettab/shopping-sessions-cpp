"""Exact byte comparisons on colliding event keys and prefix-related click IDs."""
from pathlib import Path
import json
import os
import random
import subprocess
import sys
import tempfile
import pyarrow.parquet as pq
from random_rules import parsed,clock,day,gap,reach,oracle,user_cols,camp_cols,event_cols,click_cols

def colliding():
    ids=[];i=0
    while len(ids)<160:
        raw=format(i,'08x');i+=1;v=int.from_bytes(raw.encode(),'little')
        v^=v>>27;v=(v*0x9e3779b97f4a7c15)&((1<<63)-1);v^=v>>31
        if v&4095==0:ids.append('e'+raw)
    return ids

def population(d):
    rng=random.Random(846015);lo=parsed('2036-02-28T00:00:00Z');hi=lo+19*day
    prod={'p1':'A','p2':'B'};camps={f'c_{i}':'group'+str(i%3) for i in range(8)}
    (d/'manifest.json').write_text(json.dumps({'window_start':clock(lo),'window_end':clock(hi)}))
    for table,header,entries in [('products','product_id,category',prod),('campaigns','campaign_id,channel',camps)]:
        (d/(table+'.csv')).write_text(header+'\n'+''.join(a+','+b+'\n' for a,b in entries.items()))
    ev=[];cl=[];edges=[]
    def csv(row):return ','.join('"'+s+'"' if rng.randrange(2) else s for s in row)
    for n,e in enumerate(colliding()+['long alternate e'+str(k) for k in range(34)]):
        who='u'+format(n,'08x');alias='ü '+str(n)
        edges.append(csv([who,alias]))
        t=lo+(n%12)*day+17000;owner=alias if n%5==0 else who
        earliest=[e,owner,str(t),'1911-01-01T00:00:00Z' if n%7==0 else clock(t), 'cart' if n%3==0 else 'purchase','' if n%11==0 else 'p1','0' if n%17==0 else '3']
        ev.extend([csv(earliest),csv(earliest),csv([e,'wrong user',str(t),'2190-01-01T00:00:00Z','purchase','p2','9999'])])
        if n%4==0:ev.append(csv([e,'wrong user','invalid','1900-01-01T00:00:00Z','view','p1','']))
        for z,delta in enumerate([0,gap+1,2*gap+2,reach,reach+1]):
            ev.append(csv([f'extra-{n}-{z}',owner,str(t+delta),clock(t+delta),'view','p2','']))
        pre='k'+format(n,'05x')
        options=[pre,pre+'\x00',pre+' a',pre+'  ',pre+'\r',pre+'\x7f',pre+'é',pre+'A',pre+'AA',pre+'AA\x00',pre+'Aβ',pre+' '+'v'*300]
        for j in range(6):
            text=options[(n+j)%len(options)]
            clk=[text,owner,f'c_{(n+j)%8}',str(t-1000)]
            cl.extend([csv(clk),csv(clk)])
        cl.append(csv(['next '+str(n),who,'c_5',str(t+gap)]))
        cl.append(csv(['outside '+str(n),who,'c_3',str(t-reach-1)]))
    for name,header,lines in [('events',event_cols,ev),('clicks',click_cols,cl),('links','user_id_a,user_id_b',edges)]:
        (d/name).mkdir();rng.shuffle(lines)
        for j in range(13):
            (d/name/(str(j)+'.csv')).write_bytes((header+'\n'+'\n'.join(lines[j::13])+('\n' if j%2 else '')).encode())
    return oracle(d,lo,hi,prod,camps)

def main(bins):
    with tempfile.TemporaryDirectory() as tmp:
        d=Path(tmp);expected=population(d)
        for i,exe in enumerate(bins):
            for cpu in ('1','8'):
                out=d/('output'+str(i)+'-'+cpu);subprocess.run([exe,d,out],env=os.environ|{'NCPU':cpu},check=True)
                for table,cols,ref in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),expected):
                    actual=[tuple(row[c] for c in cols) for p in (out/table).glob('*.parquet') for row in pq.read_table(p,columns=cols).to_pylist()]
                    if sorted(actual)!=ref:raise AssertionError((table,exe,cpu,len(actual),len(ref)))
    print('Forced event hash collisions, full-width earliest revisions, embedded NULs and bytewise click ties matched at one and eight workers.')
if __name__=='__main__':main(sys.argv[1:])
