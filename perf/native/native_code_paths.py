#!/usr/bin/env python3
"""Qualify a pinned release's Linux x86-64 component paths, including a parked-worker retry."""
import argparse
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile

REV = "b5f19ac6cc222f166ae9581a8cef9b87bb3c4263"
MODULES = ("csv", "routing", "sessions", "paired", "parked", "release")
VARIANTS = {
    "control": (), "dictionary": ("DIRECT_DICT_LOOPS",), "memory": ("CUSTOM_MEMORY",),
    "paired": ("PAIRED_CLICK_SORT",), "paired-memory": ("PAIRED_CLICK_SORT","CUSTOM_MEMORY"),
    "paired-memory-layout": ("PAIRED_CLICK_SORT","CUSTOM_MEMORY","INTEL_LAYOUT"),
    "paired-memory-v4": ("PAIRED_CLICK_SORT","CUSTOM_MEMORY","INTEL_LAYOUT","V4_SIDECAR"),
    "complete": ("PAIRED_CLICK_SORT","CUSTOM_MEMORY","INTEL_LAYOUT","V4_SIDECAR","DIRECT_DICT_LOOPS"),
}
PROBE = r'''
#include <cpuid.h>
#include <cstdio>
int main() {
    unsigned a,b,c,d;
    const bool lzcnt=__get_cpuid(0x80000001,&a,&b,&c,&d) && (c&(1u<<5));
    const bool common=__builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi") &&
        __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("popcnt") &&
        __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") &&
        __builtin_cpu_supports("avx512vl") && __builtin_cpu_supports("avx512dq");
    const bool cd=__builtin_cpu_supports("avx512cd"),ifma=__builtin_cpu_supports("avx512ifma");
    const bool v4=__builtin_cpu_supports("x86-64-v4");
    const bool sessions=__builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi2") &&
        __builtin_cpu_supports("popcnt") && __builtin_cpu_supports("avx512f") &&
        __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vl") &&
        __builtin_cpu_supports("avx512dq") && cd;
    std::printf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                common && lzcnt,common,sessions,common && ifma,v4,
                bool(__builtin_cpu_supports("avx2")),bool(__builtin_cpu_supports("avx512f")),
                bool(__builtin_cpu_supports("avx512bw")),bool(__builtin_cpu_supports("avx512vl")),
                bool(__builtin_cpu_supports("avx512dq")),cd,bool(__builtin_cpu_supports("bmi")),
                bool(__builtin_cpu_supports("bmi2")),bool(__builtin_cpu_supports("popcnt")),lzcnt);
}
'''

PARKED = r'''
#include "sys.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>
using namespace pipeline;
using namespace pipeline::sys;
static_assert(sizeof(pid_t)<=sizeof(long));
[[noreturn]] static void error(const char* why) {
    std::fprintf(stderr,"parked worker check failed: %s\n",why);::_exit(2);
}
static bool asleep(long tid) {
    std::ifstream f("/proc/self/task/"+std::to_string(tid)+"/status");
    for(std::string line;std::getline(f,line);)
        if(line.starts_with("State:")) return line.find("S (sleeping)")!=std::string::npos;
    return false;
}
static void parked(const std::array<long,8>& tid, unsigned exclude) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    const unsigned n=7-(exclude!=0);
    while(std::chrono::steady_clock::now()<deadline) {
        if(worker_pool().sleepers.load()>=n) {
            bool all=true;
            for(unsigned i=1;i<8;++i) if(i!=exclude) all &= asleep(tid[i]);
            if(all) return;
        }
        ::usleep(1000);
    }
    error("workers did not reach their sleep path");
}
int main(int argc,char** argv) {
    auto task_count=[] {return std::distance(std::filesystem::directory_iterator("/proc/self/task"),std::filesystem::directory_iterator{});};
    if(argc==4 && std::string_view(argv[1])=="--generic") {
        std::ifstream proof(argv[3]);long pid,from,workers,initial_tasks;
        if(!(proof>>pid>>from>>workers>>initial_tasks) || pid!=::getpid() || workers!=8 || (from!=0 && from!=1)) error("retry proof");
        for(unsigned i=1;i<8;++i) {
            long tid;
            if(!(proof>>tid) || std::filesystem::exists("/proc/self/task/"+std::to_string(tid))) error("old pool TID survived exec");
        }
        const auto now=task_count();
        if(now!=initial_tasks) error("fresh process task count changed");
        if(std::string_view(argv[2])!=(from?"worker":"main")) error("retry argument");
        std::fprintf(stderr,"generic retry pid=%ld previous_workers=%ld remaining_pool_workers=0 initial_tasks=%ld now=%ld from=%ld\n",pid,workers,initial_tasks,long(now),from);
        return 0;
    }
    if(argc!=3) error("arguments");
    const unsigned caller=std::string_view(argv[1])=="worker"?1:0;
    const auto initial_tasks=task_count();
    exec_init(argc,argv,argv[1],argv[2],8);
    SPIN_TICKS.store(0);
    std::array<long,8> tids{};
    pool_start(8);run_par(8,[&](std::size_t n){tids[n]=::syscall(SYS_gettid);});
    auto seen=tids;std::sort(seen.begin(),seen.end());
    if(seen[0]<=0 || std::adjacent_find(seen.begin(),seen.end())!=seen.end()) error("worker IDs");
    parked(tids,0);
    auto retry=[&] {
        const unsigned others=7-(caller!=0);
        parked(tids,caller);
        {std::ofstream proof(argv[2]);proof<<::getpid()<<" "<<caller<<" 8 "<<initial_tasks;
         for(unsigned i=1;i<8;++i) proof<<" "<<tids[i];
         proof<<"\n";if(!proof) error("proof write");}
        std::fprintf(stderr,"parked pid=%ld total=8 sleeping=%u caller=%u\n",long(::getpid()),others,caller);
        fallback("native qualification retry");
    };
    if(!caller) retry();
    run_par(8,[&](std::size_t n){if(n==caller) retry();});
    error("fallback unexpectedly returned");
}
'''

