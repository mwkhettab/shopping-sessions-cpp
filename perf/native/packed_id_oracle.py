"""Compare large same-ID shards and stored clock exceptions to the independent rules interpreter."""
import json,os,random,subprocess,sys,tempfile
from pathlib import Path
import pyarrow.parquet as pq
from random_rules import parsed,clock,oracle,day,user_cols,camp_cols,event_cols,click_cols

def main(binaries):
    lo=parsed('2026-03-01T00:00:00Z');hi=lo+3*day
    with tempfile.TemporaryDirectory() as temp:
        data=Path(temp)
        for sub in ('events','clicks','links'):(data/sub).mkdir()
        (data/'manifest.json').write_text(json.dumps({'window_start':clock(lo),'window_end':clock(hi)}))
        (data/'products.csv').write_text('product_id,category\np1,blue\n')
        (data/'campaigns.csv').write_text('campaign_id,channel\nc_1,email\n')
        (data/'clicks/values.csv').write_text(click_cols+'\nk-base,u-base,c_1,'+str(lo)+'\n')
        (data/'links/g.csv').write_text('user_id_a,user_id_b\nuAA00B,leading_ALIAS\nu-base,u000\n')
        # One shard has more than 131071 revisions even at one worker.
        # All arrival times per ID are distinct. The minimum is near the end.
        file=data/'events/values.csv'
        with file.open('w') as out:
            out.write(event_cols+'\n')
            for i in range(135100):
                out.write(f'e33333333,uAA00B,{lo+3300},{clock(lo+(i+1)*1000)},purchase,p1,23\n')
            for i in range(133):
                key=('eA\0 /' if i%2 else 'eGü')+str(i)
                out.write(f'"{key}",u-base,{lo+4000},{clock(lo+10000)},view,p1,\n')
                out.write(f'{key},p-other,bad,{clock(lo-7000000000)},view,p1,\n')
            out.write(f'e33333333,u-base,{lo+3000},{clock(lo-20000000000)},purchase,p1,75\n')
            out.write(f'eAA00001,u000,{lo+3001},{clock(lo)},purchase,p1,28\n')
        ref=oracle(data,lo,hi,{'p1':'blue'},{'c_1':'email'})
        for binary in binaries:
            for workers in (1,8):
                out=data/(Path(binary).name+str(workers))
                subprocess.run([binary,data,out],check=True,env=os.environ|{'NCPU':str(workers)})
                for table,cols,expected in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),ref):
                    rows=[tuple(row[c] for c in cols) for f in (out/table).glob('*.parquet') for row in pq.read_table(f,columns=cols).to_pylist()]
                    assert sorted(rows)==expected,(binary,workers,table)
    print('135100 revisions in one shard, external clocks, byte-string IDs and mixed losing users matched all scalar outputs at one and eight workers.',flush=True)
if __name__=='__main__':main([Path(x).resolve() for x in sys.argv[1:]])
