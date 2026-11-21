"""Validate release binaries and profile alternating warm/cold processes on native Linux."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "perf/native"
BASELINE = "4db76beed31640ddf4b414ed19deba5ebc660b81"
CASCADE = "avx2,bmi1,bmi2,abm,avx512f,avx512bw,avx512cd,avx512vl,avx512dq,avx512_vnni"
RUNTIME_BUILDS = {
    "control": (),
    "paired": ("PAIRED_CLICK_SORT",),
    "memory": ("CUSTOM_MEMORY",),
    "dictionary": ("DIRECT_DICT_LOOPS",),
    "paired-memory": ("PAIRED_CLICK_SORT", "CUSTOM_MEMORY"),
    "paired-memory-layout": ("PAIRED_CLICK_SORT", "CUSTOM_MEMORY", "INTEL_LAYOUT"),
    "paired-memory-v4": ("PAIRED_CLICK_SORT", "CUSTOM_MEMORY", "INTEL_LAYOUT", "V4_SIDECAR"),
    "complete": ("PAIRED_CLICK_SORT", "CUSTOM_MEMORY", "INTEL_LAYOUT", "V4_SIDECAR", "DIRECT_DICT_LOOPS"),
}


def store(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n")


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as src:
        for chunk in iter(lambda: src.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def exec_logged(command, log, env, cwd=None):
    with log.open("a", buffering=1) as out:
        print("$ " + shlex.join(map(str, command)), file=out, flush=True)
        started = time.monotonic()
        result = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=cwd)
        print(f"exit {result.returncode} after {time.monotonic()-started:.3f}s", file=out)
    if result.returncode:
        print(log.read_text(errors="replace")[-8000:], flush=True)
        raise subprocess.CalledProcessError(result.returncode, command)


def capture(cmd):
    return subprocess.check_output(cmd, text=True).strip()


def disk(path):
    stat = shutil.disk_usage(path)
    return {"total_bytes": stat.total, "used_bytes": stat.used, "available_bytes": stat.free}


def identity(folder):
    r = subprocess.run(["git", "-C", folder, "rev-parse", "HEAD"], text=True, capture_output=True)
    return r.stdout.strip() if r.returncode == 0 else None


def build_image(tag, folder, override, area, results, env, switches=()):
    image = "shopping-acceptance:" + tag
    options = [arg for name in switches for arg in ("--build-arg", name + "=1")]
    exec_logged(["docker", "build", "--platform", "linux/amd64", *options, "-t", image, folder],
                results / (tag + "-build.log"), env)
    info = json.loads(capture(["docker", "image", "inspect", image]))[0]
    if (info["Os"], info["Architecture"]) != ("linux", "amd64"):
        raise ValueError(f"{image}: expected linux/amd64, saw {info['Os']}/{info['Architecture']}")
    config = info["Config"]
    entry = override or ((config.get("Entrypoint") or []) + (config.get("Cmd") or []))[0]
    if not entry.startswith("/") or entry in ("/bin/sh", "/bin/bash"):
        raise ValueError(f"provide --{tag}-entry for the executable in {image}: {entry}")
    target = area / "accept-binaries" / tag
    target.mkdir(parents=True, exist_ok=True)
    exe = target / Path(entry).name
    cid = capture(["docker", "create", image])
    try:
        parent = Path(entry).parent.as_posix()
        # Some release images dispatch to an adjacent .opt. Copy its directory with it.
        src = cid + ":" + (parent + "/." if parent != "/" else entry)
        subprocess.run(["docker", "cp", src, target if parent != "/" else exe], check=True)
    finally:
        subprocess.run(["docker", "rm", cid], stdout=subprocess.DEVNULL, check=True)
    return exe, {"source_commit": identity(folder), "image": image, "image_id": info["Id"], "entry": entry,
                 "build_switches": list(switches), "executable": str(exe),
                 "hashes": {p.name: sha(p) for p in target.iterdir() if p.is_file()}}


def provided_binary(tag, source, area):
    source = source.resolve()
    target = area / "accept-binaries" / tag / source.name
    target.parent.mkdir(parents=True, exist_ok=True)
    binaries = [source]
    sidecar = source.with_name(source.name + ".opt")
    if sidecar.is_file():
        binaries.append(sidecar)
    for p in binaries:
        dest = target.with_name(p.name)
        shutil.copy2(p, dest)
        dest.chmod(0o755)
    return target, {"source_commit": None, "executable": str(target), "supplied_file": str(source),
                    "hashes": {p.name: sha(p) for p in binaries}}


def verify_development(tag, exe, image, area, results, env, candidate_source):
    kit = area / "starter-pyspark"
    exec_logged([sys.executable, candidate_source / "verify.py", exe, area/"dev",
                 "--starter", kit, "--answer", area/"dev-answer"], results/(tag + "-dev.log"), env)
    if image:
        log = results/(tag + "-image-dev.log")
        exec_logged([sys.executable, kit/"scripts/run_fixtures.py", kit/"fixtures", "--", image], log, env)
        with tempfile.TemporaryDirectory(dir=area) as out:
            exec_logged([image, area/"dev", out], log, env)
            exec_logged([sys.executable, kit/"scripts/compare.py", out, area/"dev-answer"], log, env)


def in_container(image, entry, meter, cpus, memory_mib, data, out):
    out.mkdir(parents=True, exist_ok=True)
    out.chmod(0o777)
    with tempfile.TemporaryDirectory(dir=out.parent) as scratch:
        os.chmod(scratch, 0o777)
        ncpu = len(cpus.split(","))
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "--network", "none", "--read-only",
               "--user", "1000:1000", "--cap-drop", "ALL", "--security-opt", "no-new-privileges",
               "--cpuset-cpus", cpus, "--cpus", str(ncpu), "--memory", str(memory_mib) + "m",
               "--memory-swap", str(memory_mib) + "m", "--tmpfs", "/tmp:rw,mode=1777,size=64m",
               "-v", str(data.resolve()) + ":/data:ro", "-v", str(out.resolve()) + ":/output",
               "-v", str(Path(scratch).resolve()) + ":/scratch", "-v", str(meter.resolve()) + ":/meter:ro",
               "-e", "HOME=/scratch", "-e", "DATA_DIR=/data", "-e", "OUTPUT_DIR=/output",
               "-e", "SCRATCH_DIR=/scratch", "-e", "NCPU=" + str(ncpu), "--entrypoint", "/meter", image, entry]
        try:
            return subprocess.run(cmd).returncode
        finally:
            # The image writes as UID 1000. Hosted runners can use a different UID,
            # in which case user-owned temporary-directory cleanup needs write access.
            for folder in (out, Path(scratch)):
                if any(not os.access(base, os.W_OK) for base, _, _ in os.walk(folder)):
                    subprocess.run(["sudo", "-n", "chmod", "-R", "a+rwX", "--", folder], check=True)


def launcher(item, exe, meter, cpus, memory):
    path = exe.parent / "image-run"
    args = [sys.executable, __file__, "container", item["image"], item["entry"], meter, cpus, str(memory)]
    path.write_text("#!/bin/sh\nexec " + shlex.join(map(str, args)) + ' "$@"\n')
    path.chmod(0o755)
    return path


def paired(scale, data, bins, area, results, env, warm_count, cold_count):
    rounds = results / (scale + "-pairs.jsonl")
    profiles = results / (scale + "-stages.jsonl")
    by_path = {str(p.resolve()): k for k, p in bins.items()}
    # Stage diagnostics are isolated from the timed samples.
    profenv = dict(env, PROFILE="1", XL_TIMING="1", V1ND_LOG="1")
    measured = [sys.executable, ROOT / "perf/measure.py", data]
    common = ["--warmup", "0", "--runs", "1", "--residency", "--output-parent", area]
    exec_logged([*measured, *bins.values(), *common, "--profile", "--preload", "--log", profiles],
                results / (scale + "-stages.log"), profenv)
    records = []
    with rounds.open("x") as pairs, tempfile.TemporaryDirectory(dir=area) as td:
        for r in range(-1, max(warm_count, cold_count)):
            modes = ("warm",) if r < 0 else (("warm", "cold") if r % 2 == 0 else ("cold", "warm"))
            for mode in modes:
                if r >= (warm_count if mode == "warm" else cold_count):
                    continue
                paths = list(bins.values())
                base = (max(0, r) // 2 + int(mode == "cold")) % len(paths)
                paths = paths[base:] + paths[:base]
                if r >= 0 and r % 2:
                    paths.reverse()
                if mode == "cold":
                    os.sync()
                f = Path(td) / f"{r}-{mode}.jsonl"
                flag = "--preload" if mode == "warm" else "--evict"
                exec_logged([*measured, *paths, *common, flag, "--log", f], results / (scale + "-pairs.log"), env)
                for order, line in enumerate(f.read_text().splitlines()):
                    rec = json.loads(line)
                    rec.update(label=by_path[rec["binary"]], round=r, cache=mode, order=order)
                    if mode == "cold":
                        rec["cold_confirmed"] = rec["resident_before"] <= 0.10
                    print(json.dumps(rec), file=pairs, flush=True)
                    records.append(rec)
    summary = {}
    for mode in ("warm", "cold"):
        for label in bins:
            selected = [r for r in records if r["round"] >= 0 and r["label"] == label and r["cache"] == mode]
            if not selected:
                continue
            metric = {"median_s": statistics.median(r["wall_s"] for r in selected),
                      "seconds": [r["wall_s"] for r in selected], "peak_mib": max(r["peak_mib"] for r in selected),
                      "major_faults": [r["major"] for r in selected],
                      "residency_before": [r["resident_before"] for r in selected]}
            if mode == "cold":
                metric["eviction_confirmed_all"] = all(r["cold_confirmed"] for r in selected)
            if label != "baseline":
                control = {r["round"]: r for r in records if r["label"] == "baseline" and r["cache"] == mode}
                deltas = [r["wall_s"] / control[r["round"]]["wall_s"] - 1 for r in selected]
                metric["paired_change_median"] = statistics.median(deltas)
                metric["faster_pairs"] = sum(d < 0 for d in deltas)
            metric["versus"] = {}
            for control_label in ("candidate", "peer", "control", "paired-memory", "paired-memory-layout", "paired-memory-v4"):
                if control_label not in bins or control_label == label:
                    continue
                control = {r["round"]: r["wall_s"] for r in records if r["label"] == control_label and r["cache"] == mode}
                ratios = [r["wall_s"] / control[r["round"]] for r in selected]
                metric["versus"][control_label] = {"median_ratio": statistics.median(ratios),
                                                  "faster_pairs": sum(ratio < 1 for ratio in ratios), "pairs": len(ratios)}
            summary[mode + "/" + label] = metric
    return summary


def generate_final(area, results, env):
    # Use the existing bounded generator experiment that makes identical shuffled
    # parts. Compare the small generation byte for byte before scaling it.
    gen = area / "generator"
    log = results / "seed47-generation.log"
    args = [gen / "target/release/generate", "--threads", "4"]
    exec_logged(["patch", "-p1", "-i", NATIVE / "generator-memory.patch"], log, env, cwd=gen)
    exec_logged(["cargo", "build", "--release", "--locked", "--offline"], log, env, cwd=gen)
    for seed, count, clicks, out in [(1, 1000000, 250000, "dev-check"), (47, 60000000, 15000000, "final")]:
        for part in range(3):
            exec_logged([*args, "--seed", str(seed), "--events", str(count), "--clicks", str(clicks),
                         "--output", area / out], log, dict(env, PART_CLASSES="3", PART_CLASS=str(part)))
        if seed == 1:
            exec_logged(["diff", "-qr", area / "dev", area / "dev-check"], log, env)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--area", type=Path, default=Path(os.environ.get("RUNNER_TEMP", "/tmp")) / "shopping-trials")
    p.add_argument("--candidate-source", type=Path, default=ROOT)
    p.add_argument("--baseline-source", type=Path, default=ROOT / ".acceptance/baseline")
    p.add_argument("--baseline-binary", type=Path)
    p.add_argument("--candidate-binary", type=Path)
    p.add_argument("--runtime-source", type=Path)
    p.add_argument("--runtime-builds", nargs="+", choices=RUNTIME_BUILDS, default=[])
    peer = p.add_mutually_exclusive_group()
    peer.add_argument("--peer-binary", type=Path)
    peer.add_argument("--peer-source", type=Path)
    p.add_argument("--peer-binary-sha256")
    p.add_argument("--peer-flags", default=CASCADE)
    for tag in ("candidate", "baseline", "peer"):
        p.add_argument("--" + tag + "-entry", help="absolute path of executable in the release Docker image")
    p.add_argument("--require-baseline-commit", default=BASELINE)
    p.add_argument("--scales", nargs="+", choices=("1M", "20M", "60M"), default=("1M", "20M", "60M"))
    p.add_argument("--practice", type=Path)
    p.add_argument("--final", type=Path)
    p.add_argument("--release-generated-practice", action="store_true",
                   help="reclaim this job's generated practice CSVs before generating seed 47")
    p.add_argument("--warm-20", type=int, default=7)
    p.add_argument("--warm-60", type=int, default=3)
    p.add_argument("--cold-runs", type=int, default=3)
    p.add_argument("--cpus", type=int, default=8)
    p.add_argument("--memory-mib", type=int, default=12288)
    p.add_argument("--skip-kernel-test", action="store_true", help="for exercising the harness with existing binaries")
    args = p.parse_args()
    if bool(args.runtime_source) != bool(args.runtime_builds) or len(set(args.runtime_builds)) != len(args.runtime_builds):
        p.error("supply --runtime-source and distinct --runtime-builds together")
    if min(args.cpus, args.warm_20, args.warm_60, args.cold_runs, args.memory_mib) < 1:
        p.error("CPU, memory and iteration values must be positive")
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        p.error("the acceptance experiment requires x86-64 Linux")
    area = args.area.resolve()
    results = area / "results/port-acceptance"
    results.mkdir(parents=True, exist_ok=True)
    if (results / "status.json").exists():
        p.error(f"a previous run already exists in {results}")
    raw_cpu = Path("/proc/cpuinfo").read_text()
    cpus = sorted(os.sched_getaffinity(0))[:args.cpus]
    os.sched_setaffinity(0, cpus)
    cpu_list = ",".join(map(str, cpus))
    info = {line.split(":", 1)[0].strip(): line.split(":", 1)[1].strip()
            for line in raw_cpu.split("\n\n", 1)[0].splitlines() if ":" in line}
    flags = set(info.get("flags", "").split())
    env = dict(os.environ, NCPU=str(len(cpus)), POLARS_MAX_THREADS=str(min(len(cpus), 2)),
               SHOPPING_TRIALS_DIR=str(area))
    for k in ("PROFILE", "XL_TIMING", "V1ND_LOG"):
        env.pop(k, None)
    hardware = {"model": info.get("model name"), "vendor": info.get("vendor_id"), "isa": sorted(flags),
                "affinity": cpus, "ncpu": len(cpus), "memory_mib": args.memory_mib,
                "meminfo": Path("/proc/meminfo").read_text(), "uname": list(platform.uname()),
                "disk_start": disk(area), "workflow_sha": os.environ.get("GITHUB_SHA")}
    store(results / "hardware.json", hardware)
    (results / "lscpu.txt").write_text(capture(["lscpu"]) + "\n")
    state = {"status": "in progress", "hardware": hardware["model"], "cpus": cpus, "verified": {}, "disks": {},
             "implementations": {}, "timings": {}}
    def mark(phase):
        state["phase"] = phase
        state["disks"][phase] = disk(area)
        store(results / "status.json", state)
        print(phase, state["disks"][phase], flush=True)

    bins, images = {}, {}
    try:
        mark("build")
        for tag in ("baseline", "candidate", "peer"):
            folder = getattr(args, tag + "_source")
            binary = getattr(args, tag + "_binary")
            if tag == "peer" and not (folder or binary):
                state["implementations"][tag] = {"tested": False, "reason": "no optional comparison input supplied"}
                continue
            if tag == "peer":
                missing = sorted(set(filter(None, args.peer_flags.split(","))) - flags)
                if missing:
                    state["implementations"][tag] = {"tested": False, "reason": "unsupported instruction set", "missing": missing}
                    continue
            if binary:
                exe, item = provided_binary(tag, binary, area)
                if tag == "peer" and args.peer_binary_sha256 and sha(exe) != args.peer_binary_sha256.lower():
                    raise ValueError("optional input executable SHA-256 differs")
            else:
                if tag == "baseline" and args.require_baseline_commit and identity(folder) != args.require_baseline_commit:
                    raise ValueError(f"baseline checkout must be {args.require_baseline_commit}")
                exe, item = build_image(tag, folder.resolve(), getattr(args, tag + "_entry"), area, results, env)
            bins[tag] = exe
            state["implementations"][tag] = item
            item["tested"] = False
        for tag in args.runtime_builds:
            exe, item = build_image(tag, args.runtime_source.resolve(), None, area, results, env, RUNTIME_BUILDS[tag])
            bins[tag] = exe
            state["implementations"][tag] = item
            item["tested"] = False
        if not args.skip_kernel_test:
            mark("kernel")
            tree = args.candidate_source.resolve()
            exe = area / "accept-binaries/candidate-kernel"
            cpp = tree / "cpp/pipeline"
            log = results / "kernel.log"
            exec_logged([os.environ.get("CXX", "g++"), "-std=c++20", "-O3", "-pthread", "-Wno-ignored-attributes",
                         "-I" + str(cpp), tree / "tests/pipeline/kernel_rows.cpp", cpp / "k_free.cpp", cpp / "k_free_x86.cpp",
                         cpp / "k_free_clicks.S", "-DSHOP_KERNEL_ABSOLUTE_TABLES", "-no-pie", "-o", exe], log, env)
            exec_logged([exe], log, env)
            wide = {"bmi1", "bmi2", "abm", "avx2", "avx512f", "avx512bw", "avx512vl", "avx512dq", "popcnt"} <= flags
            if "SIMD dispatch " + ("on." if wide else "off.") not in log.read_text():
                raise RuntimeError("kernel output did not confirm the expected native ISA dispatch")
            state["verified"]["kernel"] = "wide SIMD" if wide else "portable fallback"
        meter = area / "accept-binaries/meter"
        if any("image" in i for i in state["implementations"].values()):
            exec_logged(["gcc", "-O2", "-static", NATIVE/"meter.c", "-o", meter], results/"meter.log", env)
        for tag, exe in bins.items():
            item = state["implementations"][tag]
            if "image" in item:
                images[tag] = launcher(item, exe, meter, cpu_list, args.memory_mib)

        mark("fixtures and development tables")
        for tag, exe in list(bins.items()):
            try:
                verify_development(tag, exe, images.get(tag), area, results, env, args.candidate_source)
            except subprocess.CalledProcessError as error:
                if tag in ("baseline", "candidate"):
                    raise
                state["implementations"][tag].update(tested=False, excluded="fixture or 1M failure", error=str(error))
                print(f"Excluding {tag} after a development comparison failed", flush=True)
                del bins[tag]
                continue
            state["implementations"][tag]["tested"] = True
            state["verified"]["1M/" + tag] = "36 fixtures, additional edge cases, both published 1M tables"
        mark("patterned rules")
        for tag in ("candidate", *args.runtime_builds):
            if tag not in bins:
                continue
            rules = results/(tag + "-patterned-rules.jsonl")
            try:
                exec_logged([sys.executable, NATIVE/"port_patterns.py", bins[tag], bins["baseline"],
                             area/"starter-pyspark/fixtures", "--log", rules], rules.with_suffix(".log"), env)
            except subprocess.CalledProcessError as error:
                if tag == "candidate":
                    raise
                state["implementations"][tag].update(tested=False, excluded="patterned rules failure", error=str(error))
                del bins[tag]
                continue
            audits = [json.loads(line) for line in rules.read_text().splitlines()]
            state["verified"]["patterned rules/" + tag] = {path: sum(r["path"] == path for r in audits)
                                                               for path in ("direct", "generic")}
        for scale in ("20M", "60M"):
            if scale not in args.scales:
                continue
            data = (args.practice or area / "practice") if scale == "20M" else (args.final or area / "final")
            if scale == "60M" and not (data / "manifest.json").exists():
                if args.final:
                    raise FileNotFoundError(args.final)
                mark("generate seed 47")
                generate_final(area, results, env)
            if not (data / "manifest.json").is_file():
                raise FileNotFoundError(data / "manifest.json")
            mark(scale + " verify")
            inputs = sorted(data.rglob("*.csv"))
            state["verified"][scale + "/input"] = {"files": len(inputs), "csv_bytes": sum(f.stat().st_size for f in inputs)}
            with tempfile.TemporaryDirectory(dir=area) as ref:
                log = results / (scale + "-exact.log")
                exec_logged([bins["baseline"], data, ref], log, env)
                golden = [sys.executable, NATIVE/"acceptance_reference.py", ref, "--scale", scale,
                          "--expected", NATIVE/"acceptance-references.json"]
                exec_logged(golden, log, env)
                for tag, exe in bins.items():
                    if tag != "baseline":
                        with tempfile.TemporaryDirectory(dir=area) as out:
                            exec_logged([exe, data, out], log, env)
                            exec_logged([sys.executable, NATIVE/"compare_large.py", ref, out], log, env)
                    if tag in images:
                        with tempfile.TemporaryDirectory(dir=area) as out:
                            mlog = results/(scale + "-" + tag + "-image.log")
                            exec_logged([images[tag], data, out], mlog, env)
                            exec_logged([sys.executable, NATIVE/"compare_large.py", ref, out], mlog, env)
                            match = re.search(r"wall ([.\d]+) s, process peak RSS ([.\d]+) MiB, cgroup peak (-?[.\d]+) MiB", mlog.read_text())
                            if not match:
                                raise ValueError(f"missing container measurements in {mlog}")
                            state["verified"][scale + "/image/" + tag] = dict(zip(
                                ("wall_s", "process_peak_mib", "cgroup_peak_mib"), map(float, match.groups())))
                    state["verified"][scale + "/" + tag] = "exact tables and unique keys against independently fingerprinted control"
            mark(scale + " profiles and pairs")
            state["timings"][scale] = paired(scale, data, bins, area, results, env,
                                               args.warm_20 if scale == "20M" else args.warm_60, args.cold_runs)
            runner_temp = os.environ.get("RUNNER_TEMP")
            if (scale == "20M" and args.release_generated_practice and not args.practice and runner_temp
                    and area == (Path(runner_temp) / "shopping-trials").resolve()):
                shutil.rmtree(area/"practice")
        state["status"] = "passed"
        mark("finished")
    except BaseException as exc:
        state.update(status="failed", error=repr(exc))
        store(results / "status.json", state)
        raise
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(summary, "a") as f:
            print(f"### Native acceptance on {hardware['model']}\n\nPinned CPUs {cpus}.", file=f)
            for scale, results_by_mode in state["timings"].items():
                print(f"\n| {scale} | Median s | Peak RSS MiB |\n|---|---:|---:|", file=f)
                for name, r in results_by_mode.items():
                    print(f"| {name} | {r['median_s']:.4f} | {r['peak_mib']:.1f} |", file=f)
            print("\nPer-process CPU and I/O counters, input residency, stage logs, image cgroup peaks and exact comparisons are in the artifact.", file=f)


if __name__ == "__main__":
    if len(sys.argv) == 9 and sys.argv[1] == "container":
        _, _, image, entry, meter, cpus, memory, data, out = sys.argv
        sys.exit(in_container(image, entry, Path(meter), cpus, int(memory), Path(data), Path(out)))
    main()
