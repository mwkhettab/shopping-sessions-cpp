"""Check all click-loop instructions and relocations against the pinned inline asm."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).joinpath("k_free.rs").read_text()
cpp = Path(__file__).resolve().parents[2] / "cpp/pipeline/k_free_clicks.S"
literal = r'"(?:\\.|[^"\\])*"'
macros = {}
for name in ("cl_ms", "cl_d"):
    section = source.split(f"macro_rules! {name} {{", 1)[1].split("\n}", 1)[0]
    args = re.findall(r"\$(\w+):literal", section.split("=>", 1)[0])
    body = section.split("concat!(", 1)[1].split("\n        )", 1)[0]
    macros[name] = args, re.findall(literal + r"|\$\w+", body)
body = source.split("unsafe fn cl_loop(", 1)[1].split("core::arch::asm!(", 1)[1].split('inout("rdi")', 1)[0]
lines = []
for line in body.strip().splitlines():
    s = line.strip().rstrip(",")
    if s[0] == '"':
        lines.append(json.loads(s))
    else:
        name = s.split("!", 1)[0]
        args, tokens = macros[name]
        values = dict(zip(args, map(json.loads, re.findall(literal, s)), strict=True))
        lines.append("".join(values[t[1:]] if t.startswith("$") else json.loads(t) for t in tokens))
symbols = {"T": "shopping_kernel_tables", "CTS": "shopping_kernel_cts", "CAMP": "shopping_kernel_camp",
           "KID1": 937472, "UID1": 941504, "SCR": 1536, "PFD": 1024, "CIDO": 1024, "CAMPO": 512, "UIDO": 1536}
loop = "\n".join(lines).format(**symbols)
# Four callee-saved registers and the result pointer precede the loop in the C++ ABI.
# Align the original loop at the same address for the .p2align directive inside it.
assembly = ".intel_syntax noprefix\n.text\n.p2align 5\n.zero 9\n" + loop + "\n"


def sections(obj):
    data = obj.read_bytes()
    pos, = struct.unpack_from("<Q", data, 40)
    width, count, strings = struct.unpack_from("<HHH", data, 58)
    headers = [struct.unpack_from("<IIQQQQIIQQ", data, pos + i * width) for i in range(count)]

    def content(header):
        return data[header[4]:header[4] + header[5]]

    def text_at(blob, offset):
        return blob[offset:blob.index(0, offset)].decode()

    names = content(headers[strings])
    table = {text_at(names, header[0]): header for header in headers}
    sym = table[".symtab"]
    names = content(headers[sym[6]])
    relocations = []
    for address, info, addend in struct.iter_unpack("<QQq", content(table[".rela.text"])):
        name, = struct.unpack_from("<I", data, sym[4] + (info >> 32) * sym[9])
        relocations.append((address, info & 0xffffffff, addend, text_at(names, name)))
    return content(table[".text"]), relocations


with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    expected, candidate = tmp / "original.o", tmp / "cpp.o"
    (tmp / "original.S").write_text(assembly)
    clang = [os.environ.get("CLANG", "clang"), "-target", "x86_64-unknown-linux-gnu", "-c"]
    subprocess.run(clang + [tmp / "original.S", "-o", expected], check=True)
    subprocess.run(clang + ["-DSHOP_KERNEL_ABSOLUTE_TABLES", cpp, "-o", candidate], check=True)
    original, original_reloc = sections(expected)
    actual, actual_reloc = sections(candidate)
    assert actual[9:len(original)] == original[9:], "the loop's instructions differ"
    assert actual_reloc == original_reloc, "the loop's table or field displacements differ"
    subprocess.run(clang + [cpp, "-o", tmp / "pic.o"], check=True)
    print(f"All {len(original)-9} loop bytes and {len(original_reloc)} table relocations match. The PIC variant assembles.")
