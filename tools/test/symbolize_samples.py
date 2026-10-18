"""Symbolize sample_threads.ps1 output (run in WSL). Uses llvm-nm symbol tables of the
Windows binaries (image base + offset -> nearest preceding text symbol).
Prints, per thread file: top functions by RIP ("self") and by appearance anywhere in the
sampled stack ("inclusive", heuristic: stack qwords that point into a module).

usage: python3 symbolize_samples.py <samples dir> [top]
"""
import bisect
import collections
import re
import subprocess
import sys
from pathlib import Path

NM = "/root/ht/llvm-mingw/bin/llvm-nm"
OBJDUMP = "/root/ht/llvm-mingw/bin/llvm-objdump"
BINARIES = {
    "hydrothunder.exe": "/root/ht/build-win/hydrothunder.exe",
    "librexruntime.dll": "/root/ht/sdk/out/win-amd64/librexruntime.dll",
    "rexgpu-xenos.dll": "/root/ht/sdk/out/win-amd64/rexgpu-xenos.dll",
}
tables = {}
for name, path in BINARIES.items():
    head = subprocess.run([OBJDUMP, "-p", path], capture_output=True, text=True).stdout
    base = int(re.search(r"ImageBase\s+([0-9a-fA-F]+)", head).group(1), 16)
    syms = []
    for line in subprocess.run([NM, "--defined-only", path], capture_output=True, text=True).stdout.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) == 3 and parts[1] in "tT":
            syms.append((int(parts[0], 16) - base, parts[2]))
    syms.sort()
    tables[name] = ([a for a, _ in syms], [n for _, n in syms])


def sym(token):
    m = re.match(r"(.+)\+0x([0-9A-F]+)$", token)
    if not m or m.group(1) not in tables:
        return token.split("+")[0] if "+" in token else token
    addrs, names = tables[m.group(1)]
    off = int(m.group(2), 16)
    i = bisect.bisect_right(addrs, off) - 1
    return f"{m.group(1).split('.')[0]}:{names[i] if i >= 0 else '?'}"


top = int(sys.argv[2]) if len(sys.argv) > 2 else 15
for f in sorted(Path(sys.argv[1]).glob("thread-*.txt")):
    rows = [l.split() for l in f.read_text(encoding="utf-8-sig").splitlines() if l.strip()]
    selfc = collections.Counter(sym(r[0]) for r in rows)
    inc = collections.Counter()
    for r in rows:
        inc.update(set(sym(t) for t in r))
    print(f"== {f.name}: {len(rows)} samples")
    print("  self:")
    for k, v in selfc.most_common(top):
        print(f"    {100 * v / len(rows):5.1f}%  {k}")
    print("  inclusive (heuristic):")
    for k, v in inc.most_common(top):
        print(f"    {100 * v / len(rows):5.1f}%  {k}")
