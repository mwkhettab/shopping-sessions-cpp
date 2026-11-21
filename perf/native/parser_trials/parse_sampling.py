"""Compare parse sampling with the shipping process and preserve raw counters, coverage, and paired overhead."""
import argparse
from collections import Counter
from functools import reduce
import hashlib
import json
import operator
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile


KINDS = ("event", "click", "link")
LEAVES = ("classify_ns", "scatter_ns", "fused_ns", "special_ns")


def summarize(raw):
    workers, groups, meta = {}, {kind: {} for kind in KINDS}, None
    for line in raw.splitlines():
        if not line.startswith("[parse-sample] "):
            continue
        _, role, *fields = line.split()
        values = {k: int(v) for k, v in (word.split("=", 1) for word in fields)}
        if role == "meta":
            assert meta is None
            meta = values
        else:
            target = workers if role == "worker" else groups[role]
            tid = values.pop("tid")
            assert tid not in target
            target[tid] = values
    if meta is None:
        raise ValueError("missing compact parser telemetry; check the ELF, data, and generic fallback diagnostics")
    assert set(workers) == set(range(meta["workers"]))
    assert all(set(group) == set(workers) for group in groups.values())
    totals, estimates, fingerprints, balance = {}, {}, {}, {}
    accounting = Counter()
    for tid, w in workers.items():
        assert w["end"] >= w["begin"] > 0
        balance[tid] = {"begin_ns": w["begin"], "end_ns": w["end"], "kind": {}}
        parts = Counter({k: w[k] for k in ("jobs_ns", "handoff_ns", "tail_ns")})
        for kind, group in groups.items():
            v = group[tid]
            assert v["sample_rows"] <= v["rows"] and v["sample_bytes"] <= v["bytes"]
            assert v["sample_chunks"] <= v["chunks"] and v["sample_body_ns"] <= v["body_ns"] <= v["chunk_ns"]
            assert sum(v[k] for k in LEAVES) <= v["sample_body_ns"]
            if kind != "link":
                assert v["sample_rows"] == v["packet_rows"] and v["special_rows"] <= v["sample_rows"]
            parts[kind + "_body_ns"] = v["body_ns"]
            parts["chunk_boundary_read_release_ns"] += v["chunk_ns"] - v["body_ns"]
            balance[tid]["kind"][kind] = {k: v[k] for k in ("chunks", "bytes", "rows", "sample_chunks", "sample_rows")}
        parts["setup_schedule_cleanup_ns"] = w["end"] - w["begin"] - sum(parts.values())
        assert min(parts.values()) >= 0
        accounting.update(parts)
        balance[tid]["exclusive_worker_ns"] = dict(parts)
    origin = min(w["begin"] for w in workers.values())
    for b in balance.values():
        b["begin_ns"] -= origin
        b["end_ns"] -= origin
    for kind, group in groups.items():
        totals[kind] = vals = dict(sum((Counter(v) for v in group.values()), Counter()))
        # Counter addition drops zero fields. Preserve them for stride zero and for rarely used packet paths.
        vals.update({key: vals.get(key, 0) for key in next(iter(group.values()))})
        vals["sampled_id_xor"] = reduce(operator.xor, (v["sampled_id_xor"] for v in group.values()), 0)
        if meta["stride"] == 0:
            assert vals["sample_chunks"] == 0
        else:
            assert vals["sample_chunks"] == (vals["chunks"] + meta["stride"] - 1) // meta["stride"]
        fingerprints[kind] = [vals["chunks"], vals["sample_chunks"], vals["sampled_id_xor"], vals["sample_rows"]]
        if kind != "link" and vals["weighted_body_ns"]:
            # Each stratum contributes by its actual width, including the shorter last group.
            parts = {key: vals["weighted_" + key] for key in LEAVES}
            parts["field_reader_and_callback_overhead_ns"] = vals["weighted_body_ns"] - sum(parts.values())
            assert min(parts.values()) >= 0
            estimates[kind] = {k: v / vals["weighted_body_ns"] * vals["body_ns"] for k, v in parts.items()}
    return {"config": meta, "worker_span_ns": max(w["end"] for w in workers.values()) - origin,
            "sum_worker_ns": sum(accounting.values()), "exclusive_worker_ns": dict(accounting),
            "worker_balance": balance, "coverage_and_inclusive_ns": totals,
            "estimated_exclusive_body_worker_ns": estimates, "selected_chunks": fingerprints}


