#!/bin/bash
set -eu -o pipefail
if (( $#<2 )); then echo "usage: build-native.sh REPO-OR-GIT-ARCHIVE OUTPUT [VARIANT ...]" >&2; exit 2; fi
input=$1; output=$2; shift 2
revision=faf1047c6cb00cc8099572f5e067d18076375571
if (( $#==0 )); then set -- gcc-stock portable-unpadded portable-aligned portable-inline v4-aligned guard; fi
for v in "$@"; do
  case "$v" in gcc-stock|portable-unpadded|portable-aligned|portable-inline|v4-aligned|v4-inline|guard) ;;
    *) echo "unknown variant $v" >&2;exit 2;; esac
done
mkdir -p -- "$output"
output=$(cd -- "$output" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/source"
if [[ -f "$input" ]]; then
  tar -xf "$input" -C "$work/source" cpp/pipeline build.sh
else
  git -C "$input" archive "$revision" cpp/pipeline build.sh | tar -x -C "$work/source"
fi
if [[ ! -f "$work/source/cpp/pipeline/main.cpp" ]]; then echo "source archive missing" >&2;exit 2;fi
python3 - "$work/source" <<'TRIAL_VERIFY_SOURCE'
import hashlib,pathlib,sys
src=pathlib.Path(sys.argv[1]);d=hashlib.sha256()
for f in sorted([src/"build.sh",*(p for p in (src/"cpp/pipeline").rglob("*") if p.is_file())]):
    d.update(f.relative_to(src).as_posix().encode()+b"\0"+hashlib.sha256(f.read_bytes()).digest())
if d.hexdigest()!="aef31236ad223921f41adecbd469e202c8f94d42a4e31a70d358f82360a723bd":raise SystemExit("source files differ from faf1047")
TRIAL_VERIFY_SOURCE
cat > "$work/linux.sh" <<'TRIAL_LINUX_SH'
#!/bin/bash
set -eu -o pipefail
cd /trial/source
cc=clang++-16
guard=(python3 /trial/guard_compile.py)
action=$1
shift
{
  "$cc" --version
  ld.lld-16 --version
  g++ --version | head -1
  cat /etc/os-release | head -3
  dpkg-query -W clang-16 libllvm16 libclang-cpp16 lld-16 libstdc++-12-dev libc6-dev
} > /o/toolchain.txt
if [[ "$action" == inspect ]]; then
  exec python3 /trial/symbols.py "$@"
fi
temp_src=$(mktemp -d)
trap 'rm -rf -- "$temp_src"' EXIT
cp -R /trial/source/cpp "$temp_src/"
cd "$temp_src"
python3 - <<'PY'
from pathlib import Path
p=Path("cpp/pipeline/parse.hpp")
s=p.read_text()
old="    Ctx c{g,t,eb,cb,out,tb,"
assert s.count(old)==1
p.write_text(s.replace(old,"    Ctx<G,T> c{g,t,eb,cb,out,tb,"))
PY
if (( $# == 0 )); then set -- portable-unpadded portable-aligned v4-aligned guard; fi
for name in "$@"; do
  if [[ "$name" == gcc-stock ]]; then
    mkdir -p "$temp_src/control";cp -R /trial/source/cpp /trial/source/build.sh "$temp_src/control/"
    (cd "$temp_src/control"; STATIC=1 CXX=g++ "${guard[@]}" sh build.sh)
    cp "$temp_src/control/build/shopping" /o/gcc-stock
    continue
  fi
  if [[ "$name" == guard ]]; then
    "${guard[@]}" gcc -std=gnu11 -Os -march=x86-64 -fno-lto -static -no-pie /trial/gate.c -o /o/shopping
    if [[ -x /o/portable-aligned && -x /o/v4-aligned ]]; then
      ln -f /o/portable-aligned /o/shopping.base
      ln -f /o/v4-aligned /o/shopping.opt
    fi
    continue
  fi
  case "$name" in
    portable-unpadded) cpu=x86-64; align=0;;
    portable-aligned) cpu=x86-64; align=1;;
    v4-aligned) cpu=x86-64-v4; align=1;;
    portable-inline|v4-inline) cpu=x86-64; [[ "$name" == portable-inline ]] || cpu=x86-64-v4; align=1;;
    *) echo "unknown build variant $name" >&2; exit 2;;
  esac
  common=(-std=c++20 -O3 -DNDEBUG -pthread -fno-pic -fno-pie "-march=$cpu" -mtune=cascadelake -fconstexpr-steps=100000000 -DSHOP_KERNEL_ABSOLUTE_TABLES)
  asmflags=()
  ldflags=(-static -no-pie -fuse-ld=lld-16 -Wl,--lto-O3 -Wl,--threads=1)
  if [[ $align == 1 ]]; then
    asmflags=(-mllvm -x86-branches-within-32B-boundaries -mllvm -x86-pad-max-prefix-size=5)
    ldflags+=(-Wl,-mllvm=-x86-branches-within-32B-boundaries -Wl,-mllvm=-x86-pad-max-prefix-size=5)
  fi
  objects=()
  mkdir -p "/o/$cpu"
  for f in main k_free k_free_x86 sys_global_alloc; do
    src=cpp/pipeline/$f.cpp
    obj="/o/$cpu/$f.bc"
    if [[ "$f" == k_free_x86 && "$name" == *-inline ]]; then
      src=cpp/pipeline/k_free_inline.cpp
      obj="/o/$cpu/k_free_inline.bc"
      python3 - <<'PY'
