"""Exercise automatic routing with fresh and already populated output directories."""
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


def check(binary, starter):
    fixture = starter / "fixtures" / "A1"
    compare = starter / "scripts" / "compare.py"
    env = dict(os.environ, NCPU="2", V1ND_LOG="1")
    with TemporaryDirectory() as temp:
        output = Path(temp)
        for trace in ("[sharded] completed", "[v1nd]"):
            proc = subprocess.run([binary, fixture / "input", output], env=env,
                                  check=True, text=True, capture_output=True)
            assert trace in proc.stderr, proc.stderr
            subprocess.run([sys.executable, compare, output, fixture / "expected"], check=True)
            if trace.startswith("[sharded]"):
                (output / "user_daily" / "stale.parquet").write_bytes(b"stale")
                (output / "user_daily" / "_SUCCESS").touch()
        assert not (output / "user_daily" / "stale.parquet").exists()
        assert (output / "user_daily" / "_SUCCESS").is_file()


if __name__ == "__main__":
    check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
