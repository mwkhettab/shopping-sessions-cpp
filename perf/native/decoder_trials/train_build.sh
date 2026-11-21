#!/bin/sh
# Run inside the stock release's fresh Debian/GCC build stage.
set -eu
work=/tmp/shop-profile
mkdir -p "$work/profile" /build-proof
timed() {
    python3 - "$@" <<'PY'
import json,resource,subprocess,sys,time
label,*cmd=sys.argv[1:]
start=time.monotonic()
print("Starting",label,flush=True)
status=subprocess.run(cmd).returncode
row={"step":label,"seconds":round(time.monotonic()-start,3),"peak_child_kib":resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,"status":status}
with open("/build-proof/steps.jsonl","a") as out: print(json.dumps(row,sort_keys=True),file=out)
print(row,flush=True)
sys.exit(status)
PY
}
timed generate-inputs python3 /training/train.py generate "$work/data"
g++ -std=c++20 -O2 -pthread -c /training/training_gate.cpp -o "$work/gate.o"
cat > "$work/compiler" <<'COMPILER'
#!/bin/sh
set -eu
case "$SHOP_PGO_STEP" in
    generate)
        exec g++ "$@" -fprofile-generate=/tmp/shop-profile/profile -fprofile-update=atomic -ftest-coverage \
            /tmp/shop-profile/gate.o -Wl,--wrap=_exit -Wl,--wrap=execv \
            -Wl,--wrap=_ZN8pipeline6k_free6detail9availableEv ;;
    use)
        exec g++ "$@" -fprofile-use=/tmp/shop-profile/profile -fprofile-correction -fprofile-partial-training \
            -Werror=missing-profile -Werror=coverage-mismatch ;;
esac
exit 2
COMPILER
chmod +x "$work/compiler"
timed compile-instrumented env SHOP_PGO_STEP=generate STATIC=1 CXX="$work/compiler" sh build.sh
timed train python3 /training/train.py train "$work/data" --binary build/shopping
python3 /training/audit_profiles.py "$work"
timed compile-with-profile env SHOP_PGO_STEP=use STATIC=1 CXX="$work/compiler" sh build.sh
nm build/shopping > "$work/final-symbols"
if grep -E '(__wrap__exit|__wrap_execv|__wrap__ZN8pipeline6k_free|__gcov_dump)' "$work/final-symbols"; then
    echo "Training instrumentation leaked into the output ELF" >&2
    exit 1
fi
timed verify-trained-outputs python3 /training/train.py check "$work/data" --binary build/shopping
cp "$work/proof.json" "$work/data/check.json" /build-proof/
sha256sum build/shopping > /build-proof/final.sha256
