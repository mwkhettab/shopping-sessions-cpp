"""Check page and footer bytes for every key width, output method and integer plan."""
import argparse
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path, help="directory containing pqw.rs")
args = parser.parse_args()
with TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    rust = tmp / "oracle.rs"
    rust.write_text((args.source / "pqw.rs").read_text() + "\n" + (here / "parquet_oracle.rs").read_text())
    subprocess.run([os.getenv("RUSTC", "rustc"), "--edition=2021", "-O", rust, "-o", tmp / "rust"], check=True)
    subprocess.run([os.getenv("CXX", "c++"), "-std=c++20", "-O2", here / "parquet.cpp", "-o", tmp / "cpp"], check=True)
    for n in (0, 1, 257, 20021):
        for exe in ("cpp", "rust"):
            subprocess.run([tmp / exe, tmp, str(n)], check=True)
        expected = (tmp / "reference.parquet").read_bytes()
        for j in range(3):
            assert (tmp / f"{j}.parquet").read_bytes() == expected, (n, j)
        assert (tmp / "packed.bin").read_bytes() == (tmp / "reference.bin").read_bytes()
        print(f"{n} rows: three files match all {len(expected):,} bytes")
