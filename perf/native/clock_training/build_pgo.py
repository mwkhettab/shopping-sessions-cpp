"""Build/export the pinned Clang 16 full-LTO PGO candidate and its training proof."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
FILES = ("train.py", "compiler.py", "inside.py", "training_gate.cpp")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(checkout, revision, target, date_patch):
    if not re.fullmatch("[0-9a-f]{40}", revision):
        raise ValueError("expected full lowercase Git commit SHA")
    head = subprocess.check_output(["git", "-C", checkout, "rev-parse", "--verify", "HEAD"], text=True).strip()
    if head != revision:
        raise ValueError(f"checkout is {head}, expected {revision}")
    target.mkdir()
    with tempfile.TemporaryFile() as archive:
        subprocess.run(["git", "-C", checkout, "archive", revision, "build.sh", "Dockerfile", "cpp/pipeline"],
                       stdout=archive, check=True)
        archive.seek(0)
        with tarfile.open(fileobj=archive) as tar:
            for item in tar.getmembers():
                if item.name.startswith("/") or ".." in Path(item.name).parts or not (item.isdir() or item.isfile()):
                    raise ValueError(f"unexpected archive record {item.name}")
            tar.extractall(target)
    field_name = "cpp/pipeline/k_free_x86.cpp"
    field = target/field_name
    original_hash = sha(field)
    if date_patch:
        # The independently exported trial may also carry a historical build.sh
        # hunk. Consume only the kernel hunk and leave today's release command intact.
        command = ["git", "apply", "--no-index", "--include="+field_name]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/field_name).parent.mkdir(parents=True)
            shutil.copyfile(field, root/field_name)
            subprocess.run([*command, "--check", date_patch], cwd=root, check=True)
            subprocess.run([*command, date_patch], cwd=root, check=True)
            shutil.copyfile(root/field_name, field)
        if sha(field) == original_hash:
            raise ValueError("date patch selected no kernel change")
    original_docker = (target/"Dockerfile").read_text()
    base, work = original_docker.split("WORKDIR /src\n", 1)
    build_command = "RUN CXX=clang++-16 STATIC=1 sh build.sh\n"
    if work.count(build_command) != 1 or work.count("FROM scratch\n") != 1:
        raise ValueError("release Docker recipe changed")
    script = (target/"build.sh").read_text()
    objects = "build/main.bc build/k_free.bc build/k_free_x86.bc build/sys_global_alloc.bc build/clicks.o"
    if not all(x in script for x in ("-flto=full", "-march=x86-64", "-mtune=cascadelake", objects)):
        raise ValueError("expected stock full-LTO compiler and object ordering")
    package = "g++ clang-16 lld-16"
    if base.count(package) != 1 or " AS build\n" not in base:
        raise ValueError("release compiler package list changed")
    # Pin the base through the checked-in digest. LLVM's profile runtime and
    # llvm-profdata are separate Debian packages when recommends are disabled.
    base = base.replace(" AS build\n", " AS tools\n").replace(
        package, package+" llvm-16 libclang-rt-16-dev binutils python3")
    release = work.split("FROM scratch\n", 1)[1]
    date = str(int(bool(date_patch)))
    docker = base + '''
FROM tools AS corpus
COPY training/train.py /training/train.py
RUN python3 -c 'import datetime, hashlib, json, pathlib, random, resource, shutil, subprocess, tempfile' \\
    && python3 /training/train.py generate /corpus \\
    && cd /corpus && find . -type f -print0 | sort -z | xargs -0 sha256sum > /inputs.sha256

FROM tools AS build
WORKDIR /src
COPY --from=corpus /corpus /corpus
COPY --from=corpus /inputs.sha256 /inputs.sha256
COPY cpp/pipeline cpp/pipeline
COPY build.sh .
COPY training /training
COPY source.json /source.json
''' + f"RUN SHOP_DATE={date} python3 /training/inside.py\n\nFROM scratch AS release\n" + release
    docker += "\nFROM scratch AS proof\nCOPY --from=build /build-proof/ /\n"
    (target/"Dockerfile").write_text(docker)
    training = target/"training"; training.mkdir()
    for file in FILES:
        shutil.copyfile(HERE/file, training/file)
    source = {"revision": revision, "stock_build_sha256": sha(target/"build.sh"),
              "original_field_sha256": original_hash, "compiled_field_sha256": sha(field),
              "date_patch_sha256": sha(date_patch) if date_patch else None,
              "training_sha256": {file: sha(training/file) for file in FILES}}
    (target/"source.json").write_text(json.dumps(source, sort_keys=True, indent=2)+"\n")
    return source


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("checkout", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("expected_full_sha")
    p.add_argument("--date-patch", type=Path)
    p.add_argument("--tag", help="also load this exact linux/amd64 release image locally")
    p.add_argument("--prepare-only", action="store_true", help="export the complete build context without Docker")
    arg = p.parse_args()
    out = arg.output.resolve()
    if out.exists(): raise SystemExit(f"new output directory required {out}")
    out.mkdir(parents=True)
    context = out/"context"
    source = prepare(arg.checkout.resolve(), arg.expected_full_sha, context,
                     arg.date_patch.resolve() if arg.date_patch else None)
    if arg.prepare_only:
        print("Prepared", context, source["revision"], source["compiled_field_sha256"]); return
    build = ["docker", "buildx", "build", "--platform", "linux/amd64"]
    for target in ("release", "proof"):
        subprocess.run([*build, "--target", target, "--output", f"type=local,dest={out/target}", context], check=True)
    proof = json.loads((out/"proof/proof.json").read_text())
    elf = out/"release/app/shopping"
    if proof["source"] != source or sha(elf) != proof["release_sha256"]:
        raise ValueError("release and generated/consumed profile proofs disagree")
    if arg.tag:
        subprocess.run([*build, "--target", "release", "--load", "-t", arg.tag, context], check=True)
        container = subprocess.check_output(["docker", "create", "--platform", "linux/amd64", arg.tag], text=True).strip()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                staged = Path(tmp)/"shopping"
                subprocess.run(["docker", "cp", container+":/app/shopping", staged], check=True)
                if sha(staged) != proof["release_sha256"]: raise ValueError("loaded image ELF differs from tested export")
        finally:
            subprocess.run(["docker", "rm", container], check=True)
    print("Verified release", source["revision"], proof["release_sha256"], elf)


if __name__ == "__main__":
    main()
