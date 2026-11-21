"""Check both generic tables, before encoding, under serial and dynamic scheduling."""
import csv
import datetime
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
fixtures = Path(sys.argv[1])
epoch = datetime.date(1970, 1, 1)

def days(s):
    return str((datetime.date.fromisoformat(s) - epoch).days)

def hexbytes(s):
    return s.encode("utf-8").hex()

def expect(folder):
    result = []
    for table in ("user_daily", "campaign_daily"):
        with (folder / (table + ".csv")).open(newline="") as f:
            rows = csv.reader(f)
            next(rows)
            for row in rows:
                if table == "user_daily":
                    name, date, *nums, cat = row
                    result.append("\t".join(("U", hexbytes(name), days(date), *nums, hexbytes(cat) if cat else "-")))
                else:
                    name, channel, date, *nums = row
                    result.append("\t".join(("C", hexbytes(name), hexbytes(channel), days(date), *nums)))
    return sorted(result)

with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "rows"
    subprocess.run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O2", "-pthread",
                    "-I" + str(root / "cpp/pipeline"), str(root / "tests/pipeline/kernel_generic_rows.cpp"),
                    "-o", str(exe)], check=True)
    cases = sorted(c for c in fixtures.iterdir() if c.is_dir())
    assert cases, "no fixtures found"
    for case in cases:
        data = case / "input"
        if not data.is_dir():
            data = case / "data"
        expected = expect(case / "expected")
        for workers in (1, 3):
            result = subprocess.run([exe, data, str(workers)], check=True, capture_output=True, text=True)
            actual = sorted(result.stdout.splitlines())
            assert actual == expected, (case.name, workers, actual, expected)
    print(f"Both generic tables match all {len(cases)} published fixtures at 1 and 3 workers.")
    if len(sys.argv) == 4:
        expected = expect(Path(sys.argv[3]))
        result = subprocess.run([exe, sys.argv[2], "4"], check=True, capture_output=True, text=True)
        actual = sorted(result.stdout.splitlines())
        if actual != expected:
            raise ValueError(f"generic reference mismatch, {len(actual)} rows vs {len(expected)} expected")
        users = sum(row.startswith("U\t") for row in actual)
        print(f"Both 1M-event tables match, including {users} user days and {len(actual)-users} campaign days.")
