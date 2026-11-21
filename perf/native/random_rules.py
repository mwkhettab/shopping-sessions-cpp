"""Differential check with a small independent interpreter for the contest rules."""
import collections
import datetime as dt
import json
import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path
import pyarrow.parquet as pq

origin=dt.datetime(1970,1,1,tzinfo=dt.timezone.utc)
day=86400000
reach=7*day
gap=1800000
event_cols='event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents'
click_cols='click_id,user_id,campaign_id,ts'
user_cols='user_id day sessions events revenue_cents top_category'.split()
camp_cols='campaign_id channel day sessions purchasing_sessions revenue_cents assisted_sessions'.split()
def clock(ms,offset=0):
    moment=(origin+dt.timedelta(milliseconds=ms)).astimezone(dt.timezone(dt.timedelta(minutes=offset)))
    return moment.strftime('%Y-%m-%dT%H:%M:%S')+(moment.strftime('%z')[:3]+':'+moment.strftime('%z')[3:] if offset else 'Z')

def parsed(s):
    if re.fullmatch('[0-9]+',s): return int(s)
    if not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:Z|[+-][0-9]{2}:[0-9]{2})',s): return None
    try:
        if s[-1]!='Z' and (int(s[-5:-3])>23 or int(s[-2:])>59):return None
        delta=dt.datetime.fromisoformat(s.replace('Z','+00:00'))-origin
    except (OverflowError,ValueError): return None
    return delta.days*day+delta.seconds*1000

def fields(line,n):
    result=[]
    for field in line.split(','):
        f=field[1:-1] if len(field)>=2 and field[0]==field[-1]=='"' else field
        if '"' in f:return None
        result.append(f)
    return result if len(result)==n else None

def read(data,stem,n):
    for path in sorted((data/stem).glob('*.csv')):
        for line in path.read_bytes().decode().split('\n')[1:]:
            r=fields(line,n)
            if r:yield r

def oracle(data,lo,hi,products,campaigns):
    graph=collections.defaultdict(set)
    for a,b in read(data,'links',2):
        if a and b:graph[a].add(b);graph[b].add(a)
    person={}
    for a in graph:
        if a in person:continue
        visited={a};todo=[a]
        while todo:
            v=todo.pop()
            for other in graph[v]-visited:visited.add(other);todo.append(other)
        name=min(visited,key=str.encode)
        for v in visited:person[v]=name
    versions={}
    for eid,u,t,arr,kind,prod,price in read(data,'events',7):
        if not eid:continue
        ingest=parsed(arr)
        if eid not in versions or ingest<versions[eid][0]:
            versions[eid]=(ingest,u,t,kind,prod,price)
    events=collections.defaultdict(list)
    for ingest,u,t,kind,prod,price in versions.values():
        ts=parsed(t)
        if not u or not prod or ts is None or not lo<=ts<hi or ingest-ts>day or kind not in ('purchase','cart','view'):continue
        price=(int(price) if re.fullmatch('[0-9]+',price) else -1) if kind=='purchase' else 0
        if kind=='purchase' and not 1<=price<=10000000:continue
        events[person.get(u,u)].append((ts,price,products.get(prod,'unknown')))
    clicks=collections.defaultdict(set)
    for k,u,c,t in read(data,'clicks',4):
        ts=parsed(t)
        if k and u and c in campaigns and ts is not None and lo-reach<=ts<hi:
            clicks[person.get(u,u)].add((ts,k,c))
    users={};totals=collections.defaultdict(lambda:[0,0,0,0])
    for u,ev in events.items():
        sessions=[]
        for e in sorted(ev):
            if not sessions or e[0]-sessions[-1][-1][0]>gap:sessions.append([])
            sessions[-1].append(e)
        for session in sessions:
            start=session[0][0];date=(origin+dt.timedelta(milliseconds=start)).date()
            vals=users.setdefault((u,date),[0,0,0,collections.Counter()])
            vals[0]+=1;vals[1]+=len(session);rev=sum(e[1] for e in session);vals[2]+=rev
            for _,p,c in session:
                if p:vals[3][c]+=p
            eligible=[k for k in clicks[u] if start-reach<=k[0]<=start]
            winner=min(eligible,key=lambda k:(-k[0],k[1].encode()))[2] if eligible else 'organic'
            agg=totals[winner,date];agg[0]+=1;agg[1]+=bool(rev);agg[2]+=rev
            for camp in {x[2] for x in eligible}:totals[camp,date][3]+=1
    us=[(*key,*v[:3],min(v[3],key=lambda x:(-v[3][x],x.encode())) if v[3] else None) for key,v in users.items()]
    cs=[(c,campaigns.get(c,'organic'),d,*v) for (c,d),v in totals.items()]
    return [sorted(us),sorted(cs)]

