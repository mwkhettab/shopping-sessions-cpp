"""Check the emitted GCC counters and record whether the wide worker functions actually ran."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

root=Path(sys.argv[1]); cases=json.loads((root/"data/train.json").read_text())
gcda=sorted((root/"profile").rglob("*.gcda"))
units=("main","k_free","k_free_x86","sys_global_alloc")
units_data={}
for unit in units:
    matching=[f for f in gcda if f.name.endswith("-"+unit+".gcda")]
    if len(matching)!=1: raise SystemExit(f"{unit}: expected one gcda, found {matching}")
    units_data[unit]=matching[0]
notes,=list(Path("build").glob("*-k_free_x86.gcno"))
ids={}
for line in subprocess.check_output(["gcov-dump","-l",notes],text=True).splitlines():
    hit=re.search(r"FUNCTION ident=(\d+),.*?, [`']([^']+)'",line)
    if hit and any(name in hit[2] for name in ("events_x86","clicks_x86")): ids[hit[1]]=hit[2]
if not ids: raise SystemExit("gcov notes did not identify the two wide kernels")
counts={"events_x86":0,"clicks_x86":0}
current=None; arcs=False
for line in subprocess.check_output(["gcov-dump","-l",units_data["k_free_x86"]],text=True).splitlines():
    hit=re.search(r"FUNCTION ident=(\d+)",line)
    if hit:
        current=ids.get(hit[1]); arcs=False
    elif "COUNTERS" in line:
        arcs="COUNTERS arcs" in line
    elif current and arcs:
        hit=re.search(r":\s+\d+:\s+((?:\d+\s*)+)$",line)
        if hit:
            for key in counts:
                if key in current: counts[key]+=sum(map(int,hit[1].split()))
wide=any(row["wide_field_buffers"] for row in cases.values())
if any(bool(c)!=wide for c in counts.values()):
    raise SystemExit(f"wide reader calls and GCC arc counters disagree: {wide}, {counts}")
proof={"gcc":subprocess.check_output(["g++","--version"],text=True).splitlines()[0],
       "wide_kernel_arc_sums":counts, "wide_reader_observed":wide,
       "profile_files":{u:{"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
                        for u,p in units_data.items()}, "training":cases}
(root/"proof.json").write_text(json.dumps(proof,sort_keys=True,indent=2)+"\n")
print("Actual profiles",proof["profile_files"],"wide arc sums",counts,flush=True)
