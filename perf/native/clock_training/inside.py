"""Run the pinned shipping build twice and verify actual LLVM counter coverage."""
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import time

ROOT = Path("/tmp/shop-profile")
PROOF = Path("/build-proof")
CASES = ("market", "depot", "scalar", "opaque")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(label, *command, env=None):
    start = time.monotonic()
    print("Starting", label, flush=True)
    status = subprocess.run(command, env=env).returncode
    metrics = {"step": label, "seconds": round(time.monotonic() - start, 3), "status": status,
               "peak_child_kib": resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss}
    print(metrics, flush=True)
    with (PROOF/"steps.jsonl").open("a") as out:
        print(json.dumps(metrics, sort_keys=True), file=out)
    if status:
        raise SystemExit(status)


def counts(profile, expected):
    # Instrumentation is placed before inlining. Count function entries even
    # when the linker subsequently inlines them into the final executable.
    listing = subprocess.check_output(
        ["llvm-profdata-16", "show", "--all-functions", "--counts", profile], text=True)
    names = {"wide_events": "_ZN8pipeline6k_free6detail10events_x86E",
             "wide_clicks": "_ZN8pipeline6k_free6detail10clicks_x86E",
             "stage3": "_ZN8pipeline6events6stage3INS_1GEEE",
             "generic": "_ZN8pipeline11run_genericE", "entry": "main"}
    result = {}
    for key, name in names.items():
        anchor = re.escape(name) + ("" if name == "main" else "[^\n]*")
        hit = re.findall(r"(?m)^  " + anchor + r":\n(?:    [^\n]*\n)*?    Function count: (\d+)", listing)
        if len(hit) != 1:
            raise ValueError(f"{profile.name}: expected one qualified {key} counter, found {hit}")
        result[key] = int(hit[0])
    for key, condition in expected.items():
        if bool(result[key]) is not condition:
            raise ValueError(f"{profile.name}: {key} contradicts the process evidence {result}")
    return result


def visible_probes():
    profile = Path("build/shopping")
    symbols = subprocess.check_output(["nm", profile], text=True)
    sections = subprocess.check_output(["readelf", "-W", "-S", profile], text=True)
    bad = [x for x in ("__wrap__exit", "__wrap_execv", "shop_training_reader", "__llvm_profile_",
                        "__profc_", "__profd_", "__llvm_prf_") if x in symbols or x in sections]
    if bad:
        raise ValueError(f"training symbols/sections in release {bad}")


def main():
    ROOT.mkdir()
    PROOF.mkdir(exist_ok=True)
    data = ROOT/"data"; data.mkdir()
    raw = ROOT/"raw"; raw.mkdir()
    for name in CASES:
        (data/name).symlink_to(Path("/corpus")/name, target_is_directory=True)
    wrapper = ROOT/"clang++-16"
    shutil.copyfile("/training/compiler.py", wrapper)
    wrapper.chmod(0o755)
    training = Path("/training/train.py")
    run("compile-probes", "clang++-16", "-std=c++20", "-O2", "-pthread", "-fno-pie",
        "-c", "/training/training_gate.cpp", "-o", ROOT/"gate.o")
    fields = Path("cpp/pipeline/k_free.hpp")
    original = fields.read_bytes()
    declaration = b"bool available();"
    call = b"if (available())"
    if original.count(declaration) != 1 or original.count(call) != 2:
        raise ValueError("field dispatch anchors changed")
    fields.write_bytes(original.replace(declaration, declaration + b'\nextern "C" bool shop_training_reader(bool);')
                       .replace(call, b"if (shop_training_reader(available()))"))
    cfg = dict(os.environ, CXX=str(wrapper), STATIC="1", SHOP_PHASE="generate",
               SHOP_PROFILE_DIR=str(raw), LLVM_PROFILE_FILE=str(ROOT/"unused-%p.profraw"))
    try:
        run("compile-instrumented", "sh", "build.sh", env=cfg)
    finally:
        fields.write_bytes(original)
    run("train", sys.executable, training, "train", data, "--binary", "build/shopping", env=cfg)
    cases = json.loads((data/"train.json").read_text())
    wanted = {name + ".exit.profraw" for name in CASES} | {"opaque.exec.profraw"}
    if {f.name for f in raw.glob("*.profraw")} != wanted:
        raise ValueError("expected all four process exits and the pre-exec generic transition")
    files = sorted(raw.glob("*.profraw"))
    merged = ROOT/"use.profdata"
    run("merge-profiles", "llvm-profdata-16", "merge", "--failure-mode=any", *files, "-o", merged)
    covered = {}
    for path in files:
        # Convert each raw input separately: this also validates its header and
        # preserves the distinction between both process images of the same PID.
        single = ROOT/(path.name+".indexed")
        subprocess.run(["llvm-profdata-16", "merge", path, "-o", single], check=True)
        name, phase, _ = path.name.split(".")
        compact = name != "opaque"
        wide = bool(cases[name]["wide_field_buffers"]) if compact else False
        covered[path.name] = counts(single, {"entry": True, "stage3": compact,
                                    "generic": name == "opaque" and phase == "exit",
                                    "wide_events": wide, "wide_clicks": wide})
    combined = counts(merged, {"entry": True, "stage3": True, "generic": True,
                              "wide_events": any(c["wide_field_buffers"] for c in cases.values()),
                              "wide_clicks": any(c["wide_field_buffers"] for c in cases.values())})
    if any(sum(r[k] for r in covered.values()) != n for k, n in combined.items()):
        raise ValueError("merged function counts do not equal the five isolated dumps")
    cfg["SHOP_PHASE"] = "use"
    run("compile-profile-use", "sh", "build.sh", env=cfg)
    visible_probes()
    run("compare-synthesized-tables", sys.executable, training, "check", data, "--binary", "build/shopping")
    shutil.copyfile(data/"check.json", PROOF/"check.json")
    for name in ("source.json", "inputs.sha256"):
        shutil.copyfile("/"+name, PROOF/name)
    proof = {"compiler": subprocess.check_output(["clang++-16", "--version"], text=True).splitlines()[0],
             "profdata": subprocess.check_output(["llvm-profdata-16", "show", merged], text=True),
             "training": cases, "case_entry_counts": covered, "merged_entry_counts": combined,
             "profile_sha256": sha(merged), "release_sha256": sha("build/shopping"),
             "wide_reader_observed": bool(combined["wide_events"]),
             "source": json.loads((PROOF/"source.json").read_text())}
    (PROOF/"proof.json").write_text(json.dumps(proof, sort_keys=True, indent=2)+"\n")
    print("LLVM training coverage", covered, "release", proof["release_sha256"], flush=True)


if __name__ == "__main__":
    main()
