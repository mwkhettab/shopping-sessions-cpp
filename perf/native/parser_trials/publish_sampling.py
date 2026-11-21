"""Publish three compact GitHub annotations from one native parse sampling summary."""
import argparse
import json
from pathlib import Path
from statistics import median


def notice(title, message):
    def escape(s):
        return s.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    name = escape(title).replace(":", "%3A").replace(",", "%2C")
    print(f"::notice title={name}::{escape(message)}")


def publish(data, scale):
    rounds = list(data["rounds"].values())
    roles = sorted((role for role in rounds[0] if role.startswith("s") and role[1:].isdigit()), key=lambda v: int(v[1:]))
    role = roles[0]
    profiles = [r[role]["probe"] for r in rounds]
    cpus = data["affinity"]
    worker_count = profiles[0]["config"]["workers"]
    shipping_parse = median(r["shipping"]["parse_ms"] for r in rounds)
    shipping_wall = 1000 * median(r["shipping"]["wall_s"] for r in rounds)
    overhead = []
    for name in ["coarse", *roles]:
        o = data["overhead_versus_shipping"][name]
        def ratio(metric):
            value = o[metric]["cache_confirmed_median_ratio"]
            return "n/a" if value is None else f"{(value-1)*100:+.1f}%"
        overhead.append(f"{name} {ratio('parse_ms')}/{ratio('wall_s')} ({o['wall_s']['cache_confirmed_pairs']}/{len(rounds)} pairs)")
    pins = f"{len(cpus)} pinned CPUs" if cpus is not None else "affinity unavailable"
    notice(f"{scale} parser probe overhead",
           f"{data['cache']} input, {pins}, {worker_count} application workers. "
           f"Shipping medians {shipping_parse:.2f} ms parse, {shipping_wall:.2f} ms process. "
           "Paired parse/process changes with confirmed cache residency were " + " | ".join(overhead))
    leaves = (("field_reader_and_callback_overhead_ns", "fields"), ("classify_ns", "classify"),
              ("scatter_ns", "scatter"), ("fused_ns", "fused CSR"), ("special_ns", "special"))
    parts = []
    for kind in ("event", "click"):
        estimates = [p["estimated_exclusive_body_worker_ns"].get(kind, {}) for p in profiles]
        if not all(estimates):
            parts.append(kind + " had no active sample")
            continue
        parts.append(kind + " " + ", ".join(f"{text} {median(e[key] for e in estimates)/1e6:.2f}"
                                            for key, text in leaves if kind=="click" or key!="fused_ns"))
    links = median(p["exclusive_worker_ns"]["link_body_ns"] for p in profiles) / 1e6
    span = median(p["worker_span_ns"] for p in profiles) / 1e6
    notice(f"{scale} parser worker-time estimates",
           f"{role} per-component medians in worker-ms summed across {worker_count} workers. "
           + " | ".join(parts) + f" | link reader {links:.2f} directly measured. "
           f"Worker entry-to-last-exit span {span:.2f} ms wall. Fields include reader and callback overhead.")
    coverage = []
    for kind in ("event", "click", "link"):
        obs = [p["coverage_and_inclusive_ns"][kind] for p in profiles]
        fractions = [100*v["sample_rows"]/v["rows"] for v in obs if v["rows"]]
        coverage.append(f"{kind} {obs[0]['sample_chunks']}/{obs[0]['chunks']} chunks, "
                        + (f"{min(fractions):.1f}–{max(fractions):.1f}% rows" if fractions else "zero rows"))
    samples = [list(p["worker_balance"].values()) for p in profiles]
    elapsed = [[(w["end_ns"]-w["begin_ns"])/1e6 for w in workers] for workers in samples]
    bytes_read = [[sum(k["bytes"] for k in w["kind"].values())/(1<<20) for w in workers] for workers in samples]
    def minmax(values, digits):
        return f"{median(min(a) for a in values):.{digits}f}–{median(max(a) for a in values):.{digits}f}"
    tail = median(max(w["exclusive_worker_ns"]["tail_ns"] for w in workers) for workers in samples)/1e6
    notice(f"{scale} parser coverage and worker balance", f"{role} coverage. " + " | ".join(coverage)
           + f". Per-worker min–max medians across rounds {minmax(elapsed,2)} ms elapsed and "
           f"{minmax(bytes_read,1)} MiB input. Slowest final flush/jobs/spin median {tail:.2f} ms.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    parser.add_argument("--scale", required=True)
    args = parser.parse_args()
    publish(json.loads(args.summary.read_text()), args.scale)
