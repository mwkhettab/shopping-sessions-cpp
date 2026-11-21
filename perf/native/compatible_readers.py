import collections,re,subprocess
def summarize(binary):
    raw=subprocess.check_output(['objdump','-d','-C','--no-show-raw-insn',binary],text=True)
    active=False;counts=collections.Counter();functions=0
    for line in raw.splitlines():
        symbol=re.match(r'^\s*[0-9a-f]+ <(.*)>:$',line)
        if symbol:
            active='ordinal16_wide<' in symbol[1]
            functions+=active
            continue
        if active:
            instr=re.match(r'^\s*[0-9a-f]+:\s+([a-z0-9.]+)',line)
            if instr:counts[instr[1]]+=1
    if not functions:raise ValueError('missing the checked wide event reader in the release binary')
    incompatible=sorted(x for x in counts if x in ('vpcompressb','vpcompressw','vpexpandb','vpexpandw','vpermb','vpermt2b','vpermi2b','vpmultishiftqb'))
    if incompatible:raise ValueError('unexpected VBMI/VBMI2 instructions '+str(incompatible))
    useful={k:v for k,v in counts.items() if k in ('vpcompressd','vpmovdb','vpgatherqq','tzcnt','pext','vpcmpeqb','vpcmpneqd','vpcmpb')}
    return f'{functions} wide reader symbols, {dict(sorted(useful.items()))}. No VBMI or VBMI2 instructions from the audited opcode set.'
