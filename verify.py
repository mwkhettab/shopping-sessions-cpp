"""Run the supplied fixtures and compare a generated seed-1 dev set to its published answer."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("dev", type=Path)
parser.add_argument("--starter", type=Path, default=root / "starter-pyspark")
parser.add_argument("--answer", type=Path, default=root / "dev-answer")
args = parser.parse_args()
binary = args.binary.resolve()
scripts = args.starter / "scripts"
subprocess.run([sys.executable, scripts / "run_fixtures.py", args.starter / "fixtures", "--", binary], check=True)
subprocess.run([sys.executable, root / "tests/catalog.py", binary], check=True)
subprocess.run([sys.executable, root / "tests/pipeline/long_link.py", binary], check=True)
with tempfile.TemporaryDirectory() as out:
    subprocess.run([binary, args.dev, out], check=True)
    subprocess.run([sys.executable, scripts / "compare.py", out, args.answer], check=True)
