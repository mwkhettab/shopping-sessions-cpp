"""Create deterministic, independent CSV workloads and audit the training executable's real dispatch."""
import argparse
import datetime as dt
import functools
import hashlib
import json
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile


# Independent cases vary calendars, shape, cardinality, skew, row spelling and part count.
CASES = {
    "market": (0x3FA7D1, "2031-11-21", 13, 112000, 1401, 7),
    "depot": (0x6A0CBB, "2036-02-23", 9, 74000, 1691, 4),
    "scalar": (0x183AC3, "2034-04-17", 6, 14000, 381, 2),
    "opaque": (0x92DB11, "2033-08-19", 30, 11000, 327, 3),
}
DAY = 86400000
HEADERS = {"events": "event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents",
           "clicks": "click_id,user_id,campaign_id,ts", "links": "user_id_a,user_id_b"}


def spelling(prefix, i, salt):
    v = ((i*0x9e3779b97f4b) ^ salt) & ((1 << 40)-1)
    v = ((v ^ (v >> 17))*0xd6e8feb86659) & ((1 << 40)-1)
    alphabet = "0123456789abcdefghijklmnopqrstuvwxyz"
    result = ""
    while True:
        v, d = divmod(v,36)
        result = alphabet[d] + result
        if not v: return prefix + result


