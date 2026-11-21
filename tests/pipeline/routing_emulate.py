"""Exercise x86 routing masks, shuffles and sorting networks on non-x86 hosts with SIMDe."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PIPE = HERE.parents[1] / 'cpp/pipeline'


def main():
    args = argparse.ArgumentParser()
    args.add_argument('simde', type=Path, help='checkout containing simde/x86/avx512.h')
    args.add_argument('--sanitize', action='store_true')
    opts = args.parse_args()
    replacements = re.findall(r'\b(?:session|routing)(_mm\w+)\(', (HERE / 'session_simde.hpp').read_text() + (HERE / 'routing_simde.hpp').read_text())
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        for header in ['parse_simd.hpp', 'ckix.hpp', 'parse_packets.hpp', 'parse_sink.hpp', 'parse_links.hpp', 'links_sort.hpp']:
            s = (PIPE / header).read_text().replace('#if defined(__x86_64__)', '#if 1')
            if header == 'parse_simd.hpp':
                s = re.sub(r'#include <immintrin.h>\n#define PIPELINE_ROUTING_X64[^\n]+\n#define PIPELINE_ROUTING_INLINE[^\n]+', '#include "routing_simde.hpp"\n#define PIPELINE_ROUTING_X64\n#define PIPELINE_ROUTING_INLINE inline', s)
                s = re.sub(r'inline bool routing_x64\(\) \{.*?\n\}', 'inline bool routing_x64(){return true;}', s, flags=re.S)
                s = re.sub(r'auto result=_mm512_setzero_si512\(\);.*?return result;', 'return mul8(x,y);', s, flags=re.S)
            s = s.replace('__builtin_cpu_supports("avx512ifma")', 'false')
            for intrinsic in replacements:
                prefix = 'routing' if f'routing{intrinsic}(' in (HERE/'routing_simde.hpp').read_text() else 'session'
                s = re.sub(r'\b'+intrinsic+r'\b', prefix+intrinsic, s)
            (tmp/header).write_text(s)
        for fn in ['routing_packets', 'routing_sinks', 'routing_sort']:
            source = HERE/(fn+'.cpp')
            flags = ['-O1', '-fsanitize=undefined', '-fno-sanitize-recover=all'] if opts.sanitize else ['-O2']
            cmd = [os.getenv('CXX','c++'),'-std=c++20',*flags,'-Wno-psabi','-Wno-pass-failed','-I',str(tmp),'-I',str(PIPE),'-I',str(HERE),'-I',str(opts.simde.resolve()),str(source),'-o',str(tmp/fn)]
            subprocess.run(cmd,check=True)
            subprocess.run([str(tmp/fn)],check=True,timeout=60)


if __name__ == '__main__':
    main()
