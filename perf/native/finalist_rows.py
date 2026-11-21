"""Run boundary and unwind tests against the shipping Clang objects inside their original builder."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys

source, release = map(lambda p:Path(p).resolve(),sys.argv[1:])
tests = source / 'tests/pipeline'
enabled = [name for name in ('paired_event_rows','paired_click_rows') if (tests/(name+'.cpp')).is_file()]
if not enabled:
    raise SystemExit(0)
expected = hashlib.sha256(release.read_bytes()).hexdigest()
image = 'shopping-finalist-objects:'+os.environ['GITHUB_RUN_ID']
subprocess.run(['docker','build','--platform','linux/amd64','--target','build','-t',image,source],check=True)
stage = '''
set -eu
test "$(sha256sum build/shopping | cut -d' ' -f1)" = "$SHIPPING_SHA256"
for suite in "$@"; do
    set -- -std=c++20 -O3 -DNDEBUG -pthread -fno-pic -fno-pie -march=x86-64 -mtune=cascadelake \\
        -fconstexpr-steps=100000000 -flto=full -DSHOP_KERNEL_ABSOLUTE_TABLES -Wno-return-type -Icpp/pipeline -I/checks
    if test -f build/event_pair.o; then set -- "$@" -DSHOP_PAIRED_EVENT_KERNEL build/event_pair.o; fi
    if test "$suite" = paired_event_rows; then
        set -- "$@" -DWATCH_PAIRED_ABI -DREQUIRE_NATIVE_EVENT_KERNEL
        for sym in loop flush redo purchase; do set -- "$@" "-Wl,--wrap=shopping_kernel_event_$sym"; done
    fi
    clang++-16 "$@" "/checks/$suite.cpp" build/k_free.bc build/k_free_x86.bc build/clicks.o \\
        -static -no-pie -fuse-ld=lld-16 -Wl,--lto-O3 -Wl,--threads=1 -o "/tmp/$suite"
    status=0
    CHECK_SCALAR_FALLBACK=1 "/tmp/$suite" || status=$?
    if test "$status" = 77; then echo "$suite skipped on this CPU"; elif test "$status" != 0; then exit "$status"; fi
done
'''
subprocess.run(['docker','run','--rm','--platform','linux/amd64','--network','none','--read-only',
                '--tmpfs','/tmp:rw,exec,size=1g','--env','SHIPPING_SHA256='+expected,'-v',f'{tests}:/checks:ro',
                '--entrypoint','/bin/sh',image,'-ec',stage,'row-suites',*enabled],check=True)
print('::notice title=Release parser objects::Shipping ELF verified against build stage. Executed native or supported portable row checks for '+', '.join(enabled)+'.')
