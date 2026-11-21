"""Cross-shard people, including linked aliases whose only data are clicks or discarded revisions."""
from pathlib import Path
import json, os, random, subprocess, sys, tempfile
import pyarrow.parquet as pq
from random_rules import oracle,clock,parsed,day,gap,reach,user_cols,camp_cols,event_cols,click_cols

def dataset(root,wide):
    rng=random.Random(608538+wide)
    lo=parsed('2026-10-01T00:00:00Z')
    hi=lo+(180 if wide else 14)*day
    products={'p1':'A','p2':'B'};campaigns={'c_2':'web','c_9':'mail'}
    (root/'manifest.json').write_text(json.dumps({'window_start':clock(lo),'window_end':clock(hi)}))
    (root/'products.csv').write_text('product_id,category\np1,A\np2,B\n')
    (root/'campaigns.csv').write_text('campaign_id,channel\nc_2,web\nc_9,mail\n')
    edges=[];events=[];clicks=[]
    for component in range(513):
        n=1537 if component==0 else 2+component%15
        names=[f'u{component:04x}{index:04x}' if index%7 else f'long alias {component}-{index} α' for index in range(n)]
        ghost='!'+str(component);names.append(ghost)
        rng.shuffle(names)
        edges.extend(zip(names,names[1:]))
        if component%7==0:edges.extend([(ghost,ghost),(names[0],names[-1])])
        aliases=[name for name in names if name!=ghost]
        for i,name in enumerate(aliases):
            start=lo+(component%4)*day+(i%21)*gap+(i%4)
            if component%5!=1 and i%4!=2:
                eid='e'+str(component)+'-'+str(i)
                amount=(i%37)+1
                events.append((eid,name,str(start),clock(start),'purchase' if i%3==0 else 'view','p'+str(1+i%2),str(amount)))
                if i%19==0:
                    # Earlier copy owned by a different alias and syntactically valid, with bad event time.
                    events.append((eid,aliases[(i+1)%n],'bad',clock(start-1000),'cart','p1',''))
            if (component%5!=2 and i%4) or component%5==1:
                stamp=start-(reach if i%3==0 else 0)
                clicks.append((f'k{component}:{i}',name,'c_2' if i%2 else 'c_9',str(stamp)))
        if component%13==0:
            events.append((f'Unlinked-{component}',f'solo-{component}',str(lo),clock(lo),'view','p1',''))
        if component%11==0:
            clicks.append((f'click solo-{component}',f'no-events-{component}','c_9',str(lo)))
    for sub,header,rows in [('events',event_cols,events),('clicks',click_cols,clicks),('links','user_id_a,user_id_b',edges)]:
        rng.shuffle(rows);d=root/sub;d.mkdir()
        for part in range(17):
            with (d/(str(part)+'.csv')).open('w') as out:
                out.write(header+'\n')
                for row in rows[part::17]:
                    out.write(','.join('"'+x+'"' if rng.randrange(4)==0 else x for x in row)+'\n')
    return oracle(root,lo,hi,products,campaigns)

def verify(binaries):
    for wide in (False,True):
        with tempfile.TemporaryDirectory() as tmp:
            data=Path(tmp)
            expected=dataset(data,wide)
            for binary in binaries:
                for workers in (1,8):
                    out=data/(Path(binary).name+str(workers))
                    subprocess.run([binary,data,out],check=True,env=os.environ|{'NCPU':str(workers),'POLARS_MAX_THREADS':'2'})
                    for tab,cols,ref in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),expected):
                        rows=[tuple(row[c] for c in cols) for f in (out/tab).glob('*.parquet') for row in pq.read_table(f,columns=cols).to_pylist()]
                        assert sorted(rows)==ref,(binary,workers,wide,tab,len(rows),len(ref))
    print('513 mixed components, one 1,537-alias chain, click-only groups and conflicting cross-alias revisions matched all scalar cells at 1 and 8 workers in both layouts.',flush=True)

if __name__=='__main__':verify([Path(f).resolve() for f in sys.argv[1:]])