def generate(data,seed):
    rng=random.Random(seed)
    lo=parsed('1969-12-29T00:00:00Z') if seed%3==0 else parsed('2023-03-22T10:00:00Z')
    hi=lo+(240 if seed%4==0 else 17)*day
    products={f'p{i}':f'category{i}' for i in range(260 if seed%4==0 else 7)}
    campaigns={'c_a':'one','c_B':'two','c_0':'ads','c_00':'zero','?β':'text'}
    for n in ('events','clicks','links'):(data/n).mkdir(parents=True)
    (data/'manifest.json').write_text(json.dumps(dict(window_start=clock(lo),window_end=clock(hi))))
    for f,header,items in [('products','product_id,category',products),('campaigns','campaign_id,channel',campaigns)]:
        (data/(f+'.csv')).write_text(header+'\n'+''.join(k+','+v+'\n' for k,v in items.items()))
    names=['u1','u10000000','u3','u6','u00001','u4\0x','Ü','!ghost','a long anonymous alias',' U','uA','uZ',' U ','u00','ui','u5']
    def encode(row):
        return ','.join('"'+str(x)+'"' if rng.random()<0.35 else str(x) for x in row)
    def save(stem,header,rows):
        rng.shuffle(rows)
        for i in range(5):
            b=(header+'\n'+'\n'.join(rows[i::5])+('\n' if i%2==0 else '')).encode()
            (data/stem/(str(i)+'.csv')).write_bytes(b)
        (data/stem/'empty.csv').write_text(header+'\n')
    edges=[(names[0],names[6]),(names[7],names[6]),(names[0],names[0]),(names[5],names[8]),(names[10],names[11]),(names[11],names[12])]
    edges+= [(rng.choice(names),rng.choice(names)) for _ in range(seed%6)]
    save('links','user_id_a,user_id_b',[encode(r) for r in edges+edges[:3]]+['"invalid,x','a,b,c'])
    def wire(ms):
        if ms>=0 and (ms%1000 or rng.randrange(3)==0):return str(ms)
        return clock(ms,rng.choice([0,0,-720,60,330,900]))
    times=[lo+rng.randrange(hi-lo) for _ in range(75)]
    times+= [lo-1,lo,hi,hi-1,lo+day,lo+gap,lo+gap+1,lo+2*gap+1,lo+reach,lo+reach+1]
    ev=[];cl=[]
    for i in range(1450):
        u=rng.choice(names[:5]*2+names+[''])
        eid=rng.choice(['e','E longé','e\0','012'])+str(i)
        ms=rng.choice(times);ingest=(ms//1000*1000)+rng.choice([-1000,0,0,0,day,day+1000])
        row=(eid,u,wire(ms) if rng.randrange(37) else '2023-02-29T12:00:00Z',clock(ingest),rng.choice(['view','cart','purchase','purchase','unknown']),rng.choice(['p0','p1','p2','missing','',f'p{len(products)-1}']),rng.choice(['','12','１２','0','010','10000001','10000000','4']))
        ev.append(encode(row))
        if i%7==0:ev.append(encode(row))
        if i%11==0:ev.append(encode((eid,rng.choice(names),wire(ms),clock(parsed('1800-01-01T00:00:00Z')+1000*i),rng.choice(['view','bad']),'p1','')))
        if i%29==0:ev.extend([encode(row)+',x','"bad'+encode(row)])
    for i in range(731):
        ms=rng.choice(times)+rng.choice([0,0,-reach,-reach-1,1]);u=rng.choice(names)
        k=rng.choice(['k','kA','!k','β','k\0'])+str(i)
        row=(k,u,rng.choice(list(campaigns)+['unknown']),wire(ms))
        cl.append(encode(row))
        if i%5==0:cl.append(encode(row))
        if i%31==0:cl.append(encode(row)+'\r')
    save('events',event_cols,ev);save('clicks',click_cols,cl)
    return oracle(data,lo,hi,products,campaigns)


def run_seeds(binary):
    for seed in range(24):
        with tempfile.TemporaryDirectory() as tmp:
            data=Path(tmp)
            reference=generate(data,seed)
            out=data/'output'
            subprocess.run([binary,data,out],check=True,env=os.environ|{'NCPU':str([1,4,8][seed%3])})
            for table,cols,expected in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),reference):
                rows=[tuple(row[c] for c in cols) for path in (out/table).glob('*.parquet') for row in pq.read_table(path,columns=cols).to_pylist()]
                if sorted(rows)!=expected: raise AssertionError((binary,seed,table,len(rows),len(expected)))
    print('24 independent whole-pipeline differential seeds passed:',binary)
if __name__=='__main__':
    run_seeds(Path(sys.argv[1]).resolve())
