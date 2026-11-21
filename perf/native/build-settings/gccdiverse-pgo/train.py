"""Write small deterministic workloads used only by the compiler's profile pass."""
import sys
from pathlib import Path

def generate(folder, wide, clustered=False):
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
    n=84000 if wide else (270000 if clustered else 240000)
    def events():
        for i in range(n):
            # Integer mixing varies arrival order independently of timestamp.
            x=(i*2654435761)&0xffffffff
            sec=x%1166400
            uid=user(x%(160 if x%5==0 else 17000))
            if clustered:
                owner=(x>>13)%((i%7+1)*1500)
                sequence=(x>>7)%12
                sec=(owner*9551+(i%4)*86400+sequence*270)%1166400
                uid=f"u{owner:x}" if owner%9 else f"end user {owner}"
            iso=clock(sec)
            eid=f"e{x:08x}" if i%17 else f"event {i}"
            if clustered and i%8:
                eid=f"e{x:x}" if i%3 else f"E{i:09d}"
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

if __name__ == "__main__":
    dest=Path(sys.argv[1])
    generate(dest/"short",False)
    generate(dest/"wide",True)
    generate(dest/"clustered",False,True)
