"""Check committed output after a verified single-run shortcut or a complete retry."""
import os,subprocess,sys,tempfile
from pathlib import Path
from ordinal_rules import populate
from ordinal_fixtures import tables
for seed in (7933,27011,401273):
    with tempfile.TemporaryDirectory() as day:
        p=Path(day);populate(p,seed);candidate=Path(sys.argv[1]).resolve();control=Path(sys.argv[2]).resolve()
        subprocess.run([control,p,p/'control'],env=os.environ|{'NCPU':'3','SHOP_GENERAL_READER':'1'},check=True)
        wanted=tables(p/'control')
        for threads in ('1','8'):
            out=p/'same-output';out.mkdir(exist_ok=True)
            q=subprocess.run([candidate,p,out],env=os.environ|{'NCPU':threads,'SHOP_ORDINAL_AUDIT':'1'},capture_output=True,text=True,check=True)
            assert 'ordinal retry:' in q.stderr,(seed,threads,q.stderr)
            assert not list(out.glob('.sessions-*')) and tables(out)==wanted,(seed,threads)
print('All three fragmented populations invoked the verified retry and matched complete tables at one and eight workers, including reuse of the destination.')
