"""Write small deterministic workloads used only by the compiler's profile pass."""
import sys
from pathlib import Path

def generate(folder, wide):
    for table in ("events", "clicks", "links"):
        (folder / table).mkdir(parents=True, exist_ok=True)
    (folder / "manifest.json").write_text(
        '{"window_start":"2023-01-01T00:00:00Z","window_end":"'
        + ("2023-06-30" if wide else "2023-01-15") + 'T00:00:00Z"}'
    )
    def save(table, header, rows):
        with (folder / table).open("w") as out:
            print(header, file=out)
            for i, row in enumerate(rows):
                fields=(f'"{value}"' if (i*3+j*7) % 13 < 3 else str(value) for j,value in enumerate(row))
                print(",".join(fields), file=out)
    categories=300 if wide else 16
    save("products.csv","product_id,category",((f"p{i}",f"category{i%categories}") for i in range(2048)))
    save("campaigns.csv","campaign_id,channel",((f"c_{i}",f"channel{i%6}") for i in range(128)))
    def user(i):
        if i % 19 == 0: return f"long visitor {i}"
        return f"u{i*1315423911 & 0xffffffff:08x}"
    def clock(second):
        day, secs=divmod(second,86400)
        hour, secs=divmod(secs,3600)
        minute, sec=divmod(secs,60)
        return f"2023-01-{day+1:02d}T{hour:02d}:{minute:02d}:{sec:02d}"
    def instant(i,sec,iso):
        fmt=i%7
        if fmt in (0,1,2): return iso+"Z"
        if fmt in (3,4): return str(1672531200000+1000*sec+i%1000)
        return iso+("+00:00" if fmt==5 else "-02:00")
    n=84000 if wide else 240000
    def events():
        for i in range(n):
            # Integer mixing varies arrival order independently of timestamp.
            x=(i*2654435761)&0xffffffff
            sec=x%1166400
            iso=clock(sec)
            uid=user(x%(160 if x%5==0 else 17000))
            eid=f"e{x:08x}" if i%17 else f"event {i}"
            product=f"p{x%2111}"
            kind=("purchase","cart","view","view","cart")[i%5]
            row=(eid,uid,instant(i,sec,iso),iso+"Z",kind,product,str(x%1000000+1) if kind=="purchase" else "")
            yield row
            if i%11==0: yield row
            if i%59==0: yield (eid,uid,"invalid","2023-02-01T00:00:00Z","purchase",product,"?")
    save("events/part.csv","event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents",events())
    def clicks():
        for i in range(n//4):
            x=i*2246822519&0xffffffff
            sec=x%1166400
            yield (f"k{x:08x}",user(x%17000),f"c_{x%131}",instant(i,sec,clock(sec)))
    save("clicks/part.csv","click_id,user_id,campaign_id,ts",clicks())
    save("links/part.csv","user_id_a,user_id_b",((user(i),user(i+1)) for i in range(0,17000,3)))

def generate_indexed(folder):
    import random
    from datetime import datetime, timezone
    rng=random.Random(30277)
    for table in ('events','clicks','links'):
        (folder/table).mkdir(parents=True,exist_ok=True)
    (folder/'manifest.json').write_text('{"window_start":"2022-06-01T00:00:00Z","window_end":"2022-06-11T00:00:00Z"}')
    (folder/'products.csv').write_text('product_id,category\np1,food\np2,tools\n')
    (folder/'campaigns.csv').write_text('campaign_id,channel\nc_0,store\nc_1,mail\n')
    salts=[rng.getrandbits(40) for _ in range(3)]
    def ident(prefix,index,salt):
        mask=2**40-1
        v=(index*0x9e3779b97f4b^salt)&mask
        v=((v^(v>>17))*0xd6e8feb86659)&mask
        result=''
        while True:
            v,digit=divmod(v,36)
            result='0123456789abcdefghijklmnopqrstuvwxyz'[digit]+result
            if not v:return prefix+result
    def save(sub,header,rows):
        rng.shuffle(rows)
        with (folder/sub/'part.csv').open('w') as output:
            print(header,file=output)
            for row in rows:
                print(','.join('"'+v+'"' if rng.randrange(5)==0 else v for v in row),file=output)
    base=1654041600
    name=lambda n:ident('u',n,salts[0])
    names=[[name(512*g+a) for a in range(3)] for g in range(600)]
    links=[(u[0],u[1]) for u in names]+[(u[1],u[2]) for u in names[::3]]
    links.extend([(names[10][0],names[11][0]),(names[18][2],'other-name')])
    rows=[];clicks=[]
    for i in range(52000):
        g=0 if i<5000 else 1+((i-5000)//80)%599
        u=names[g][rng.randrange(3)]
        sec=base+(i%1000)*603
        iso=datetime.fromtimestamp(sec,timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
        ts=str(sec*1000+i%500) if i%3==1 else (iso[:-1]+'+00:00' if i%7==0 else iso)
        line=(ident('e',i,salts[1]),u,ts,iso,'purchase' if i%17==3 else ('view' if i%2 else 'cart'),'p1',str(i%991+1))
        rows.append(line)
        if i%23==0:rows.append(line)
        if i%4==0:clicks.append((ident('k',i,salts[2]),u,'c_'+str(int(i%3==0)),ts))
    save('links','user_id_a,user_id_b',links)
    save('events','event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents',rows)
    save('clicks','click_id,user_id,campaign_id,ts',clicks)

if __name__ == "__main__":
    dest=Path(sys.argv[1])
    generate(dest/"short",False)
    generate(dest/"wide",True)
    generate_indexed(dest/"indexed")
