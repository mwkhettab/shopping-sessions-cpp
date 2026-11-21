"""Run the public rule cases inside independently salted, direct-index inputs."""
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile

import pyarrow.parquet as pq

from ordinal_rules import spelling


def fields(line, count):
    raw = line.split(b',')
    if len(raw) != count:
        return None
    result = []
    for item in raw:
        quoted = len(item) >= 2 and item[:1] == item[-1:] == b'"'
        value = item[1:-1] if quoted else item
        if b'"' in value:
            return None
        result.append((value, quoted))
    return result


def tables(out):
    result = []
    for table in ('user_daily', 'campaign_daily'):
        rows = []
        for part in (out / table).glob('*.parquet'):
            rows.extend(tuple(r.values()) for r in pq.read_table(part).to_pylist())
        result.append(sorted(rows, key=repr))
    return result


def enrich(src, data, seed):
    rng = random.Random(seed)
    us, es = rng.randrange(1 << 40), rng.randrange(1 << 40)
    def person(ordinal): return spelling('u', ordinal, us).encode()
    def event(ordinal): return spelling('e', ordinal, es).encode()
    people = set()
    ids = set()
    schema = {'events': (7, {0: ids, 1: people}), 'links': (2, {0: people, 1: people}), 'clicks': (4, {1: people})}
    for f in ('manifest.json', 'products.csv', 'campaigns.csv'):
        shutil.copy2(src/f, data/f)
    for folder, (count, keys) in schema.items():
        for path in (src/folder).glob('*.csv'):
            for line in path.read_bytes().split(b'\n')[1:]:
                parsed = fields(line, count)
                if parsed:
                    for i, values in keys.items():
                        if parsed[i][0]: values.add(parsed[i][0])
    # Keep the rank of transformed user spellings, including bytewise tie-breaks.
    user_text = sorted(person((64+i//4)*512+(i%4)) for i in range(len(people)))
    user_map = dict(zip(sorted(people), user_text))
    event_map = {v:event(22000+i) for i,v in enumerate(sorted(ids))}
    for folder,(count,keys) in schema.items():
        (data/folder).mkdir()
        for path in sorted((src/folder).glob('*.csv')):
            lines = path.read_bytes().split(b'\n')
            for j in range(1,len(lines)):
                parsed = fields(lines[j],count)
                if parsed is None: continue
                for i, values in keys.items():
                    value,q = parsed[i]
                    if value: parsed[i] = ((user_map if values is people else event_map)[value],q)
                lines[j]=b','.join(b'"'+v+b'"' if q else v for v,q in parsed)
            (data/folder/path.name).write_bytes(b'\n'.join(lines))
    origin=json.loads((data/'manifest.json').read_text())['window_start'].encode()
    # Exercise the full ingestion path even when a fixture has only one row.
    # Malformed/losing fixture records retain their original payloads and times.
    path=data/'events/independent-cohort.csv'
    with path.open('wb') as out:
        out.write(b'event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents\n')
        for i in range(4300):
            group=0 if i<900 else 1+(i-900)//128
            row=[event(i),person(group*512),origin,origin,b'view',b'extra product',b'']
            out.write(b','.join(row)+b'\n')


def main():
    candidate, oracle, fixtures = map(lambda arg: Path(arg).resolve(), sys.argv[1:4])
    active=[]; general=[]
    for seed,fixture in enumerate(sorted(fixtures.glob('*/input')),19):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);data=base/'data';data.mkdir();enrich(fixture,data,seed)
            subprocess.run([oracle,data,base/'expected'],check=True,env=os.environ|{'SHOP_GENERAL_READER':'1','NCPU':'3'})
            p=subprocess.run([candidate,data,base/'actual'],check=True,env=os.environ|{'SHOP_ORDINAL_AUDIT':'1','NCPU':'8'},capture_output=True,text=True)
            assert tables(base/'expected')==tables(base/'actual'),fixture.parent.name
            (active if 'ordinal engine ' in p.stderr else general).append(fixture.parent.name)
    assert len(active)+len(general)==36 and len(active)>=30,(active,general)
    print(f'All 36 independently salted fixture embeddings matched both tables. Direct-index engine active in {len(active)} cases. General cases: {general}.',flush=True)

if __name__=='__main__':main()
