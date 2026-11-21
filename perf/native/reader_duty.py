from pathlib import Path


def add_timing(source):
    anchor='  const Time anchor = window.start / 1000;'
    if anchor not in source:raise ValueError('unexpected reader input setup')
    source=source.replace(anchor,anchor+'''
  struct alignas(64) ReaderDuty {std::array<int64_t,3> nanos{};std::array<size_t,3> jobs{};};
  std::vector<ReaderDuty> duty(threads);
  const bool trace_jobs=std::getenv("SHOP_PHASE_TRACE");
  struct DutyClock {
    ReaderDuty &worker;unsigned kind;bool enabled;int64_t initial;
    static int64_t time(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return int64_t(t.tv_sec)*1000000000+t.tv_nsec;}
    DutyClock(ReaderDuty &w,unsigned k,bool active):worker(w),kind(k),enabled(active),initial(active?time():0){}
    ~DutyClock(){if(enabled){worker.nanos[kind]+=time()-initial;++worker.jobs[kind];}}
  };
''',1)
    import re
    pattern=r'(\[&\]\(size_t job, unsigned t\) \{\n)([ ]+)(if \(job < links.size\(\)\))'
    def duty(match):
        return match[1]+match[2]+'DutyClock input_work(duty[t],job<links.size()?0:job<links.size()+events.size()?1:2,trace_jobs);\n'+match[2]+match[3]
    source,count=re.subn(pattern,duty,source,count=1)
    assert count==1
    anchor='  timer.mark("concurrent input, raw user partition and revision index");'
    assert anchor in source
    return source.replace(anchor,anchor+'''
  if(trace_jobs){
    for(unsigned k=0;k<3;++k){
      int64_t total=0;size_t jobs=0;
      for(const auto &worker:duty){total+=worker.nanos[k];jobs+=worker.jobs[k];}
      std::cerr<<"SHOP_INPUT "<<std::array{"links","events","clicks"}[k]<<" "<<total<<" "<<jobs<<"\\n";
    }
  }
''',1)


