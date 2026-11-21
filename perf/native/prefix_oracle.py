"""Independent full-pipeline comparison over all optional-quote combinations."""
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path
import pyarrow.parquet as pq
from random_rules import clock, parsed, oracle, day, gap, reach, user_cols, camp_cols, event_cols, click_cols

def make(data):
    start=parsed('2032-02-28T00:00:00Z')
    stop=start+5*day
    (data/'manifest.json').write_text(json.dumps({'window_start':clock(start),'window_end':clock(stop)}))
    prod={'p1':'a','p2':'space '};camps={'c_a':'1','c_b':'2'}
    for stem,header,values in [('products','product_id,category',prod),('campaigns','campaign_id,channel',camps)]:
        (data/(stem+'.csv')).write_text(header+'\n'+''.join(k+','+v+'\n' for k,v in values.items()))
    def fmt(row,mask):
        return ','.join('"'+s+'"' if (mask>>i)&1 else s for i,s in enumerate(row))
    ev=[];cl=[];ln=[]
    suffixes=['12345678','abcdefgh','  0<>@ %','abcdef\x00A','ABCDEFβ','1234567\x7f','1234567\x00','extra space','ré']
    for mask in range(128):
        for form in range(3):
            for variant in range(7):
                n=(mask*3+form)*7+variant
                owner='u'+suffixes[n%len(suffixes)]
                key='e'+format(n,'08x')
                ms=start+(n%79)*gap+1000*(n%3)
                stamp=(str(ms+int(variant==4)),clock(ms),clock(ms,330))[form]
                # Every combination gets valid and invalid earliest revisions.
                kind=('view','cart','view','purchase','NOOP','cart','view')[variant]
                price=('0','+5','１２','','100','27','5000')[(variant+form)%7]
                event=[key,owner,stamp,clock(ms),kind,('', 'p1','p2','p_other')[(variant+form+mask)%4],price]
                ev.append(fmt(event,mask))
                if variant==3 or variant==6:
                    later=event.copy();later[1]='uabcdef02';later[2]=str(ms);later[3]=clock(ms+5000)
                    later[4:]=['purchase','p2','381'];ev.append(fmt(later,mask^127))
                if variant==1:
                    earlier=event.copy();earlier[2]='2A320228T00:00:00Z';earlier[3]=clock(ms-10000)
                    ev.append(fmt(earlier,mask^11))
                if variant==2:
                    for pos in [0,2,5,len(fmt(event,mask))-2]:
                        wire=fmt(event,mask);ev.append(wire[:pos]+'",'+wire[pos:])
                    ev.append(fmt(event,mask)+',"')
                if not n%9:
                    for j,ct in enumerate([ms,ms-reach,ms-reach-1]):
                        cl.append(fmt([f'k{n:06x}{j:02x}',owner,['c_a','c_b'][j%2],str(ct)],mask))
                if not n%433:ln.append(fmt([owner,'!ghost '+str(n%2)],mask))
    rng=random.Random(41807)
    for stem,header,rows in [('events',event_cols,ev),('clicks',click_cols,cl),('links','user_id_a,user_id_b',ln)]:
        (data/stem).mkdir()
        rng.shuffle(rows)
        for i in range(5):
            (data/stem/(str(i)+'.csv')).write_bytes((header+'\n'+'\n'.join(rows[i::5])+('\n' if i%2 else '')).encode())
    return oracle(data,start,stop,prod,camps)

def main(binaries):
    with tempfile.TemporaryDirectory() as tmp:
        data=Path(tmp);refs=make(data)
        for i,binary in enumerate(binaries):
            out=data/('result'+str(i))
            subprocess.run([binary,data,out],check=True,env=os.environ|{'NCPU':'8'})
            for name,cols,ref in zip(('user_daily','campaign_daily'),(user_cols,camp_cols),refs):
                got=[tuple(row[c] for c in cols) for path in (out/name).glob('*.parquet') for row in pq.read_table(path,columns=cols).to_pylist()]
                if sorted(got)!=ref:raise AssertionError((binary,name,len(got),len(ref)))
    print('128 quote masks, three clock forms, invalid revisions, cross-user duplicates and malformed lines matched independent scalar results',flush=True)
if __name__=='__main__':main(sys.argv[1:])
