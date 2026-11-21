"""Build independent release images, then check their actual wide parser objects on Intel."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
PLAN = json.loads((HERE / "plan.json").read_text())


def replace_one(path, old, new):
    s = path.read_text()
    if s.count(old) != 1:
        raise ValueError(f"expected one {old!r} in {path}")
    path.write_text(s.replace(old, new))


def build(source_archive, name, changes, output):
    if not all(re.fullmatch(r"[a-z0-9-]+", v) for v in [name, *changes]):
        raise ValueError("invalid variant name")
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp)
        with tarfile.open(source_archive) as files:
            files.extractall(tree, filter="data")
        for change in changes:
            subprocess.run(["git", "apply", "--no-index", HERE / (change + ".patch")], cwd=tree, check=True)
        flags = []
        if "sparse-groups" in changes:
            flags.append("-DSHOP_SPARSE_USER_OUTPUT=1")
        if "event12-scatter" in changes:
            flags.append("-DSHOP_R12_SCATTER=1")
        if "batched-walk" in changes:
            flags.append("-DSHOP_BATCHED_WALK=1")
        if flags:
            tag = "set -- -std=c++20 -O3"
            replace_one(tree / "build.sh", tag, tag + " " + " ".join(flags))
        docker = tree / "Dockerfile"
        if "paired-events" in changes:
            replace_one(docker, "RUN CXX=clang++-16 STATIC=1 sh build.sh",
                        "RUN PAIRED_EVENT_KERNEL=1 CXX=clang++-16 STATIC=1 sh build.sh")
        checks = []
        if "paired-events" in changes:
            checks.append(("paired_rows", ["-DWATCH_PAIRED_ABI",
                *["-Wl,--wrap=shopping_kernel_event_" + s for s in ("loop","flush","redo","purchase")]]))
        if "click-pairs" in changes:
            checks.append(("click_rows", []))
        if checks:
            with (tree/".dockerignore").open("a") as f:
                f.write("\n!tests/\n!tests/pipeline/\n!tests/pipeline/**\n")
            command = ["COPY tests/pipeline tests/pipeline"]
            for test, opts in checks:
                shutil.copy2(HERE / (test+".cpp"), tree/"tests/pipeline")
                options = ["clang++-16", "-std=c++20", "-O3", "-DNDEBUG", "-pthread", "-fno-pic", "-fno-pie",
                           "-march=x86-64", "-mtune=cascadelake", "-fconstexpr-steps=100000000", "-flto=full",
                           "-DSHOP_KERNEL_ABSOLUTE_TABLES", "-DREQUIRE_NATIVE_EVENT_KERNEL", "-Wno-return-type",
                           "-Icpp/pipeline", "-Itests/pipeline", *opts]
                if "paired-events" in changes:
                    options.extend(("-DSHOP_PAIRED_EVENT_KERNEL", "build/event_pair.o"))
                options.extend((f"tests/pipeline/{test}.cpp", "build/k_free.bc", "build/k_free_x86.bc", "build/clicks.o",
                                "-static", "-no-pie", "-fuse-ld=lld-16", "-Wl,--lto-O3", "-Wl,--threads=1", "-o", f"/{test}"))
                command.append("RUN " + " ".join(options))
            replace_one(docker, "FROM scratch", "\n".join(command) + "\n\nFROM scratch")
            with docker.open("a") as f:
                for test, _ in checks:
                    f.write(f"\nCOPY --from=build /{test} /tests/{test}\n")
        log = output / (name + ".log")
        with log.open("w") as stream:
            subprocess.run(["docker", "buildx", "build", "--platform", "linux/amd64", "--progress", "plain",
                            "--output", "type=local,dest="+str(output/name), tree], stdout=stream, stderr=subprocess.STDOUT, check=True)
            for test, _ in checks:
                subprocess.run([output/name/"tests"/test], stdout=stream, stderr=subprocess.STDOUT, check=True)
            cpp_tests = []
            if "stage192" in changes:
                cpp_tests.extend(("routing_part", "routing_packets", "routing_sinks"))
            if "input-release" in changes:
                cpp_tests.append("routing_input_release")
            if "packed12" in changes:
                cpp_tests.append("routing_event12")
            if "event12-scatter" in changes:
                shutil.copy2(HERE / "edge_packets.cpp", tree / "tests/pipeline")
                cpp_tests.append("edge_packets")
            if "batched-walk" in changes:
                cpp_tests.append("session_runs")
            for test in cpp_tests:
                exe = tree / test
                subprocess.run(["g++-12", "-std=c++20", "-O2", "-pthread", "-Wno-ignored-attributes",
                                *flags, *(["-DSESSION_TEST_FAST"] if test == "session_runs" else []),
                                "-Icpp/pipeline", f"tests/pipeline/{test}.cpp",
                                "cpp/pipeline/sys_global_alloc.cpp", "-o", exe],
                               cwd=tree, stdout=stream, stderr=subprocess.STDOUT, check=True)
                if test == "session_runs":
                    for pattern in range(5):
                        subprocess.run([exe, str(pattern)], stdout=stream, stderr=subprocess.STDOUT, check=True)
                else:
                    subprocess.run([exe], stdout=stream, stderr=subprocess.STDOUT, check=True)
            if "sparse-groups" in changes:
                exe=tree/"check-user-groups"
                subprocess.run(["g++-12","-std=c++20","-O2","-pthread","-Wno-ignored-attributes",
                                "-DSHOP_SPARSE_USER_OUTPUT=1","-Icpp/pipeline",HERE/"edge_writer.cpp",
                                "cpp/pipeline/sys_global_alloc.cpp","-o",exe],cwd=tree,stdout=stream,stderr=subprocess.STDOUT,check=True)
                python=Path(os.environ["RUNNER_TEMP"])/"shopping-python/bin/python"
                subprocess.run([python,HERE/"edge_writer.py",exe],stdout=stream,stderr=subprocess.STDOUT,check=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(["git","-C",args.source,"rev-parse","HEAD"],text=True).strip()
    if revision != PLAN["base"]:
        raise ValueError((revision,PLAN["base"]))
    good=[]
    with tempfile.TemporaryDirectory() as tmp:
        archive=Path(tmp)/"sources.tar"
        subprocess.run(["git","-C",args.source,"archive","--output",archive,"HEAD",".dockerignore","Dockerfile",
                        "build.sh","cpp/pipeline","tests/pipeline"], check=True)
        for name, changes in PLAN["variants"].items():
            try:
                build(archive,name,changes,args.output)
            except (OSError, ValueError, subprocess.CalledProcessError) as exc:
                log=args.output/(name+".log")
                tail=log.read_text(errors="replace").splitlines()[-14:] if log.is_file() else []
                message=" | ".join([str(exc),*tail])[-3400:].replace("%","%25")
                print(f"::warning title={name} build or row check::{message}",flush=True)
                continue
            good.append(name)
            print(f"::notice title={name} candidate build::Release built and selected row checks passed.",flush=True)
    (args.output/"passed.txt").write_text("".join(name+"\n" for name in good))
    if not good:
        raise SystemExit("All candidate builds or focused rows failed")

if __name__=="__main__":
    main()
