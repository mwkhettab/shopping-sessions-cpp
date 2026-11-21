import datetime as dt
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import pyarrow as pa
import pyarrow.compute as pc
import pyarrow.parquet as pq

exe=Path(sys.argv[1]).resolve()
sizes=(0,1,7,8,15,16,17,63,64,65,4095,4096,4097,6143,6144,6145,10241,12288,13817)
def expected(case,i):
    mode=case%4
    valid=mode==1 or (mode==2 and i%17<8) or (mode==3 and i%257==0)
    paid=mode==1 or (mode==2 and i%17==7) or (mode==3 and i%1037>532)
    person=19+i//67;cat=i%32
    return ("客户_"+str(case)+"_"+("0"*(person%21))+str(person),
            dt.date(1970,1,1)+dt.timedelta(days=19000+i%67),
            1 if case%3==0 else 1+i%173,
            2147483000-i%123 if case%4==0 else i%153+1,
            2147483627-i%312 if paid else 0,
            ("分类_"+str(cat)+"q"*(cat%3*50)) if valid else None)

with tempfile.TemporaryDirectory() as temp:
    root=Path(temp)
    for name in ("part-99.parquet",".part-12.parquet.writing","_SUCCESS",".notes","_ignored.parquet",".part-x.parquet.writing"):
        (root/name).write_text("old")
    (root/"link.parquet").symlink_to(root/".notes")
    subprocess.run([exe,root,"interrupt"],check=True)
    assert (root/".part-0.parquet.writing").is_file() and not (root/"part-0.parquet").exists()
    subprocess.run([exe,root],check=True)
    for name in ("part-99.parquet",".part-12.parquet.writing",".part-0.parquet.writing"): assert not (root/name).exists()
    for name in ("_SUCCESS",".notes","_ignored.parquet",".part-x.parquet.writing","link.parquet"): assert (root/name).exists()
    f=pq.ParquetFile(root/"part-0.parquet")
    assert f.schema_arrow.names==["user_id","day","sessions","events","revenue_cents","top_category"]
    assert pa.types.is_date32(f.schema_arrow.field("day").type)
    assert f.schema_arrow.field("top_category").nullable
    for field in f.schema_arrow:
        if field.name!="top_category": assert not field.nullable
    groups=[];seen=set();absent_stats=0;nulls=0
    for case,count in enumerate(sizes):
        start=0
        while start<count:
            n=4096 if count-start>6144 else count-start
            group=len(groups);t=f.read_row_group(group); cols=[a.to_pylist() for a in t.columns]
            rows=list(zip(*cols));w=[expected(case,i) for i in range(start,start+n)]
            assert len(rows)==n and sorted(rows)==sorted(w),(case,group)
            for j in (1,2,3,4):
                numeric=[r[j] for r in w]
                assert pc.min_max(t.column(j)).as_py()=={"min":min(numeric),"max":max(numeric)}
            for x in rows:
                key=x[:2]; assert key not in seen;seen.add(key)
            nulls+=t.column(5).null_count
            absent_stats+=sum(f.metadata.row_group(group).column(k).statistics is None for k in range(6))
            groups.append(n);start+=n
    assert len(groups)==f.num_row_groups
    report={"rows":len(seen),"duplicates":0,"group_rows":groups,"null_categories":nulls,
            "tested_column_extrema_per_group":4,"column_chunks_without_optional_footer_statistics":absent_stats,
            "sparse_file_bytes":(root/"part-0.parquet").stat().st_size,"retry":"passed"}
    print(json.dumps(report,sort_keys=True))
    if len(sys.argv)>2: Path(sys.argv[2]).write_text(json.dumps(report,sort_keys=True,indent=2)+"\n")
