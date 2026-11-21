"""Expose the retained source samples and paired holdout results on public checks."""
import json
import re
import statistics
import sys
from pathlib import Path

def emit(title, message):
    text=message.replace('%','%25').replace('\r','%0D').replace('\n','%0A')
    print(f'::notice file=BENCHMARKS.md,line=1,title={title}::{text}',flush=True)

def publish_acceptance(root, status):
    state = json.loads(status.read_text())
    hw = json.loads(status.with_name("hardware.json").read_text())
    builds = state["implementations"]
    label = root.name
    def notice(title, lines):
        emit(f"Native {title} ({label})", "\n".join(lines))

    status_line = f'{state["status"]} at {state["phase"]}'
    if state.get("error"):
        status_line += " — " + state["error"]
    features = sorted(set(hw.get("isa", ())) & {"avx2", "avx512f", "avx512bw", "avx512cd",
                                              "avx512vl", "avx512dq", "avx512_vnni", "bmi1", "bmi2"})
    revisions = []
    for tag, item in builds.items():
        if not (item.get("source_commit") or item.get("hashes")):
            continue
        switches = ",".join(item.get("build_switches", ())) or "defaults"
        files = " ".join(f'{name}={digest}' for name,digest in item.get("hashes", {}).items() if name != "LICENSE")
        revisions.append(f'{tag} {item.get("source_commit") or "supplied binary"} {switches} {files}')
    notice("hardware and revisions", [
        f'{hw["vendor"]} {hw["model"]}, {hw["ncpu"]} vCPUs, affinity {hw["affinity"]}, '
        f'container limit {hw["memory_mib"]} MiB, workflow {hw.get("workflow_sha")}',
        "Relevant ISA " + ", ".join(features), status_line, *revisions,
    ])

    checked = state["verified"]
    lines = []
    for tag, item in builds.items():
        checks = [f'{stage} {checked[stage + "/" + tag]}' for stage in
                  ("1M", "patterned rules", "20M", "60M") if stage + "/" + tag in checked]
        excluded = item.get("excluded") or item.get("reason")
        if excluded:
            checks.append(excluded)
        lines.append(tag + " | " + (", ".join(checks) or "no recorded result"))
    if "kernel" in checked:
        lines.append("kernel " + checked["kernel"])
    notice("correctness", [status_line, *lines])

    for scale in ("20M", "60M"):
        images = []
        for tag in builds:
            image = checked.get(f"{scale}/image/{tag}")
            if image:
                images.append(f'{tag} {image["wall_s"]:.4f} s, process peak {image["process_peak_mib"]:.1f} MiB, '
                              f'cgroup peak {image["cgroup_peak_mib"]:.1f} MiB')
        if images:
            notice(scale + " containers", images)
        stages = status.with_name(scale + "-stages.jsonl")
        if stages.is_file():
            names = {item.get("executable"):tag for tag,item in builds.items()}
            lines = []
            for row in map(json.loads, stages.read_text().splitlines()):
                tag = names.get(row["binary"], Path(row["binary"]).parent.name)
                profile = row.get("profile", "").strip().splitlines()
                elapsed = next((s for s in profile if "parse=" in s or "parse " in s), "")
                if not elapsed and profile:
                    elapsed = profile[-1]
                lines.append(f'{tag} wall={row["wall_s"]:.4f}s cpu={row["user_s"]+row["sys_s"]:.4f}s '
                             f'major={row["major"]} {elapsed[:420]}')
            if lines:
                notice(scale + " stages", lines)

    if state.get("status") == "failed":
        errors = []
        for log in sorted(status.parent.glob("*.log")):
            with log.open("rb") as src:
                src.seek(max(0, log.stat().st_size - 6000))
                tail = src.read().decode(errors="replace").splitlines()
            if tail and re.search(r"^exit (?!0\b)-?\d+ after", tail[-1]):
                errors.append(f'{log.name}\n' + "\n".join(tail[-9:])[-1800:])
        if errors:
            notice("failed checks", errors[:2])

    for scale, rows in state["timings"].items():
        for mode in ("warm", "cold"):
            lines = []
            for key, metric in rows.items():
                cache, tag = key.split("/", 1)
                if cache != mode:
                    continue
                samples = ",".join(f'{s:.4f}' for s in metric["seconds"])
                parts = [f'{tag} {metric["median_s"]:.4f} s, runs [{samples}], {metric["peak_mib"]:.1f} MiB']
                if "eviction_confirmed_all" in metric:
                    parts.append("cold verified=" + str(metric["eviction_confirmed_all"]))
                if "paired_change_median" in metric:
                    parts.append(f'vs baseline {metric["paired_change_median"]:+.2%} '
                                 f'{metric["faster_pairs"]}/{len(metric["seconds"])} faster')
                for control, pair in metric.get("versus", {}).items():
                    parts.append(f'vs {control} {pair["median_ratio"]:.4f}x '
                                 f'{pair["faster_pairs"]}/{pair["pairs"]} faster')
                lines.append(" | ".join(parts))
            if lines:
                notice(scale + " " + mode, lines)


