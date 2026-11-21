"""Trace per-stage launch, available CPU, and last-job drain on the release source."""
from pathlib import Path
import json,os,re,shutil,statistics,subprocess,tempfile
def instrument(source):
    a=source.index('template <class F> void parallel(')
    code=source[a:]
    def once(old,new):
        nonlocal code
        if code.count(old)!=1:raise ValueError('unexpected parallel source '+old[:80])
        code=code.replace(old,new)
    once('  std::atomic<size_t> next{0};','''  std::atomic<size_t> next{0};
  auto wall=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};
  auto cpu_now=[](){timespec c{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&c);return int64_t(c.tv_sec)*1000000000+c.tv_nsec;};
  struct alignas(64) Load {int64_t first{},end{},cpu{},longest{};size_t count{};};
  std::vector<Load> sampled(std::min<size_t>(threads,jobs));
  const auto before=wall();''')
    once('    workers.emplace_back([&, t] {','''    workers.emplace_back([&, t] {
      auto &sample=sampled[t];sample.first=wall();const auto first_cpu=cpu_now();''')
    once('          f(i, t);','''          auto begin_task=wall();
          f(i, t);
          sample.longest=std::max<int64_t>(sample.longest,wall()-begin_task);
          ++sample.count;''')
    once('      load.cpu = metrics::thread_cpu() - cpu;\n#endif','''      load.cpu = metrics::thread_cpu() - cpu;
#endif
      sample.cpu=cpu_now()-first_cpu;sample.end=wall();''')
    once('  workers.clear();','''  workers.clear();
  if(!sampled.empty()){
    auto after=wall();int64_t first=sampled[0].first,last=0,longest=0,launch=0,drain=0,cpu=0;
    for(const auto &v:sampled){first=std::min(first,v.first);last=std::max(last,v.end);longest=std::max(longest,v.longest);cpu+=v.cpu;}
    for(const auto &v:sampled){launch+=v.first-first;drain+=last-v.end;}
    std::cerr<<"SHOP_TEAM "<<jobs<<" "<<sampled.size()<<" "<<after-before<<" "<<first-before<<" "<<after-last<<" "<<launch<<" "<<drain<<" "<<longest<<" "<<cpu<<"\\n";
  }''')
    return source[:a]+code
def execute(root,source,scales,compiler,env,pgo,run,note,results_dir=None):
    with tempfile.TemporaryDirectory(dir=source.parent) as d:
        work=Path(d);shutil.copytree(source/'cpp',work/'cpp')
        p=work/'cpp/input.hpp';p.write_text(instrument(p.read_text()))
        if pgo:
            (work/'perf').mkdir()
            shutil.copy2(root/'perf/native/train.py',work/'perf/train.py')
            shutil.copy2(root/'perf/native/image-pgo-build.sh',work/'build.sh')
            run('sh','build.sh',cwd=work,extra={'CXX':compiler,'PGO':'1'})
            binary=work/'build/shopping'
        else:
            binary=work/'shopping';subprocess.run([compiler,'-O3','-DNDEBUG','-std=c++20','-pthread',work/'cpp/main.cpp','-o',binary],check=True,env=env)
        all_samples={}
        for label,data,ref in scales:
            series=[]
            for cycle in range(3):
                with tempfile.TemporaryDirectory(dir=work) as output:
                    proc=subprocess.run([binary,data,output],env=env|{'PROFILE':'1'},capture_output=True,text=True,check=True)
                    if cycle==0:
                        check=root/'perf/native/compare_large.py'
                        subprocess.run([os.sys.executable,check,output,ref],check=True,env=env,stdout=subprocess.DEVNULL)
                    entries=[tuple(map(int,line.split()[1:])) for line in proc.stderr.splitlines() if line.startswith('SHOP_TEAM ')]
                    if len(entries)!=8:raise ValueError(f'Expected 8 release teams, got {len(entries)}: {proc.stderr[-1200:]}')
                    series.append(entries)
            all_samples[label]=series
            labels=['input','dedup','open outputs','group aliases','union edges','attach aliases','linked sessions','close outputs']
            records=[]
            for i,stage in enumerate(labels):
                rows=[x[i] for x in series]
                wall=statistics.median(x[2]/1e6 for x in rows)
                # Drain times are summed over workers, then expressed as
                # available elapsed time per worker; job times are wall-clock.
                launch=statistics.median((x[3]+x[4]+x[5]/x[1])/1e6 for x in rows)
                drain=statistics.median(x[6]/x[1]/1e6 for x in rows)
                largest=statistics.median(x[7]/1e6 for x in rows)
                util=statistics.median(x[8]/(x[2]*x[1]) for x in rows)
                records.append(f'{stage} wall {wall:.1f}, launch/join {launch:.1f}, drain {drain:.1f}, longest job {largest:.1f} ms, CPU {100*util:.1f}%')
            note('Per-worker tail '+label,' / '.join(records))
        if results_dir:(results_dir/'worker-tails.json').write_text(json.dumps(all_samples))
