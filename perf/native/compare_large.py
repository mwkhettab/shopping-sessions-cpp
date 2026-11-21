"""Exact table comparison with memory bounded by one UTC day of two outputs."""
from pathlib import Path
import sys
import polars as pl
import os
root = Path(os.environ['RUNNER_TEMP'])/'shopping-trials'
sys.path.insert(0,str(root/"starter-pyspark/scripts"))
from run_fixtures import SCHEMAS

def scan(directory, table, schema):
    paths=sorted((directory/table).glob("*.parquet"))
    assert paths, directory/table
    for file in paths:
        actual = pl.read_parquet_schema(file)
        assert set(actual)==set(schema), file
        for c,typ in schema.items():
            assert (actual[c]==typ or (typ==pl.Int128 and actual[c].is_integer())
                    or (typ==pl.String and actual[c].base_type() in (pl.String, pl.Categorical, pl.Enum))), (file,c,actual[c])
    return pl.scan_parquet(paths).select([pl.col(c).cast(t) for c,t in schema.items()])

left,right=map(Path,sys.argv[1:])
for table,schema in SCHEMAS.items():
    a,b=[scan(d,table,schema) for d in (left,right)]
    keys=[k for k in ("user_id","campaign_id","day") if k in schema]
    days=set(a.select(pl.col("day").unique()).collect()["day"]) | set(b.select(pl.col("day").unique()).collect()["day"])
    n=0
    for day in days:
        predicate=pl.col("day").is_null() if day is None else pl.col("day")==day
        x,y=(data.filter(predicate).sort(keys).collect() for data in (a,b))
        assert x.height==x.n_unique(subset=keys) and y.height==y.n_unique(subset=keys), (table,day,"duplicate keys")
        assert x.equals(y,null_equal=True), (table,day,"rows differ",x.height,y.height)
        n+=x.height
    print(f"{table}: {n} identical rows, zero duplicate keys",flush=True)
print("MATCH",flush=True)