for root in map(Path,sys.argv[1:]):
    status = root / "port-acceptance" / "status.json"
    if status.is_file():
        publish_acceptance(root, status)
        continue
    # GitHub displays ten annotations per step. Lead with full release images
    # from each runner and aggregate their other conclusions separately.
    for report in sorted(root.rglob('all-notes.json')):
        notes=json.loads(report.read_text())
        images=[x for x in notes if x.startswith(('Image selection','Image binary','Image optimized binary','Docker fixture binary','Docker practice','Docker final','Docker holdout','Release image pairs'))]
        if images:emit('Release images '+report.parent.name,' || '.join(images)[:4000])
    for report in sorted(root.rglob('all-notes.json')):
        notes=json.loads(report.read_text())
        ranked=[]
        for scale in ('practice','final','holdout'):
            values=[]
            for entry in notes:
                m=re.match(scale+r' (\S+) median ([0-9.]+) s, runs ([^]]+)]',entry)
                if m:values.append((float(m[2]),m[1]))
            if values:ranked.append(scale+' '+', '.join(f'{name} {value:.4f}' for value,name in sorted(values)))
        if ranked:emit('Full-process medians '+report.parent.name,' || '.join(ranked))
        details=[]
        for entry in notes:
            if entry.startswith('Workers '): details.append(entry.partition(', runs')[0])
            elif entry.startswith('Input probe '):
                head,_,tail=entry.partition(', runs');details.append(head+' digest '+tail.partition('digest ')[2])
            elif entry.startswith(('Floor read','Floor lex','Hardware ','Runner CPU topology','Two pinned CPUs','Skewed revision locations','ThreadSanitizer ','Failure','Rejected ')):details.append(entry)
        if details:emit('Workers and cumulative input probes '+report.parent.name,' || '.join(details)[:4000])
    for report in sorted(root.rglob('all-notes.json')):
        notes=json.loads(report.read_text())
        calibration=[x for x in notes if x.startswith('Cross-profile ')]
        if calibration:emit('Compiler training by CPU capability '+report.parent.name,' || '.join(calibration)[:4100])
        balance=[x for x in notes if x.startswith('Per-worker tail ')]
        if balance:emit('Per-worker CPU and stage drain '+report.parent.name,' || '.join(balance)[:4100])
        minimum=[x for x in notes if x.startswith('Minimum Intel ISA ')]
        if minimum:emit('AVX-512F and BW reader audit '+report.parent.name,' || '.join(minimum)[:3000])
        projection=[x for x in notes if x.startswith(('Event projection ','Hot symbol sizes ','Instruction supply '))]
        if projection:emit('Event projections and instruction delivery '+report.parent.name,' || '.join(projection)[:3900])
        keep=[re.sub(r' {2,}',' ',x) for x in notes if x.startswith(('Release pairs','Population structure','ThreadSanitizer','20M process PMU','Sampled branch-misses','Sampled cache-misses','Input probe'))]
        if keep:emit('Paired comparisons and profiles '+report.parent.name,' || '.join(keep)[:4000])
        phase=[x for x in notes if x.startswith(('Reader CPU by input','Process boundaries','Revision storage','Phase PMU','Phase clock diagnostic'))]
        if phase:emit('Hardware samples by execution phase '+report.parent.name,' || '.join(phase)[:4000])
        isolated=[x for x in notes if x.startswith(('Isolated release input','Isolated release digests'))]
        if isolated:emit('Release input write costs '+report.parent.name,' || '.join(isolated)[:4000])
        revision_counts=[x for x in notes if x.startswith('Revision counts ')]
        if revision_counts:emit('Revision sieve and arena counts '+report.parent.name,' || '.join(revision_counts)[:4000])
        memory=[x for x in notes if x.startswith(('Per-stage resources','Timeline storage'))]
        if memory:emit('Memory allocation and phase faults '+report.parent.name,' || '.join(memory)[:4000])
    for source in root.rglob('source-lines*.txt'):
        folder=source.parent
        records=[]
        for line in source.read_text(errors='replace').splitlines():
            match=re.match(r'\s*([0-9.]+)%\s+(.+)',line)
            if match and len(records)<22:records.append(match.group(1)+'% '+match.group(2).strip()[:125])
        emit('Source samples '+folder.name+' '+source.stem,' / '.join(records) or 'Source line report contained no samples.')
    for profile in root.rglob('holdout-head-to-head.jsonl'):
        records={}
        for line in profile.read_text().splitlines():
            row=json.loads(line)
            if row['round']>=0:records.setdefault(Path(row['binary']).name,[]).append(row['wall_s'])
        emit('Five paired holdout medians '+profile.parent.name, ', '.join(f'{n} {statistics.median(v):.4f} s' for n,v in records.items()))
    for profile in root.rglob('holdout.jsonl'):
        groups={}
        for line in profile.read_text().splitlines():
            row=json.loads(line)
            if row['round'] >= 0:
                groups.setdefault(Path(row['binary']).name,[]).append(row)
        emit('All held-out entries '+profile.parent.name, ' / '.join(
            f"{name} {statistics.median(r['wall_s'] for r in rows):.4f} s at {max(r['peak_mib'] for r in rows):.0f} MiB"
            for name,rows in groups.items()))
    for report in root.rglob('cpu-samples.txt'):
        rows=[line.strip()[:135] for line in report.read_text(errors='replace').splitlines() if re.match(r'\s*[0-9.]+%',line)]
        if rows:emit('Top CPU symbols '+report.parent.name,' / '.join(rows[:9]))
