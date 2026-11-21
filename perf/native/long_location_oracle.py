"""Exercise both sides of the real 32-bit compact revision locator limit."""
import datetime
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import pyarrow.parquet as pq


def verify(area,generator,binaries,note,count=(1<<23)+65):
    with tempfile.TemporaryDirectory(dir=area) as temp:
        root=Path(temp)
        for folder in ('events','clicks','links'):(root/folder).mkdir()
        (root/'manifest.json').write_text(json.dumps({'window_start':'2040-01-01T00:00:00Z','window_end':'2040-01-03T00:00:00Z'}))
        (root/'products.csv').write_text('product_id,category\np,hardware\n')
        (root/'campaigns.csv').write_text('campaign_id,channel\n')
        (root/'links/a.csv').write_text('user_id_a,user_id_b\n')
        (root/'clicks/a.csv').write_text('click_id,user_id,campaign_id,ts\n')
        csv=root/'events/a.csv';subprocess.run([generator,csv,str(count)],check=True)
        with csv.open('a') as stream:
            for i in range(count-64,count):
                kind=('bad','purchase','purchase','purchase')[i%4]
                owner='u00000001' if i%4<2 else 'u00000002'
                price=(1,1,11,17)[i%4]
                arrival='2040-01-01T00:00:15Z' if i%4==2 else '2040-01-01T00:00:00Z'
                print(f'e{i:08x},{owner},2208988800000,{arrival},{kind},p,{price}',file=stream)
        date=datetime.date(2040,1,1)
        for binary in binaries:
            out=root/('out-'+Path(binary).name)
            subprocess.run([binary,root,out],check=True,env=os.environ|{'NCPU':'1'})
            def rows(table,cols):
                return sorted(tuple(v[k] for k in cols.split()) for part in (out/table).glob('*.parquet') for v in pq.read_table(part).to_pylist())
            assert rows('user_daily','user_id day sessions events revenue_cents top_category')==[('u00000001',date,1,count-32,16,'hardware'),('u00000002',date,1,16,272,'hardware')]
            assert rows('campaign_daily','campaign_id channel day sessions purchasing_sessions revenue_cents assisted_sessions')==[('organic','organic',date,2,2,288,0)]
            note('Skewed revision locations '+Path(binary).name, f'{count:,} valid original events on one raw alias at one worker, followed by 64 competing revisions. All user and campaign values matched.')


if __name__=='__main__':
    folder,gen,n,*paths=sys.argv[1:]
    verify(Path(folder),gen,paths,print,int(n))
