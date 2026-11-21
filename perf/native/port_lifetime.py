"""Time fresh production-image containers using the Docker engine's start and finish timestamps."""
from datetime import datetime
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

from port_acceptance import container_options, release_output


def run_image(image, source, area, cpus, memory_mib, csvs):
    with tempfile.TemporaryDirectory(dir=area) as temp:
        root = Path(temp)
        out, scratch = root / "output", root / "scratch"
        for folder in (out, scratch):
            folder.mkdir()
            folder.chmod(0o777)
        settings = container_options(cpus, memory_mib, source, out, scratch)
        container = subprocess.check_output(["docker", "create", *settings, image], text=True).strip()
        try:
            block = bytearray(2 << 20)
            for file in csvs:
                with file.open("rb", buffering=0) as f:
                    while f.readinto(block):
                        pass
            start = time.monotonic_ns()
            run = subprocess.run(["docker", "start", "--attach", container], capture_output=True, text=True)
            client_s = (time.monotonic_ns() - start) * 1e-9
            state = json.loads(subprocess.check_output(["docker", "inspect", "--format", "{{json .State}}", container], text=True))
            if run.returncode or state["ExitCode"] or state["OOMKilled"] or state["Status"] != "exited":
                raise RuntimeError(f'container {container} exited {state}, {run.stderr[-1200:]}')
            def stamp(key):
                return datetime.fromisoformat(state[key].replace("Z", "+00:00"))
            engine_s = (stamp("FinishedAt") - stamp("StartedAt")).total_seconds()
            if engine_s <= 0:
                raise ValueError(f'invalid Docker lifecycle timestamps {state}')
            sizes = {t: sum(p.stat().st_size for p in (out/t).glob("*.parquet")) for t in ("user_daily", "campaign_daily")}
            if not all(sizes.values()):
                raise ValueError(f'missing output from {image}')
            return {"engine_s": engine_s, "client_start_attach_s": client_s, "output_bytes": sizes}
        finally:
            try:
                subprocess.run(["docker", "rm", "-f", container], stdout=subprocess.DEVNULL, check=True)
            finally:
                release_output((out, scratch))


def paired_lifetime(images, source, area, results, cpus, memory_mib, scale, rounds):
    csvs = sorted(source.rglob("*.csv"))
    labels = ["baseline", "candidate"]
    observations = {tag: [] for tag in labels}
    with (results / f"{scale}-container-lifetime.jsonl").open("x", buffering=1) as samples:
        for round_id in range(-1, rounds):
            for tag in labels if round_id % 2 == 0 else reversed(labels):
                record = run_image(images[tag]["image_id"], source, area, cpus, memory_mib, csvs)
                record.update(label=tag, round=round_id, image_id=images[tag]["image_id"])
                print(json.dumps(record), file=samples)
                if round_id >= 0:
                    observations[tag].append(record)
    summary = {}
    for tag, samples in observations.items():
        values = [r["engine_s"] for r in samples]
        summary[tag] = {"median_engine_s": statistics.median(values), "engine_s": values,
                        "median_client_start_attach_s": statistics.median(r["client_start_attach_s"] for r in samples)}
    ratios = [c["engine_s"] / b["engine_s"] for c, b in zip(observations["candidate"], observations["baseline"])]
    summary["paired"] = {"median_ratio": statistics.median(ratios), "wins": sum(v < 1 for v in ratios), "rounds": rounds}
    print(f'::notice file=BENCHMARKS.md,line=1,title={scale} Docker start to exit::{json.dumps(summary, sort_keys=True)}', flush=True)
    return summary