from pathlib import Path
p=Path("cpp/pipeline/k_free_x86.cpp")
s=p.read_text()
for declared in ("inline bool ev_s(","inline Step ev_d(","inline P ev_fast("):
    assert s.count(declared)==1
    s=s.replace(declared,"[[gnu::always_inline]] "+declared)
p.with_name("k_free_inline.cpp").write_text(s)
PY
    fi
    # Full-LTO code generation occurs in LLD. Both portable layouts share identical input bitcode.
    if [[ ! -s "$obj" ]]; then
      printf '%q ' "$cc" "${common[@]}" -flto=full -c "$src" -o "$obj"; echo
      "${guard[@]}" "$cc" "${common[@]}" -flto=full -c "$src" -o "$obj.tmp"
      mv "$obj.tmp" "$obj"
    fi
    objects+=("$obj")
  done
  asm="/o/$cpu/clicks-align$align.o"
  if [[ ! -s "$asm" ]]; then
    "${guard[@]}" "$cc" -fno-pie -DSHOP_KERNEL_ABSOLUTE_TABLES "${asmflags[@]}" -c cpp/pipeline/k_free_clicks.S -o "$asm.tmp"
    mv "$asm.tmp" "$asm"
  fi
  printf '%q ' "$cc" "${common[@]}" -flto=full "${ldflags[@]}" "${objects[@]}" "$asm" -o "/o/$name"; echo
  "${guard[@]}" "$cc" "${common[@]}" -flto=full "${ldflags[@]}" "${objects[@]}" "$asm" -o "/o/$name.tmp"
  mv "/o/$name.tmp" "/o/$name"
  sha256sum "/o/$name"
  readelf -h "/o/$name" | grep -E 'Type:|Machine:'
  if readelf -l "/o/$name" | grep -q 'Requesting program interpreter'; then echo 'unexpected dynamic ELF' >&2;exit 1;fi
done
TRIAL_LINUX_SH
cat > "$work/guard_compile.py" <<'TRIAL_GUARD_COMPILE_PY'
"""Cap per-compiler disk use and stop only its child process group on low host space."""
import json
import os
import shutil
import signal
import subprocess
import sys
import time

floor=1280*1024**2
root="/o"
def free():return shutil.disk_usage(root).free
if free()<floor:raise SystemExit("compiler storage reserve would be exceeded")
start=time.monotonic()
p=subprocess.Popen(sys.argv[1:],start_new_session=True)
lowest=free()
while True:
    pid,status,usage=os.wait4(p.pid,os.WNOHANG)
    if pid:break
    remain=free();lowest=min(lowest,remain)
    if remain<floor:
        os.killpg(p.pid,signal.SIGKILL);os.wait4(p.pid,0);raise SystemExit("stopped compiler to preserve 1280 MiB of scratch volume space")
    time.sleep(0.25)
p.returncode=os.waitstatus_to_exitcode(status)
with open("/o/build-metrics.jsonl","a") as f:
    print(json.dumps({"command":sys.argv[1:],"exit":p.returncode,"elapsed":round(time.monotonic()-start,3),"max_rss_kib":usage.ru_maxrss,"min_avail_bytes":lowest}),file=f)
sys.exit(p.returncode)
TRIAL_GUARD_COMPILE_PY
cat > "$work/gate.c" <<'TRIAL_GATE_C'
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argc;
    char path[4096];
    const ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-6);
    if(n<=0 || n==(ssize_t)sizeof(path)-6) {
        perror("readlink executable");
        return 126;
    }
    const int v4=__builtin_cpu_supports("x86-64-v4");
    if(getenv("SHOP_TRIAL_REPORT")) fprintf(stderr,"[compiler-trial] x86-64-v4=%d\n",v4!=0);
    if(v4) {
        memcpy(path+n,".opt",5);
        execv(path,argv);
        if(errno!=ENOENT) perror("optimized benchmark executable");
    }
    memcpy(path+n,".base",6);
    execv(path,argv);
    perror("portable benchmark executable");
    return 126;
}
TRIAL_GATE_C
cat > "$work/Dockerfile" <<'TRIAL_DOCKERFILE'
FROM debian:bookworm-slim@sha256:734a72b3d6142a71993b4bc580ca01d8ca687f77a2c422d96a961d3376b179bc AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ clang-16=1:16.0.6-15~deb12u1 lld-16=1:16.0.6-15~deb12u1 binutils python3 && rm -rf /var/lib/apt/lists/*
COPY source/ /trial/source/
COPY linux.sh guard_compile.py gate.c /trial/
ARG VARIANTS
RUN mkdir /o && bash /trial/linux.sh compile $VARIANTS
FROM scratch AS binaries
COPY --from=build /o/ /
TRIAL_DOCKERFILE
if [[ -n "${SHOP_TRIAL_CONTEXT_TAR:-}" ]]; then
  tar -czf "$SHOP_TRIAL_CONTEXT_TAR" -C "$work" source Dockerfile linux.sh guard_compile.py gate.c
  exit 0
fi
docker build --platform linux/amd64 --build-arg "VARIANTS=$*" --target binaries --output "type=local,dest=$output" "$work"
for v in "$@"; do [[ "$v" == guard ]] && v=shopping; sha256sum "$output/$v"; done
