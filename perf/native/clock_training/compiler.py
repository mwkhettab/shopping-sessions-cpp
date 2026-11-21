#!/usr/bin/python3
"""Thin wrapper. Its /tmp/.../clang++-16 basename selects the shipping LTO build."""
import json
import os
import sys

args = sys.argv[1:]
phase = os.environ["SHOP_PHASE"]
profile = "/tmp/shop-profile/use.profdata"
if any(arg.endswith(".S") for arg in args):
    extra = []
elif phase == "generate":
    extra = ["-fprofile-instr-generate", "-fprofile-update=atomic"]
    if "-c" not in args:
        extra += ["/tmp/shop-profile/gate.o", "-Wl,--wrap=_exit", "-Wl,--wrap=execv"]
elif phase == "use":
    extra = ["-fprofile-instr-use=" + profile, "-Werror=profile-instr-out-of-date"]
else:
    raise SystemExit(f"unexpected phase {phase}")
if os.environ["SHOP_DATE"] == "1" and not any(arg.endswith(".S") for arg in args):
    extra += ["-DSHOP_SHARED_EVENT_DATE"]
with open("/build-proof/commands.jsonl", "a") as log:
    print(json.dumps({"phase": phase, "shipping": args, "added": extra}), file=log)
os.execv("/usr/bin/clang++-16", ["clang++-16", *args, *extra])