FORCED = r'''
#include "parse_packets.hpp"
namespace pipeline::parse {
template<bool CheckBad> inline EvMasks qualification_packet(const EvBatch& b,Out& out,std::size_t n,const EvP& p) {
    if(std::getenv("QUALIFY_IFMA")) return ev_packet<CheckBad,true>(b,out,n,p);
    return ev_packet<CheckBad,false>(b,out,n,p);
}
}
#define ev_vec qualification_packet
#include "routing_packets.cpp"
'''

def tiny_input(path):
    path.mkdir()
    (path/"manifest.json").write_text('{"window_start":"2026-10-01T00:00:00Z","window_end":"2026-10-03T00:00:00Z"}')
    (path/"products.csv").write_text("product_id,category\np1,toys\n")
    (path/"campaigns.csv").write_text("campaign_id,channel\nc_1,paid\n")
    data = {
        "events": "event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents\n"
                  "opaque first,alice,2026-10-01T12:00:00Z,2026-10-01T12:00:00Z,view,p1,\n"
                  "opaque second,anon,2026-10-01T12:00:01Z,2026-10-01T12:00:01Z,purchase,p1,17\n",
        "clicks": "click_id,user_id,campaign_id,ts\nopaque click,anon,c_1,2026-10-01T12:00:00Z\n",
        "links": "user_id_a,user_id_b\nalice,anon\n",
    }
    for kind, csv in data.items():
        (path/kind).mkdir()
        (path/kind/"part.csv").write_text(csv)

def table_check(out):
    import pyarrow.parquet as pq
    day = datetime.date(2026,10,1)
    tables = {
        "user_daily":dict(user_id="alice",day=day,sessions=1,events=2,revenue_cents=17,top_category="toys"),
        "campaign_daily":dict(campaign_id="c_1",channel="paid",day=day,sessions=1,purchasing_sessions=1,
                              revenue_cents=17,assisted_sessions=1),
    }
    for table, row in tables.items():
        rows = [r for part in sorted((out/table).glob("*.parquet")) for r in pq.read_table(part).to_pylist()]
        if rows != [row]:
            raise RuntimeError(f"{table}: expected {row!r}, observed {rows!r}")

def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1<<20), b""): h.update(b)
    return h.hexdigest()

