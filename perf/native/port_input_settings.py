"""Measure input policies on the same release ELF and verify their complete output."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


SETTINGS = {
    "release": {},
    "retain-mappings": {"XL_REL": "0"},
    "smaller-chunks": {"XL_CH": "2048"},
    "buffered-read": {"XL_RD": "256"},
}


def compare(args):
    native = args.tools / "perf/native"
    meter = args.tools / "perf/measure.py"
    args.output.mkdir(exist_ok=False, parents=True)
    cpus = sorted(os.sched_getaffinity(0))[:8]
    os.sched_setaffinity(0, cpus)
    env = dict(os.environ, NCPU=str(len(cpus)), POLARS_MAX_THREADS="2", ARROW_NUM_THREADS="2")
    for key in ("XL_RD", "XL_CH", "XL_REL", "V1ND_LOG", "XL_TIMING", "PROFILE"):
        env.pop(key, None)
    policies = list(SETTINGS)
    with tempfile.TemporaryDirectory(dir=args.output) as temporary:
        scratch = Path(temporary)
        reference = scratch / "reference"
        reference.mkdir()
        subprocess.run([args.oracle, args.input, reference], env=env, check=True)
        subprocess.run([sys.executable, native / "acceptance_reference.py", reference, "--scale", args.scale,
                        "--expected", native / "acceptance-references.json"], env=env, check=True)
        for name in policies:
            with tempfile.TemporaryDirectory(dir=scratch) as out:
                subprocess.run([args.exe, args.input, out], env=env | SETTINGS[name], check=True)
                subprocess.run([sys.executable, native / "compare_large.py", reference, out], env=env, check=True)
        results = []
        with (args.output / "samples.jsonl").open("x", buffering=1) as raw:
            for n in range(-1, args.rounds):
                offset = max(0, n) // 2 % len(policies)
                order = policies[offset:] + policies[:offset]
                if n >= 0 and n % 2:
                    order.reverse()
                for position, name in enumerate(order):
                    sample = scratch / f"{name}-{n}.jsonl"
                    subprocess.run([sys.executable, meter, args.input, args.exe, "--runs", "1", "--warmup", "0",
                                    "--log", sample, "--residency", "--preload", "--output-parent", scratch],
                                   env=env | SETTINGS[name], check=True)
                    row = json.loads(sample.read_text())
                    row.update(policy=name, round=n, position=position)
                    print(json.dumps(row), file=raw)
                    if n >= 0:
                        results.append(row)
    controls = {r["round"]: r["wall_s"] for r in results if r["policy"] == "release"}
    summary = {}
    for name in policies:
        rows = [r for r in results if r["policy"] == name]
        ratios = [r["wall_s"] / controls[r["round"]] for r in rows]
        summary[name] = {"median_s": statistics.median(r["wall_s"] for r in rows),
                         "paired_ratio": statistics.median(ratios), "faster_pairs": sum(v < 1 for v in ratios),
                         "rounds": len(rows), "peak_mib": max(r["peak_mib"] for r in rows),
                         "min_residency": min(r["resident_before"] for r in rows),
                         "median_user_s": statistics.median(r["user_s"] for r in rows),
                         "median_sys_s": statistics.median(r["sys_s"] for r in rows), "environment": SETTINGS[name]}
    output = {"scale": args.scale, "workers": len(cpus), "affinity": cpus, "measurements": summary}
    (args.output / "summary.json").write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps(output), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("exe", "oracle", "input", "output", "tools"):
        parser.add_argument("--" + flag, type=Path, required=True)
    parser.add_argument("--scale", choices=("20M", "60M"), required=True)
    parser.add_argument("--rounds", type=int, default=7)
    args = parser.parse_args()
    if args.rounds < 2:
        parser.error("at least two paired rounds are required")
    compare(args)
