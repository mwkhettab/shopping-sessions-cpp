"""Isolate actual release callbacks and their two destination writes on one native runner."""
from pathlib import Path
import json,os,re,shutil,statistics,subprocess,tempfile,time
modes=[('structure',-1),('validated',0),('raw_only',1),('revisions_only',2),('both_writes',3)]

def specialized(original,mode):
    s=original
    def replace(old,new):
        nonlocal s
        if s.count(old)!=1:raise ValueError('unexpected source anchor '+old[:65])
        s=s.replace(old,new,1)
    # Every semantic mode uses the same checked fields and unsigned digests.
    # Additional per-shard counters give revision-only mode the same raw locations.
    replace('  std::vector<Worker> workers(threads);','''  struct alignas(64) Measurements {
    std::array<size_t,shards> positions{};
    uint64_t n{},lex{},direct{},valid{},ids{},times{},money{};
  };
  std::vector<Measurements> measurements(threads);
  std::vector<Worker> workers(threads);''')
    replace('  const Time anchor = window.start / 1000;','  const Time anchor = window.start / 1000;\n  const auto input_start=Timer::Clock::now();')
    replace('             auto put =','             auto &measure=measurements[t];\n             auto put =')
    replace('               uint64_t location = 0;','''               measure.ids+=id.hash()+user.hash();
               measure.times+=uint64_t(ingest)+uint64_t(e.ts);
               measure.money+=uint64_t(e.price)+uint64_t(e.category);
               measure.valid+=bool(user.bits);
               uint64_t location = 0;''')
    replace('location = uint64_t(rows.size()) * shards + b + 1;', 'location=uint64_t(measure.positions[b]++)*shards+b+1;')
    replace('                 rows.emplace_back(Key{user.bits}, layout.pack(e));','                 if constexpr(PROBE_RAW)rows.emplace_back(Key{user.bits}, layout.pack(e));')
    replace('               Time seconds = ingest / 1000;','               if constexpr(!PROBE_REV)return;\n               Time seconds = ingest / 1000;')
    replace('               Time ingest = ingest_time(r[3]);','''               ++measure.n;
               measure.lex+=r[0].size()+r[1].size()+r[2].size()+r[3].size();
               if constexpr(PROBE_STRUCTURE)return;
               Time ingest = ingest_time(r[3]);''')
    replace('               Time ingest = ingest_time(arrival), ts = timestamp(event_time);','''               ++measure.n;++measure.direct;
               measure.lex+=id_width+user_width+event_time.size()+arrival.size();
               if constexpr(PROBE_STRUCTURE)return true;
               Time ingest = ingest_time(arrival), ts = timestamp(event_time);''')
    anchor='  timer.mark("concurrent input, raw user partition and revision index");'
    replace(anchor,'''  const auto input_end=Timer::Clock::now();
  Measurements sum;
  for(const auto &d:measurements){
    sum.n+=d.n;sum.lex+=d.lex;sum.direct+=d.direct;sum.valid+=d.valid;
    sum.ids+=d.ids;sum.times+=d.times;sum.money+=d.money;
  }
  std::cout<<"SHOP_INPUT_COST "<<std::chrono::duration<double>(input_end-input_start).count()
           <<" "<<sum.n<<" "<<sum.lex<<" "<<sum.direct<<" "<<sum.valid<<" "<<sum.ids<<" "<<sum.times<<" "<<sum.money<<"\\n";
  return;
'''+anchor)
    return (f'#define PROBE_RAW {int(mode>=0 and bool(mode&1))}\n#define PROBE_REV {int(mode>=0 and bool(mode&2))}\n#define PROBE_STRUCTURE {int(mode<0)}\n'+s)

def measure(source,data,work,compiler,env,pgo,train=None):
    programs={}
    input_text=(source/'cpp/main.cpp').read_text()
    for name,mode in modes:
        root=work/name;root.mkdir();shutil.copytree(source/'cpp',root/'cpp')
        (root/'cpp/main.cpp').write_text(specialized(input_text,mode))
        exe=root/'shopping';flags=[compiler,'-std=c++20','-DNDEBUG','-O3','-pthread','cpp/main.cpp','-o',exe]
        if pgo:
            idx=root/'profiles';subprocess.run(flags+['-fprofile-generate='+str(idx),'-fprofile-update=atomic'],env=env,cwd=root,check=True)
            for form in ('short','wide'):
                subprocess.run([exe,train/form,root/'unused-output'],cwd=root,env=env,check=True,stdout=subprocess.DEVNULL)
            flags+=['-fprofile-use='+str(idx),'-fprofile-correction','-Werror=missing-profile']
        subprocess.run(flags,cwd=root,env=env,check=True);programs[name]=exe
    # One pass before each rotation makes all variants use the same warm input.
    results={name:[] for name in programs};digests={};order=list(programs)
    for r in range(-1,5):
        buf=bytearray(1<<20)
        for f in data.rglob('*.csv'):
            with f.open('rb',buffering=0) as stream:
                while stream.readinto(buf):pass
        for name in order[r%len(order):]+order[:r%len(order)]:
            start=time.monotonic()
            out=subprocess.check_output([programs[name],data,work/'unused-output'],env=env,text=True)
            process=time.monotonic()-start
            match=re.search(r'^SHOP_INPUT_COST ([0-9.eE+-]+) ([0-9 ]+)$',out,re.M)
            if not match:raise ValueError(out[-500:])
            digest=tuple(map(int,match[2].split()))
            if digests.setdefault(name,digest)!=digest:raise ValueError('non-deterministic callback digest '+name)
            if r>=0:results[name].append((float(match[1]),process))
    for name,digest in digests.items():
        if digest[:3]!=digests['structure'][:3]:raise ValueError('different row boundaries or direct callback counts')
        if name!='structure' and digest!=digests['both_writes']:raise ValueError('different decoded IDs, values or event times')
    return results,digests

def collect(root,area,note,run,env):
    # The control's measured C++ source and its PGO training source are known.
    source=area/'shipresident-pgo'
    if not source.is_dir():return
    train=area/'isolated-input-training'
    if not train.is_dir():run(os.sys.executable,root/'perf/native/train.py',train)
    with tempfile.TemporaryDirectory(dir=area) as temp:
        results,digests=measure(source,area/'practice',Path(temp),'g++-12',env,True,train)
    path=area/'results'/'isolated-input.json';path.write_text(json.dumps({'observations':results,'digests':digests}))
    for name,rows in results.items():
        note('Isolated release input '+name,f'20M, input phase {statistics.median(r[0] for r in rows):.4f} s, full probe {statistics.median(r[1] for r in rows):.4f} s, input rotations {[round(r[0],4) for r in rows]}.')
    d=digests['both_writes'];note('Isolated release digests',f'All modes matched {d[0]} rows, {d[1]} checked field bytes and {d[2]} direct callbacks; all semantic modes agreed on {d[3]} valid raw events and the complete key, time and value checksums. Clicks and links run normally throughout.')
