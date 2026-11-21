"""Select a native Intel host that can execute the CSV instruction path being measured."""
import os
from pathlib import Path

records = Path("/proc/cpuinfo").read_text().split("\n\n")
affinity = os.sched_getaffinity(0)
cpus = {}
for record in records:
    fields = dict(line.split(":", 1) for line in record.splitlines() if ":" in line)
    fields = {key.strip(): value.strip() for key, value in fields.items()}
    if "processor" in fields and int(fields["processor"]) in affinity:
        cpus[int(fields["processor"])] = fields
required = {"avx2","bmi1","bmi2","abm","popcnt","avx512f","avx512bw","avx512dq","avx512vl"}
approved = bool(cpus) and len(cpus) == len(affinity) and all(
    cpu.get("vendor_id") == "GenuineIntel" and required <= set(cpu.get("flags", "").split())
    for cpu in cpus.values())
models = sorted({cpu.get("model name","unknown") for cpu in cpus.values()})
message = f"{', '.join(models)}, {len(affinity)} assigned vCPUs, Intel wide event measurements enabled={approved}"
print("::notice title=Runner instruction path::" + message)
with open(os.environ["GITHUB_OUTPUT"], "a") as output:
    print("wide=" + str(approved).lower(), file=output)
