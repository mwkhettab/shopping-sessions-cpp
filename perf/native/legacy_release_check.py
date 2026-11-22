"""Reproduce both earlier table fingerprints and time the previously scored input layout."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from port_acceptance import NATIVE, generate_parts, sha, store
from port_lifetime import paired_lifetime

area = Path(os.environ['RUNNER_TEMP']) / 'shopping-trials'
results = area / 'results' / 'port-acceptance'
state = json.loads((results / 'status.json').read_text())
hardware = json.loads((results / 'hardware.json').read_text())
cpus = hardware['affinity']
os.sched_setaffinity(0, cpus)
env = dict(os.environ, NCPU=str(len(cpus)), POLARS_MAX_THREADS='2', ARROW_NUM_THREADS='2')
images = {label: state['implementations'][label] for label in ('baseline', 'candidate')}
with tempfile.TemporaryDirectory(dir=area, prefix='legacy-') as temporary:
    root = Path(temporary)
    with zipfile.ZipFile(area / 'generator.zip') as bundle:
        bundle.extractall(root)
    gen = root / 'generator'
    subprocess.run(['patch', '-p1', '-i', NATIVE / 'legacy-generator.patch'], cwd=gen, check=True)
    hashes = json.loads((NATIVE / 'legacy-generator-files.json').read_text())
    assert all(sha(gen / path) == digest for path, digest in hashes.items())
    subprocess.run(['patch', '-p1', '-i', NATIVE / 'generator-memory.patch'], cwd=gen, check=True)
    subprocess.run(['cargo','build','--release','--locked','--offline'], cwd=gen, check=True)
    generator = gen / 'target/release/generate'
    for scale, seed, rows, pairs in [('20M', 23, 20_000_000, 7), ('60M', 47, 60_000_000, 5)]:
        with tempfile.TemporaryDirectory(dir=root) as test:
            here = Path(test)
            source = here / 'data'
            generate_parts(generator, seed, rows, rows // 4, source, results / ('legacy-'+scale+'-generate.log'), env)
            fingerprints = {}
            for tag, item in images.items():
                out = here / tag
                run = subprocess.run([item['executable'], source, out], env=dict(env, XL_TIMING='1', V1ND_LOG='1'),
                                     text=True, capture_output=True, check=True)
                marker = 'parse=' if tag == 'baseline' else '[sharded] completed'
                assert marker in run.stderr and '[pipeline] fallback:' not in run.stderr, run.stderr
                check = [sys.executable, NATIVE / 'acceptance_reference.py', out, '--scale', scale,
                         '--expected', NATIVE / 'acceptance-references.json']
                fingerprints[tag] = json.loads(subprocess.check_output(check, env=env, text=True))
                shutil.rmtree(out)
            assert fingerprints['baseline'] == fingerprints['candidate']
            times = paired_lifetime(images, source, area, results, cpus, hardware['memory_mib'], 'Original '+scale, pairs)
            data = {'tables':fingerprints['candidate'], 'lifetime':times}
            store(results / ('legacy-'+scale+'.json'), data)
            print('::notice title=Original '+scale+' compatibility::'+json.dumps(data), flush=True)
