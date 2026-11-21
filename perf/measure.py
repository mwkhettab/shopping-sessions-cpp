"""Rotate fresh processes, report wall time and resource use, keep raw observations."""
import argparse
import ctypes
import json
import mmap
import os
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("input", type=Path)
p.add_argument("binaries", nargs="+", type=Path)
p.add_argument("--runs", type=int, default=5)
p.add_argument("--warmup", type=int, default=1)
p.add_argument("--output-parent", type=Path, default=None)
p.add_argument("--log", type=Path, required=True)
p.add_argument("--profile", action="store_true")
p.add_argument("--residency", action="store_true", help="sample mapped CSV page residency before and after each run")
cache = p.add_mutually_exclusive_group()
cache.add_argument("--preload", action="store_true", help="sequentially read input outside each timed interval")
cache.add_argument("--evict", action="store_true", help="ask Linux to drop the input files' clean cache pages before each sample")
args = p.parse_args()
if args.runs < 1 or args.warmup < 0:
    p.error("--runs must be positive and --warmup must be nonnegative")
files = sorted(args.input.rglob("*.csv")) if args.residency or args.preload or args.evict else []

def resident_fraction():
    mincore = ctypes.CDLL(None, use_errno=True).mincore
    mincore.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
    present = total = 0
    for path in files:
        size = path.stat().st_size
        if not size:
            continue
        pages = (size + mmap.PAGESIZE - 1) // mmap.PAGESIZE
        vec = (ctypes.c_ubyte * pages)()
        with path.open("rb") as fd, mmap.mmap(fd.fileno(), 0, access=mmap.ACCESS_COPY) as mapping:
            address = ctypes.addressof(ctypes.c_char.from_buffer(mapping))
            if mincore(address, size, vec):
                raise OSError(ctypes.get_errno(), f"mincore on {path}")
        present += sum(bool(value & 1) for value in vec)
        total += pages
    return present / total if total else 1

env = dict(os.environ, NCPU=os.environ.get("NCPU", "8"))
if args.profile:
    env["PROFILE"] = "1"
else:
    env.pop("PROFILE", None)
binaries = [b.resolve() for b in args.binaries]
times = {b: [] for b in binaries}
with args.log.open("x") as log:
    for i in range(-args.warmup, args.runs):
        offset = i % len(binaries)
        for binary in binaries[offset:] + binaries[:offset]:
            if args.preload:
                block = bytearray(1024 * 1024)
                for path in files:
                    with path.open("rb", buffering=0) as fd:
                        while fd.readinto(block):
                            pass
            if args.evict:
                for path in files:
                    with path.open("rb", buffering=0) as fd:
                        os.posix_fadvise(fd.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
            before = resident_fraction() if args.residency else None
            with tempfile.TemporaryDirectory(dir=args.output_parent) as out:
                with tempfile.TemporaryFile(mode="w+") as err:
                    start = time.monotonic_ns()
                    with subprocess.Popen([binary, args.input, out], stderr=err, env=env) as child:
                        _, status, u = os.wait4(child.pid, 0)
                        wall = (time.monotonic_ns() - start) * 1e-9
                        child.returncode = os.waitstatus_to_exitcode(status)
                    err.seek(0)
                    detail = err.read()
                    if child.returncode:
                        raise SystemExit(f"{binary} exited {child.returncode}: {detail}")
                record = dict(binary=str(binary), input=str(args.input), round=i, wall_s=wall,
                              user_s=u.ru_utime, sys_s=u.ru_stime, minor=u.ru_minflt,
                              major=u.ru_majflt, inblock=u.ru_inblock, outblock=u.ru_oublock,
                              peak_mib=u.ru_maxrss / (1048576 if platform.system() == "Darwin" else 1024),
                              voluntary=u.ru_nvcsw, involuntary=u.ru_nivcsw, profile=detail)
                if args.residency:
                    record.update(resident_before=before, resident_after=resident_fraction())
                record["output_bytes"] = sum(path.stat().st_size for path in Path(out).rglob("*.parquet"))
                print(json.dumps(record), file=log, flush=True)
                print(f"{i:2} {binary.name:16} {wall:7.3f}s  cpu {u.ru_utime+u.ru_stime:6.2f}s"
                      f"  rss {record['peak_mib']:6.0f} MiB  major {u.ru_majflt}", flush=True)
                if i >= 0:
                    times[binary].append(wall)
for b, series in times.items():
    print(f"{b.name}: median {statistics.median(series):.3f}s  range {min(series):.3f}–{max(series):.3f}s")
