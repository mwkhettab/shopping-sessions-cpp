"""Exercise revisions whose earlier and later copies enter different physical stores."""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import pyarrow.parquet as pq
from random_rules import clock, parsed, oracle, day, user_cols, camp_cols, event_cols, click_cols


def verify(binaries):
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp)
        for table in ('events','clicks','links'):(root/table).mkdir()
        start=parsed('2040-01-05T00:00:00Z');stop=start+4*day
        (root/'manifest.json').write_text('{"window_start":"'+clock(start)+'","window_end":"'+clock(stop)+'"}')
        (root/'products.csv').write_text('product_id,category\np1,a\np2,b\n')
        (root/'campaigns.csv').write_text('campaign_id,channel\nc_3,y\n')
        (root/'links/a.csv').write_text('user_id_a,user_id_b\nu00000001,!ghost\n')
        (root/'clicks/a.csv').write_text(click_cols+'\nk1,u00000001,c_3,'+clock(start)+'\n')
        for part in range(54):
            with (root/'events'/f'{part:03d}.csv').open('w') as out:
                print(event_cols,file=out)
                for i in range(850):
                    v=(i*179+part*73)%1087
                    owner=f'u{(v+part)%129:08x}'
                    instant=start+(v%163)*1_800_001
                    arrival=clock(start+(v%47)*1000+part*1000)
                    if part==4 and v%17==0:arrival='1964-02-29T00:01:03Z'
                    if part==36 and v%11==0:arrival='2180-03-01T12:01:04Z'
                    kind='view' if part%3 else 'purchase'
                    if (v+part)%13==0:kind='invalid'
                    eid='e'+f'{v:08x}' if v%7 else 'opaque event '+str(v)
                    print(','.join((eid,owner,str(instant),arrival,kind,'p'+str(1+v%2),str(1+v))),file=out)
        answers=oracle(root,start,stop,{'p1':'a','p2':'b'},{'c_3':'y'})
        for binary in binaries:
            for threads in (1,8):
                out=root/('output-'+Path(binary).name+'-'+str(threads))
                got=subprocess.run([binary,root,out],check=True,capture_output=True,text=True,env=os.environ|{'NCPU':str(threads),'SHOP_LAYOUT_TRACE':'1'})
                m=re.search(r'SHOP_REVISION_LAYOUT (\d+) (\d+)',got.stderr)
                if not m:raise AssertionError('missing revision layout totals')
                row,column=map(int,m.groups());name=Path(binary).name
                if 'revforcea' in name:assert row and not column
                elif 'revforcec' in name:assert column and not row
                else:assert row and column,(row,column)
                for table,fields,expected in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),answers):
                    values=[tuple(record[f] for f in fields) for path in (out/table).glob('*.parquet') for record in pq.read_table(path).to_pylist()]
                    if sorted(values)!=expected:raise AssertionError((name,threads,table))
                print(f'{name} at {threads} workers matched the independent tables with {row} row-layout and {column} column-layout revisions.',flush=True)


if __name__=='__main__':verify(sys.argv[1:])
