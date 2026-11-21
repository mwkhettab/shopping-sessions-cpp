"""Check the previously qualified release image and official control on held-out seed 733."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from port_acceptance import BASELINE, NATIVE, capture, exec_logged, generate_parts, sha, store

SEED, EVENTS, CLICKS, WORKERS = 733, 60_000_000, 15_000_000, 8
METER = re.compile(r"^wall ([\d.]+) s, process peak RSS ([\d.]+) MiB, cgroup peak (-?[\d.]+) MiB, "
                   r"major faults (\d+), minor faults (\d+)$", re.MULTILINE)


def qualified_image(state, tag, commit):
    item = state["implementations"].get(tag, {})
    checks = state["verified"]
    if (not item.get("tested") or item.get("source_commit") != commit or not item.get("hashes")
            or any(f"{scope}/{tag}" not in checks for scope in ("1M", "20M/image", "60M/image", "20M", "60M"))):
        raise ValueError(f"{tag} at {commit} did not complete image and table qualification")
    image = item["image_id"]
    if capture(["docker", "image", "inspect", "--format", "{{.Os}}/{{.Architecture}} {{.Id}}", image]) != "linux/amd64 " + image:
        raise ValueError(f"qualified linux/amd64 image unavailable: {image}")
    return {key: item[key] for key in ("source_commit", "image_id", "entry", "hashes", "build_switches")}


def run_image(item, target, source, area, log, hw, env):
    mask = ",".join(map(str, hw["affinity"]))
    exec_logged([sys.executable, NATIVE/"port_acceptance.py", "container", item["image_id"], item["entry"],
                 area/"accept-binaries/meter", mask, str(hw["memory_mib"]), source, target, str(WORKERS)], log, env)
    match = METER.search(log.read_text())
    if not match:
        raise ValueError(f"missing container meter output in {log}")
    wall, process, group, major, minor = match.groups()
    if float(process) <= 0 or float(group) <= 0:
        raise ValueError(f"missing process or cgroup peak in {log}")
    return {"wall_s": float(wall), "process_peak_mib": float(process), "cgroup_peak_mib": float(group),
            "major_faults": int(major), "minor_faults": int(minor)}


def qualify(area, tag, commit, *, counts=(EVENTS, CLICKS)):
    prior = area/"results/port-acceptance"
    state = json.loads((prior/"status.json").read_text())
    hw = json.loads((prior/"hardware.json").read_text())
    if state["status"] != "passed" or not hw["affinity"]:
        raise ValueError("a completed native qualification with CPU affinity is required")
    builds = {"baseline": qualified_image(state, "baseline", BASELINE), "shipping": qualified_image(state, tag, commit)}
    generator = area/"generator/target/release/generate"
    digest = sha(generator)
    if digest != state.get("checked_generator_sha256"):
        raise ValueError("generator does not match the binary checked against the original seed-1 dev files")
    env = dict(os.environ, SHOPPING_TRIALS_DIR=str(area), POLARS_MAX_THREADS="2", ARROW_NUM_THREADS="2")
    results = Path(tempfile.mkdtemp(prefix="shipping-733-", dir=area/"results"))
    summary = {"status": "running", "shipping_tag": tag, "seed": SEED, "events": counts[0], "clicks": counts[1],
               "application_workers": WORKERS, "cpu_affinity": hw["affinity"], "pinned_vcpus": len(hw["affinity"]),
               "cpu_model": hw["model"], "container_limit_mib": hw["memory_mib"], "generator_sha256": digest,
               "images": builds, "measurements": {}, "tables": {}}
    print("Held-out records", results, flush=True)
    def save():
        pending = results/"status.tmp"
        store(pending, summary)
        pending.replace(results/"status.json")
    save()
    try:
        with tempfile.TemporaryDirectory(prefix="heldout-733-", dir=area) as work:
            tmp = Path(work)
            source = tmp/"input"
            generate_parts(generator, SEED, *counts, source, results/"generate.log", env)
            inputs = list(source.rglob("*.csv"))
            summary["input"] = {"csv_files": len(inputs), "csv_bytes": sum(p.stat().st_size for p in inputs)}
            for role, item in builds.items():
                summary["measurements"][role] = run_image(item, tmp/role, source, area, results/(role+".log"), hw, env)
                summary["tables"][role] = json.loads(subprocess.check_output(
                    [sys.executable, NATIVE/"acceptance_reference.py", tmp/role], env=env, text=True))
                save()
            exec_logged([sys.executable, NATIVE/"compare_large.py", tmp/"baseline", tmp/"shipping"], results/"tables.log", env)
            if summary["tables"]["baseline"] != summary["tables"]["shipping"]:
                raise ValueError("logical table fingerprints differ")
        summary["status"] = "passed"
    except BaseException as exc:
        summary.update(status="failed", error=repr(exc))
        raise
    finally:
        save()
    record = json.dumps(summary, sort_keys=True)
    print(record, flush=True)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::notice file=BENCHMARKS.md,line=1,title=Native held-out seed 733::" + record.replace("%", "%25"))
    return summary


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--area", type=Path, default=Path(os.environ.get("RUNNER_TEMP", "/tmp"))/"shopping-trials")
    p.add_argument("--shipping-tag", default="candidate")
    p.add_argument("--shipping-commit", required=True)
    args = p.parse_args()
    if not re.fullmatch("[0-9a-f]{40}", args.shipping_commit) or args.shipping_tag == "baseline":
        p.error("supply the exact shipping source SHA and its qualified candidate tag")
    qualify(args.area.resolve(), args.shipping_tag, args.shipping_commit)
