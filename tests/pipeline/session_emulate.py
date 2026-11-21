"""Run the dense x86 algorithm on another ISA, using SIMDe test headers."""
import argparse
import os
from pathlib import Path
import re
import platform
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PIPE = HERE.parents[1] / 'cpp/pipeline'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('simde', type=Path, help='checkout containing simde/x86/avx512.h')
    ap.add_argument('--sanitize', action='store_true')
    opts = ap.parse_args()
    simde = opts.simde.resolve()
    if not (simde / 'simde/x86/avx512.h').is_file():
        ap.error('SIMDe headers missing')
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp)
        header = (PIPE / 'events_fast.hpp').read_text()
        header = re.sub(r'inline bool fast_cpu\(\) \{.*?\n\}', 'inline bool fast_cpu() { return true; }', header, flags=re.S)
        a = header.index('#if defined(__x86_64__)')
        b = header.index('namespace pipeline::events {', a)
        header = header[:a] + '''
#define PIPELINE_EVENTS_NATIVE 1
#define PIPELINE_EV_ATTR
#include "session_simde.hpp"
''' + header[b:]
        a = header.index('inline __m256i mul32_256(')
        b = header.index('inline constexpr double NAME_C', a)
        header = header[:a] + 'inline __m256i mul32_256(__m256i a,__m256i b){return _mm256_mul_epu32(a,b);}\n' + header[b:]
        header = header[:header.rindex('#if defined(__clang__)')]
        for intrinsic in re.findall(r'\bsession(_mm\w+)\(', (HERE / 'session_simde.hpp').read_text()):
            header = re.sub(r'\b'+intrinsic+r'\b', 'session'+intrinsic, header)
        (path / 'events_fast.hpp').write_text(header)
        source = (HERE / 'session_runs.cpp').read_text().replace('../../cpp/pipeline/', '')
        (path / 'run.cpp').write_text(source)
        sanitizer = 'undefined' if platform.system() == 'Darwin' else 'address,undefined'
        flags = ['-O1', '-g', '-fsanitize='+sanitizer, '-fno-sanitize-recover=all', '-fno-omit-frame-pointer'] if opts.sanitize else ['-O2']
        subprocess.run([os.getenv('CXX', 'c++'), '-std=c++20', *flags, '-Wno-psabi', '-Wno-pass-failed',
                        '-DSESSION_TEST_FAST', '-I', str(path), '-I', str(PIPE), '-I', str(HERE), '-I', str(simde),
                        str(path/'run.cpp'), '-o', str(path/'run')], check=True)
        for pattern in range(5):
            subprocess.run([str(path/'run'), str(pattern)], check=True, timeout=45)


if __name__ == '__main__':
    main()
