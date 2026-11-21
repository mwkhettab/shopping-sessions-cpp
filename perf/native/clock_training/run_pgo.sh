#!/bin/bash
set -euo pipefail
label=$1
rev=$2
out=$3
here=$(cd -- "$(dirname -- "$0")" && pwd)
options=()
case "$label" in
    llvm-pgo-date) options+=(--date-patch "$here/shared-event-date.patch");;
    llvm-pgo) ;;
    *) exit 2;;
esac
log="$out/$label.log"
if ! python3 "$here/build_pgo.py" .acceptance/candidate "$out/$label" "$rev" "${options[@]}" >"$log" 2>&1; then
    tail -50 "$log"
    python3 - "$log" "$label" <<'PY'
from pathlib import Path
import sys
message = Path(sys.argv[1]).read_text(errors="replace")[-2500:]
print("::warning title=" + sys.argv[2] + " excluded::" + message.replace("%","%25").replace("\r","%0D").replace("\n","%0A"))
PY
    exit 1
fi
python3 - "$out/$label/proof/proof.json" "$label" <<'PY'
from pathlib import Path
import json, sys
proof=json.loads(Path(sys.argv[1]).read_text())
assert proof["wide_reader_observed"]
print("::notice title=" + sys.argv[2] + " training::Native wide reader was instrumented, merged profile " +
      proof["profile_sha256"] + ", uninstrumented release " + proof["release_sha256"])
PY
