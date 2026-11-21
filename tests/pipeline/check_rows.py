"""Compare all parsed row fields, packed values and byte offsets for two kernels."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from row_corpus import build

HERE = Path(__file__).resolve().parent


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument("rust_src", type=Path, help="Directory containing iface.rs and k_ref.rs")
    parser.add_argument("fixtures", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        build(args.fixtures, work)
        for name, blob in (("iface.rs","b7aaade57802bdea66392bfd1a1e3c7fee69d9e1"),
                           ("k_ref.rs","9a32cab4b5ffc59ff2a198637884090abd96e714")):
            data = (args.rust_src/name).read_bytes()
            header = f"blob {len(data)}".encode()+bytes(1)
            if hashlib.sha1(header+data).hexdigest() != blob:
                raise ValueError(f"Unexpected module revision: {name}")
            (work/name).write_bytes(data)
        shutil.copy2(HERE/"rows_oracle.rs", work)
        subprocess.run(["rustc","--edition=2021","-Awarnings","-O",work/"rows_oracle.rs","-o",work/"reference"],check=True)
        subprocess.run([os.environ.get("CXX","c++"),"-O2","-std=c++20",HERE/"rows.cpp","-o",work/"candidate"],check=True)
        for name, row_bytes in (("events",136),("clicks",80)):
            args = [name,work/(name+".csv")]
            ref = subprocess.check_output([work/"reference",*args])
            got = subprocess.check_output([work/"candidate",*args])
            if ref != got:
                raise ValueError(f"{name}: difference in parsed offsets, row values or packed columns")
            print(f"{name}: {len(ref)//row_bytes} records matched byte for byte")


if __name__ == "__main__":
    run()