def report(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def sha(path):
    with path.open("rb") as stream:
        h = hashlib.sha256()
        for data in iter(lambda: stream.read(1 << 20), b""):
            h.update(data)
    return h.hexdigest()


def validate(args, env):
    checker = args.tools/"perf/native/acceptance_reference.py"
    known = ["--scale", args.scale, "--expected", checker.with_name("acceptance-references.json")] if args.scale else []
    fingerprints = {}
    for label, executable, stride in (("shipping", args.shipping, 0), ("coarse", args.probe, 0), ("all", args.probe, 1)):
        with tempfile.TemporaryDirectory(dir=args.output) as td:
            row = subprocess.run([executable, args.input, td], capture_output=True, text=True,
                                 env=dict(env, SHOP_PARSE_STRIDE=str(stride), SHOP_PARSE_PHASE="0"), check=True)
            if label != "shipping":
                summary = summarize(row.stderr)
                assert summary["config"]["stride"] == stride
                report(args.output / f"verify-{label}-profile.json", summary)
            (args.output / f"verify-{label}.log").write_text(row.stderr)
            fp = subprocess.check_output([sys.executable, checker, td, *known], text=True, env=env)
            fingerprints[label] = json.loads(fp)
    if not fingerprints["shipping"] == fingerprints["coarse"] == fingerprints["all"]:
        raise ValueError("shipping, coarse and fully sampled binaries differ on the two logical tables")
    return fingerprints


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for item in ("shipping", "probe", "input", "tools", "output"):
        p.add_argument("--" + item, type=Path, required=True)
    p.add_argument("--scale", choices=("20M", "60M"), help="enforce the existing seed-23 or seed-47 independent digests")
    p.add_argument("--cache", choices=("warm", "cold"), default="warm")
    p.add_argument("--rounds", type=int, default=4)
    p.add_argument("--strides", type=int, nargs="+", default=[16, 64])
    args = p.parse_args()
    if args.rounds < 1 or any(s < 1 for s in args.strides) or len(set(args.strides)) != len(args.strides):
        p.error("rounds and distinct sample strides must be positive")
    for arg in ("shipping", "probe", "input", "tools", "output"):
        setattr(args, arg, getattr(args, arg).resolve())
    args.output.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, XL_TIMING="1", POLARS_MAX_THREADS=os.getenv("POLARS_MAX_THREADS", "2"))
    fingerprints = validate(args, env)
    report(args.output/"verified.json", fingerprints)
    measure = args.tools/"perf/measure.py"
    roles = [("shipping", args.shipping, 0), ("coarse", args.probe, 0), *[(f"s{s}", args.probe, s) for s in args.strides]]
    records = {}
    count = len(roles)
    base_order = [0, *((i+1)//2 if i%2 else count-i//2 for i in range(1, count))]
    for r in range(args.rounds):
        # Each role occupies every position in a complete block. Reverse alternate blocks
        # to balance adjacent predecessors with an odd number of roles too.
        order = [roles[(index+r)%count] for index in base_order]
        if (r//count)%2: order.reverse()
        records[r] = {}
        for label, executable, stride in order:
            path = args.output/f"r{r}-{label}.jsonl"
            cmd = [sys.executable, measure, args.input, executable, "--profile", "--residency",
                   "--preload" if args.cache == "warm" else "--evict", "--runs", "1", "--warmup", "0",
                   "--output-parent", args.output, "--log", path]
            subprocess.run(cmd, env=dict(env, SHOP_PARSE_STRIDE=str(stride), SHOP_PARSE_PHASE=str(r)), check=True)
            rec = json.loads(path.read_text())
            lap = re.search(r"(?:^|\s)parse=([.\d]+)", rec["profile"])
            if not lap:
                raise ValueError(f"{label} has no compact parser stage timing")
            before = rec["resident_before"]
            entry = {"wall_s": rec["wall_s"], "parse_ms": float(lap[1]), "resident_before": before,
                     "cache_confirmed": before >= 0.90 if args.cache == "warm" else before <= 0.10}
            if label != "shipping":
                entry["probe"] = summarize(rec["profile"])
                assert entry["probe"]["config"]["stride"] == stride and entry["probe"]["config"]["phase"] == r
            records[r][label] = entry
    ratios = {}
    for label, _, _ in roles[1:]:
        ratios[label] = {}
        for metric in ("parse_ms", "wall_s"):
            pairs = [v[label][metric] / v["shipping"][metric] for v in records.values() if v["shipping"][metric]]
            confirmed = [v[label][metric] / v["shipping"][metric] for v in records.values()
                         if v["shipping"][metric] and v[label]["cache_confirmed"] and v["shipping"]["cache_confirmed"]]
            ratios[label][metric] = {"round_ratios": pairs, "median_ratio": statistics.median(pairs) if pairs else None,
                                     "cache_confirmed_pairs": len(confirmed),
                                     "cache_confirmed_median_ratio": statistics.median(confirmed) if confirmed else None}
    report(args.output/"summary.json", {"hashes": {"shipping": sha(args.shipping), "probe": sha(args.probe)},
           "cache": args.cache, "affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
           "overhead_versus_shipping": ratios, "rounds": records})
    print(json.dumps(ratios, sort_keys=True))


if __name__ == "__main__":
    main()