def elf_digest(path):
    with path.open("rb") as f:
        hdr=f.read(20)
    if len(hdr)!=20 or hdr[:6]!=b"\x7fELF\x02\x01" or int.from_bytes(hdr[18:20],"little")!=62:
        raise ValueError(f"expected Linux x86-64 little-endian ELF: {path}")
    return digest(path)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,required=True,help="Git checkout or a git archive tar of the specified commit")
    p.add_argument("--revision",default=REV,help="full immutable commit ID")
    p.add_argument("--output",type=Path,required=True,help="new directory for the per-check report and logs")
    p.add_argument("--variant",choices=VARIANTS,default="control")
    p.add_argument("--cxx",default=os.environ.get("CXX") or shutil.which("g++-12") or "g++")
    p.add_argument("--release",type=Path,help="release ELF extracted alongside its optional .opt")
    p.add_argument("--release-sha256")
    p.add_argument("--only",choices=MODULES,nargs="+",default=MODULES)
    p.add_argument("--require-wide",action="store_true",help="exit 77 if the requested wide coverage was unavailable")
    p.add_argument("--compile-unavailable",action="store_true",help="also build tests for unavailable ISA paths, without running them")
    args=p.parse_args()
    if platform.system()!="Linux" or platform.machine() not in ("x86_64","amd64"):
        p.error("requires Linux x86-64")
    if not re.fullmatch(r"[0-9a-f]{40}",args.revision): p.error("--revision must be a full SHA")
    if "release" in args.only and not args.release: p.error("--release is required for the release check")
    repo=args.source.resolve()
    git=["git","-c",f"safe.directory={repo}","-C",str(repo)]
    if repo.is_dir() and subprocess.check_output([*git,"rev-parse",args.revision+"^{commit}"],text=True).strip()!=args.revision:
        p.error("source SHA did not resolve")
    dest=args.output.resolve();dest.mkdir(parents=True,exist_ok=False)
    state={"source":args.revision,"variant":args.variant,"requested":list(args.only),"checks":{},
           "flags":{},"features":{},"affinity":sorted(os.sched_getaffinity(0)),"status":"running"}
    state["cpuinfo"]={k.strip():v.strip() for line in Path("/proc/cpuinfo").read_text().split("\n\n",1)[0].splitlines()
                      if ":" in line for k,v in [line.split(":",1)] if k.strip() in ("vendor_id","model name","flags")}
    state["kernel"]=platform.release()
    state["physical_memory_bytes"]=os.sysconf("SC_PHYS_PAGES")*os.sysconf("SC_PAGE_SIZE")
    for path in (Path("/sys/fs/cgroup/memory.max"),Path("/sys/fs/cgroup/memory/memory.limit_in_bytes")):
        if path.is_file():
            limit=path.read_text().strip()
            state["cgroup_memory_limit_bytes"]=int(limit) if limit.isdecimal() else limit
            break
    def save():
        (dest/"status.json").write_text(json.dumps(state,sort_keys=True,indent=2)+"\n")
    def mark(name,status,detail=""):
        state["checks"][name]={"status":status,"detail":detail};save();print(name,status,detail,flush=True)
    def run(name,cmd, env=None,timeout=240):
        cmd=list(map(str,cmd))
        with (dest/(name+".log")).open("a") as f:
            f.write("$ "+shlex.join(cmd)+"\n");f.flush()
            r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env,timeout=timeout)
        if r.returncode: raise RuntimeError(f"{name} returned {r.returncode}: "+(dest/(name+".log")).read_text(errors="replace")[-2000:])
        return (dest/(name+".log")).read_text(errors="replace")
    try:
        gcc=subprocess.check_output([args.cxx,"--version"],text=True)
        if "clang" in gcc.lower(): raise ValueError("use the GNU C++ compiler for the release toolchain check")
        state["compiler"]=gcc.splitlines()[0]
        with tempfile.TemporaryDirectory() as td:
            tmp=Path(td); src=tmp/"source";src.mkdir()
            archive=repo.read_bytes() if repo.is_file() else subprocess.check_output([*git,"archive",args.revision,"cpp/pipeline","tests/pipeline","build.sh"])
            with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
                if tar.pax_headers.get("comment")!=args.revision: raise ValueError("archive has no matching Git commit ID")
                state["archive_sha256"]=hashlib.sha256(archive).hexdigest()
                for member in tar:
                    if member.isdir(): continue
                    if not member.isfile() or not (src/member.name).resolve().is_relative_to(src): raise ValueError(member.name)
                    target=src/member.name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(member).read())
            cpp=src/"cpp/pipeline"; tests=src/"tests/pipeline"
            switches=set(VARIANTS[args.variant]); build=(src/"build.sh").read_text()
            if any(s not in build for s in switches): raise ValueError("requested switches absent in pinned build.sh")
            base=[args.cxx,"-std=c++20","-O3","-DNDEBUG","-pthread","-static","-fno-pie","-no-pie","-DSHOP_KERNEL_ABSOLUTE_TABLES"]
            pr=tmp/"cpu.cpp";pr.write_text(PROBE);exe=tmp/"cpu"
            run("probe-build",[*base,pr,"-o",exe]); flags=run("probe",[exe]).splitlines()[-1].split()
            features=("csv","routing","sessions","ifma","v4","avx2","avx512f","avx512bw","avx512vl","avx512dq","avx512cd","bmi1","bmi2","popcnt","lzcnt")
            feat=dict(zip(features,map(lambda v: v=="1",flags),strict=True));state["features"]=feat
            for name in ("csv","routing","sessions","ifma","v4"): print("CPU",name,feat[name],flush=True)
            flavors=[]
            if "PAIRED_CLICK_SORT" in switches: flavors.append("-DSHOP_PAIRED_CLICK_SORT")
            if "DIRECT_DICT_LOOPS" in switches: flavors.append("-DSHOP_DIRECT_DICT_LOOPS=1")
            if "INTEL_LAYOUT" in switches: flavors+=["-mtune=cascadelake","-flto=1","-flto-partition=one","-Wa,-mbranches-within-32B-boundaries"]
            want_v4="V4_SIDECAR" in switches
            if want_v4 and feat["v4"]: flavors+=["-march=x86-64-v4","-mtune=cascadelake"]
            if want_v4 and not feat["v4"]: mark("v4-instructions","skipped","CPU/OS does not support x86-64-v4")
            libs=[];opts=[*base,*flavors]
            state["flags"]={"production":opts,"tests":opts+["-UNDEBUG"],"switches":sorted(switches)}
            if set(args.only)-{"release"}:
                alloc=tmp/"alloc.o";run("allocator-build",[*opts,"-c",cpp/"sys_global_alloc.cpp","-o",alloc]);libs.append(alloc)
                if "CUSTOM_MEMORY" in switches:
                    mem=tmp/"mem.o"
                    run("memory-object-build",[args.cxx,"-std=c++20","-O3","-march=x86-64","-ffreestanding","-fno-builtin",
                                               "-fno-stack-protector","-fno-lto","-fno-pie","-c",cpp/"sys_memory_x86.cpp","-o",mem])
                    libs.append(mem)
            def unit(name,file,options=(),more=()):
                target=tmp/name
                run(name+"-build",[*opts,"-UNDEBUG","-Wno-ignored-attributes","-I",cpp,"-I",tests,*options,file,*more,*libs,"-o",target])
                return target
            def test(name,exe,argv=(),env=None,must=()):
                result=run(name,[exe,*argv],env=env,timeout=45)
                if any(needle not in result for needle in must): raise RuntimeError(f"{name}: missing completion proof")
                mark(name,"passed",result.splitlines()[-1]);return result
            if "parked" in args.only:
                helper=tmp/"parked.cpp";helper.write_text(PARKED); target=unit("parked",helper)
                for who in ("main","worker"):
                    expected="sleeping="+("7" if who=="main" else "6")
                    test("parked-"+who,target,[who,tmp/("marker-"+who)],must=(expected,"previous_workers=8 remaining_pool_workers=0"))
            if "csv" in args.only:
                parts=[]
                for name in ("k_free.cpp","k_free_x86.cpp","k_free_clicks.S"):
                    obj=tmp/(name+".o");run(name+"-build",[*opts,"-Wno-ignored-attributes","-c",cpp/name,"-o",obj]);parts.append(obj)
                target=unit("csv",tests/"kernel_rows.cpp",more=parts)
                test("csv",target,must=("SIMD dispatch "+("on." if feat["csv"] else "off."),))
                if not feat["csv"]: mark("csv-wide","skipped","CSV test ran its portable branch")
            if "routing" in args.only:
                for key,name in (("packets","routing_packets.cpp"),("sinks","routing_sinks.cpp")):
                    test("routing-"+key,unit(key,tests/name))
                if feat["routing"] or args.compile_unavailable:
                    forced=tmp/"forced.cpp";forced.write_text(FORCED);target=unit("forced",forced)
                if feat["routing"]:
                    env=dict(os.environ);env.pop("QUALIFY_IFMA",None)
                    test("routing-wide-no-ifma",target,env=env)
                    if feat["ifma"]: test("routing-wide-ifma",target,env=env|{"QUALIFY_IFMA":"1"})
                    else: mark("routing-wide-ifma","skipped","CPU lacks AVX512IFMA")
                else: mark("routing-wide","skipped","routing packet and sink tests ran scalar branches")
            if "sessions" in args.only:
                if feat["sessions"] or args.compile_unavailable:
                    limits=unit("dense-limits",tests/"session_dense_limits.cpp")
                    sess=unit("sessions",tests/"session_runs.cpp",["-DSESSION_TEST_FAST"])
                if feat["sessions"]:
                    test("dense-limits",limits)
                    for i in range(5): test("session-pattern-"+str(i),sess,[i])
                else: mark("sessions-wide","skipped","CPU lacks dense session ISA, including AVX512CD")
            if "paired" in args.only:
                if not (tests/"links_sort_pair.cpp").exists(): mark("paired-sort","skipped","absent in pinned revision")
                else:
                    if feat["routing"] or args.compile_unavailable:
                        pair=unit("paired-sort",tests/"links_sort_pair.cpp",["-DSHOP_PAIRED_CLICK_SORT"])
                    if feat["routing"]: test("paired-sort",pair)
                    else: mark("paired-sort","skipped","CPU lacks paired-sort ISA")
            if "release" in args.only:
                exe=args.release.resolve();side=exe.with_name(exe.name+".opt")
                if not exe.is_file(): raise FileNotFoundError(exe)
                state["release"]={"path":str(exe),"sha256":elf_digest(exe),"sidecar":elf_digest(side) if side.is_file() else None}
                if args.release_sha256 and state["release"]["sha256"]!=args.release_sha256: raise ValueError("release SHA-256 differs")
                if want_v4 and not side.is_file(): raise ValueError("expected adjacent release sidecar")
                dat=tmp/"input";tiny_input(dat)
                env={k:v for k,v in os.environ.items() if not k.startswith(("XL_","V1ND_"))}
                env.update(NCPU="8",XL_POOL="1",XL_SPIN="0",XL_TIMING="1",V1ND_LOG="1")
                trace=shutil.which("strace");out=tmp/"fallback";tracefile=dest/"release-exec.trace"
                if trace:
                    q=subprocess.run([trace,"-qq","-f","-e","trace=execve","-o",str(tmp/"trace-check"),"/bin/true"],capture_output=True)
                    if q.returncode: trace=None
                runner=[trace,"-qq","-f","-e","trace=execve","-o",tracefile] if trace else []
                s=run("release-retry",[*runner,exe,dat,out],env,timeout=45)
                if "[pipeline] fallback:" not in s or "[v1nd]" not in s: raise ValueError("release did not report the actual generic retry")
                table_check(out);mark("release-retry","passed","8 workers, automatic retry, both hand-calculated tables exact")
                direct=tmp/"generic";run("release-generic",[exe,"--generic",dat,direct],env,timeout=45)
                table_check(direct);mark("release-generic","passed","both hand-calculated tables exact")
                if trace:
                    calls=tracefile.read_text()
                    if not re.search(r'execve\("/proc/self/exe", \[[^\n]*"--generic"',calls): raise ValueError("missing process re-exec")
                    opted=bool(re.search(r'execve\("[^"]+\.opt", [^\n]+ = 0',calls))
                    if want_v4 and opted!=feat["v4"]: raise ValueError("observed v4 sidecar disagrees with CPU/OS support")
                    mark("release-exec","passed","recorded generic exec"+(" and v4 sidecar" if want_v4 and feat["v4"] else ""))
                else: mark("release-exec","skipped","strace unavailable or ptrace disallowed; actual sidecar selection unobserved")
            lacking=[k for k in ("csv","routing","sessions") if k in args.only and not feat[k]]
            if "paired" in args.only and (tests/"links_sort_pair.cpp").exists() and not feat["routing"]: lacking.append("paired")
            if want_v4 and not feat["v4"]: lacking.append("v4")
            if lacking and args.require_wide:
                state["status"]="wide coverage unavailable";state["unavailable"]=lacking;save();return 77
        state["status"]="passed";save();return 0
    except Exception as exc:
        state.update(status="failed",error=str(exc));save();raise

if __name__=="__main__": sys.exit(main())
