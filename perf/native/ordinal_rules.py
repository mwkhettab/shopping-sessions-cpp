"""Exercise the direct event slots on shuffled and conflicting input populations."""
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

def spelling(prefix,value,salt):
    mask=2**40-1
    v=(value*0x9e3779b97f4b^salt)&mask
    v=((v^(v>>17))*0xd6e8feb86659)&mask
    digits='0123456789abcdefghijklmnopqrstuvwxyz';name=''
    while True:
        v,d=divmod(v,36);name=digits[d]+name
        if not v:return prefix+name

def populate(data,seed):
    from datetime import datetime,timezone
    rng=random.Random(seed)
    salts=[rng.randrange(1<<40) for _ in range(3)]
    user=lambda n:spelling('u',n,salts[0])
    event=lambda n:spelling('e',n,salts[1])
    click=lambda n:spelling('k',n,salts[2])
    ms=1803772800000
    def f1(t):return datetime.fromtimestamp(t//1000,timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    for k in ('events','clicks','links'):(data/k).mkdir(parents=True)
    (data/'manifest.json').write_text(json.dumps({'window_start':f1(ms),'window_end':f1(ms+9*86400000)}))
    (data/'products.csv').write_text('product_id,category\np0,A\np1,a\n')
    (data/'campaigns.csv').write_text('campaign_id,channel\nc_1,S\nc_2,T\n')
    def csv(row):return ','.join('"'+x+'"' if rng.randrange(4)==0 else x for x in row)
    links=[];events=[];clicks=[]
    for g in range(320):
        if g%3:links.append(csv([user(g*512),user(g*512+1)]))
        if g%7==2:links.append(csv([user(g*512+1),user(g*512+4)]))
    links+= [csv([user(512*3+1),' leading']),csv([' leading',user(512*7)]),
             csv([user(512*7),user(512*8+2)]),csv([user(512*8+2),'opaque長']),
             csv([user(2*512+30),user(3*512+2)]),csv(['opaque長','A only in links'])]
    for i in range(24000):
        group=0 if i<2500 else (i*41)%320
        u=user(group*512+(0 if i%7<5 else i%4))
        if i>2500 and i%113==0:u='opaque長'
        if i>2500 and i%271==0:u='unlinked '+str(i%10)
        if i>2500 and i%359==0:u=user(15*512+32)
        when=ms+(i*613 if i<2500 else rng.randrange(8*86400000))
        row=[event(i),u,str(when),f1(when),'purchase' if i%9==1 else 'view','p'+str(i%2),str(i%19+1)]
        events.append(csv(row))
        if i%37==0:
            late=row.copy();late[1]=user(12*512);late[3]=f1(when+19000);late[4]='purchase';late[6]='781'
            events+= [csv(late),csv(row)]
        if i%53==0:
            earlier=row.copy();earlier[2]='invalid';earlier[3]='1903-01-01T00:00:00Z';events.append(csv(earlier))
        if i%79==0:events.append(csv([event(i),'x','bad','1901-01-01T00:00:00Z','view','p0',''])+',extra')
        if i%11==0:
            c=[click(i),u,'c_1' if i%3 else 'c_2',str(when)]
            clicks.extend([csv(c),csv(c)])
    for e in ('e01','é other','e000','e0','mixed\0id',event(1<<36)):
        events.append(csv([e,user(7*512),str(ms+3600000),'1901-01-01T00:00:00Z','purchase','p0','15']))
        events.append(csv([e,'wrong',str(ms),f1(ms),'purchase','p1','492']))
    for label,value,header in [('events',events,'event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents'),
                               ('clicks',clicks,'click_id,user_id,campaign_id,ts'),('links',links,'user_id_a,user_id_b')]:
        rng.shuffle(value)
        for i in range(7):(data/label/(str(i)+'.csv')).write_text(header+'\n'+'\n'.join(value[i::7])+('\n' if i%2 else ''))

def main():
    import pyarrow.parquet as pq
    binary=str(Path(sys.argv[1]).resolve())
    general=str(Path(sys.argv[2]).resolve()) if len(sys.argv)>2 else binary
    for seed in (7933,27011,401273):
        with tempfile.TemporaryDirectory() as path:
            data=Path(path);populate(data,seed)
            def get(exe,out,**env):
                p=subprocess.run([exe,data,out],env=os.environ|env,check=True,capture_output=True,text=True)
                rows=[]
                for table in ('user_daily','campaign_daily'):
                    allrows=[]
                    for f in (out/table).glob('*.parquet'):
                        t=pq.read_table(f);allrows.extend(tuple(r.values()) for r in t.to_pylist())
                    rows.append(sorted(allrows))
                return p.stderr,rows
            _,wanted=get(general,data/'general',NCPU='3',SHOP_GENERAL_READER='1')
            for n in ('1','8'):
                trace,got=get(binary,data/('direct'+n),NCPU=n,SHOP_ORDINAL_AUDIT='1')
                assert 'ordinal engine ' in trace,trace
                assert got==wanted,(seed,n,[len(x) for x in got],[len(x) for x in wanted])
    print('Three independently salted shuffled ID layouts matched at one and eight workers, including split aliases, cross-group links and invalid earliest events.',flush=True)
if __name__=='__main__':main()