def iso(ms):
    return dt.datetime.fromtimestamp(ms//1000,dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def generate(dest, name, limit=None):
    seed, day, days, n, persons, parts = CASES[name]
    n = n if limit is None else min(n, limit)
    rng = random.Random(seed)
    ms = int(dt.datetime.fromisoformat(day).replace(tzinfo=dt.timezone.utc).timestamp())*1000
    opaque = name == "opaque"
    salts = [rng.getrandbits(40) for _ in range(3)]
    dest.mkdir(parents=True)
    (dest/"manifest.json").write_text(json.dumps({"window_start":iso(ms),"window_end":iso(ms+days*DAY)}))
    (dest/"products.csv").write_text("product_id,category\n"+"".join(f"p{i},group {i%19}\n" for i in range(3700)))
    (dest/"campaigns.csv").write_text("campaign_id,channel\n"+"".join(f"c_{i+7},channel {i%5}\n" for i in range(139)))
    @functools.lru_cache(None)
    def user(person, alias=0):
        return f" personne Ω/{person}-{alias} " if opaque else spelling("u", person*512+alias, salts[0])
    def clock(value, shape):
        if shape<4: return iso(value)
        if shape<8: return str(value)
        minutes = 345 if shape==8 else -210
        return iso(value+minutes*60000)[:-1] + ("+05:45" if minutes>0 else "-03:30")
    def emit(fields):
        return ",".join(f'"{x}"' if rng.randrange(9)<2 else str(x) for x in fields)
    def store_rows(kind, rows):
        rng.shuffle(rows)
        folder=dest/kind; folder.mkdir()
        for i in range(parts):
            (folder/f"part{i}.csv").write_text(HEADERS[kind]+"\n"+"\n".join(rows[i::parts])+"\n")
        (folder/"empty.csv").write_text(HEADERS[kind]+"\n")
    links = [emit((user(p),user(p,1))) for p in range(persons) if p%5]
    links += [emit((user(p,1),user(p,2))) for p in range(1,persons,7)]
    links += [emit((user(p),user(p+1))) for p in range(9,persons-1,89)]
    links += [emit((user(19),"shared Ω relay")),emit(("shared Ω relay",user(31,1)))]
    long_prefix = "transfer " + "Ω"*153 + name
    links += [emit((user(23),long_prefix+" north")),emit((long_prefix+" north",user(101,1))),
              emit((user(47),long_prefix+" south")),emit((long_prefix+" south",user(87,1)))]
    store_rows("links",links)
    events, clicks = [], []
    # The compact backend accepts a contiguous logical run per owner. Physical CSV
    # rows are shuffled afterwards. Opaque identifiers separately exercise the general path.
    timeline = [[] for _ in range(persons)]
    for j in range(n):
        person = 0 if j%17==0 else (j%11 if j%7==0 else rng.randrange(persons))
        session = rng.randrange(1+days*2)
        anchor = (person*26457319 + session*45102417) % (days*DAY-3600000)
        timeline[person].append(anchor+rng.randrange(2800000))
    rows = ((person,offset) for person,times in enumerate(timeline) for offset in sorted(times))
    for i,(p,offset) in enumerate(rows):
        alias = 0 if rng.randrange(4) else rng.randrange(1,4)
        uid = user(p,alias)
        ts = ms+offset
        ing = iso(ts+rng.randrange(118000))
        eid = "receipt Ω " + str(i) if opaque or i%891==512 else spelling("e",i,salts[1])
        kind = "purchase" if i%19==1 else ("cart" if i%6==0 else "view")
        product = "p"+str(rng.randrange(3741))
        value = str(1+rng.randrange(134501)) if kind=="purchase" else ""
        row = [eid,uid,clock(ts,rng.randrange(10)),ing,kind,product,value]
        if i%263==43: row[2]="bad time"
        if i%469==1: row[1]=""
        events.append(emit(row))
        if i%47==3:
            events.append(emit([eid,uid,clock(ts+rng.randrange(1000),i%10),iso(ts+221000),"purchase",product,"763"]))
        if i%103==0: events.append(emit(row))
        if i%4==0:
            cid = "ad|"+str(i) if opaque else spelling("k",i//4,salts[2])
            camp = "c_"+str((7+i%3) if i%5==0 else 7+rng.randrange(143))
            when = ts-rng.choice((0,1000,355000,604800000,604800001))
            clicks.append(emit([cid,uid,camp,clock(when,i%10)]))
            if i%116==0: clicks.append(emit([cid,uid,camp,clock(when,9)]))
    store_rows("events",events)
    store_rows("clicks",clicks)


def tables(output):
    result = {}
    for table in ("user_daily","campaign_daily"):
        paths = sorted((output/table).glob("*.parquet"))
        if not paths: raise ValueError(f"missing {table}")
        for file in paths:
            blob=file.read_bytes()
            if not (len(blob)>12 and blob[:4]==blob[-4:]==b"PAR1"):
                raise ValueError(f"invalid Parquet envelope in {file}")
            result[str(file.relative_to(output))]=hashlib.sha256(blob).hexdigest()
    return result


def run(exe, data, out_parent, workers, pgo, portable):
    cfg=dict(os.environ,NCPU=str(workers),XL_TIMING="1",V1ND_LOG="1",
             SHOP_PGO_FIELD_SCALAR=str(int(portable)),SHOP_PGO_CASE=data.name,XL_POOL="1")
    with tempfile.TemporaryDirectory(dir=out_parent) as out:
        proc=subprocess.run([exe,data,out],env=cfg,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=150)
        if proc.returncode: raise RuntimeError(proc.stdout[-4000:])
        compact = "[pipeline] events=" in proc.stdout and "[pipeline] fallback:" not in proc.stdout
        generic = "[pipeline] fallback:" in proc.stdout and "[v1nd]" in proc.stdout
        role = "compact" if compact else "generic" if generic else "unknown"
        expected = "generic" if data.name=="opaque" else "compact"
        if role!=expected: raise ValueError(f"{data.name}: expected {expected}, observed {role}\n{proc.stdout[-1200:]}")
        activity = {}
        if pgo:
            check=re.search(r"\[pgo\] exit hardware=(\d+) wide=(\d+) portable=(\d+) parked=(\d+)",proc.stdout)
            if check is None: raise ValueError(f"missing training exit gate: {proc.stdout[-1500:]}")
            hw,wide,scalar,parked=map(int,check.groups())
            if compact and not ((hw and not portable and wide>0 and scalar==0)
                                or ((portable or not hw) and wide==0 and scalar>0)):
                raise ValueError(f"unexpected decoder dispatch {check[0]}")
            if compact and parked < workers-1:
                raise ValueError(f"shipping worker pool did not reach its wait state: {check[0]}")
            activity={"field_hardware":bool(hw),"wide_field_buffers":wide,"portable_field_buffers":scalar,"parked_workers":parked}
        return {"path":role,"workers":workers,**activity,"outputs":tables(Path(out))}


def main():
    p=argparse.ArgumentParser()
    p.add_argument("action",choices=("generate","train","check","stock-smoke"))
    p.add_argument("area",type=Path)
    p.add_argument("--binary",type=Path)
    p.add_argument("--limit",type=int)
    args=p.parse_args()
    area=args.area.resolve();area.mkdir(parents=True,exist_ok=True)
    if args.action=="generate":
        for name in CASES: generate(area/name,name,args.limit)
        return
    exe=args.binary.resolve()
    pgo=args.action=="train"
    if pgo:
        sequence=[("market",8,False),("depot",4,False),("scalar",1,True),("opaque",1,False)]
    else:
        sequence=[("scalar",1,False),("opaque",1,False)]
        if args.action=="stock-smoke": sequence += [("market",3,False),("depot",3,False)]
    results={}
    for name,workers,portable in sequence:
        results[name]=run(exe,area/name,area,workers,pgo,portable)
        print(name,{k:v for k,v in results[name].items() if k!="outputs"},flush=True)
    if args.action=="check":
        previous=json.loads((area/"train.json").read_text())
        for name in ("scalar","opaque"):
            if previous[name]["outputs"]!=results[name]["outputs"]:
                raise ValueError(f"{name}: one-worker Parquet bytes differed between training and optimized builds")
    (area/(args.action+".json")).write_text(json.dumps(results,sort_keys=True,indent=2)+"\n")


if __name__=="__main__": main()
