"""Analyze hitch_sample.ps1 output (run in WSL; symbols from the exact Windows binaries).
Finds the longest stretch where the GPU command thread (cp, from the game log) is idle-waiting,
then shows what every other thread was doing in that stretch vs. outside it.

usage: python3 hitch_analyze.py <samples dir> <game.log> [exe] [rexgpu dll] [runtime dll]
"""
import bisect, collections, re, subprocess, sys
from pathlib import Path

d = Path(sys.argv[1]); log = Path(sys.argv[2]).read_text(errors='replace')
bins = {'hydrothunder.exe': sys.argv[3] if len(sys.argv) > 3 else '/root/ht/build-win/hydrothunder.exe',
        'rexgpu-xenos.dll': sys.argv[4] if len(sys.argv) > 4 else '/root/ht/sdk/out/win-amd64/rexgpu-xenos.dll',
        'librexruntime.dll': sys.argv[5] if len(sys.argv) > 5 else '/root/ht/sdk/out/win-amd64/librexruntime.dll'}
NM = '/root/ht/llvm-mingw/bin/llvm-nm'; OD = '/root/ht/llvm-mingw/bin/llvm-objdump'
tables = {}
for name, path in bins.items():
    head = subprocess.run([OD, '-p', path], capture_output=True, text=True).stdout
    base = int(re.search(r'ImageBase\s+([0-9a-fA-F]+)', head).group(1), 16)
    syms = sorted((int(a, 16) - base, n) for a, t, n in
                  (l.split(maxsplit=2) for l in subprocess.run([NM, '--defined-only', path], capture_output=True, text=True).stdout.splitlines() if len(l.split(maxsplit=2)) == 3)
                  if t in 'tT')
    tables[name.lower()] = ([a for a, _ in syms], [n for _, n in syms])
mods = []
for l in (d / 'modules.txt').read_text(encoding='utf-8-sig').splitlines():
    n, b, e = l.split(); mods.append((int(b, 16), int(e, 16), n.lower()))
mods.sort()
def sym(a):
    i = bisect.bisect_right([m[0] for m in mods], a) - 1
    if i < 0 or a >= mods[i][1]: return '?'
    b, e, n = mods[i]
    if n in tables:
        addrs, names = tables[n]; j = bisect.bisect_right(addrs, a - b) - 1
        return n.split('.')[0] + ':' + (names[j] if j >= 0 else '?')
    return n
cp = re.search(r'\[t(\d+)\] \[ht-cpu\]', log).group(1)
samples = collections.defaultdict(list)  # tid -> [(ms, [syms])]
for l in (d / 'all-threads.txt').read_text(encoding='utf-8-sig').splitlines():
    if l.startswith('#') or not l.strip(): continue
    f = l.split(); ms, tid = int(f[0]), f[1]
    samples[tid].append((ms, [sym(int(x, 16)) for x in f[2:]]))
WAIT = ('ntdll', 'win32u', 'kernelbase')
def waiting(s): return s[0].split(':')[0].split('.')[0] in WAIT
# longest stretch of cp waiting
best = (0, 0, 0); start = None; last = None
for ms, s in samples[cp]:
    if waiting(s):
        if start is None: start = ms
        last = ms
    else:
        if start is not None and last - start > best[0]: best = (last - start, start, last)
        start = None
if start is not None and last - start > best[0]: best = (last - start, start, last)
dur, a, b = best
print(f'cp thread {cp}: longest wait {dur} ms from {a} to {b} ms after sampling start')
cp_done = False
order = sorted(samples.items(), key=lambda kv: -sum(1 for ms, s in kv[1] if a <= ms <= b and not waiting(s)))
order.sort(key=lambda kv: kv[0] != cp)
for tid, ss in order:
    inside = [s for ms, s in ss if a <= ms <= b]
    busy = [s for s in inside if not waiting(s)]
    if not inside: continue
    print(f'\n== thread {tid}{" (cp)" if tid == cp else ""}: {len(inside)} samples in stretch, {len(busy)} busy')
    top = collections.Counter(s[0] for s in inside).most_common(6)
    for k, v in top: print(f'   self {100*v/len(inside):5.1f}%  {k[:150]}')
    inc = collections.Counter(x for s in inside for x in set(s[1:]))
    for k, v in inc.most_common(10): print(f'   incl {100*v/len(inside):5.1f}%  {k[:150]}')
    if tid != cp and len(busy) < 3 and cp_done: break
    cp_done = cp_done or tid == cp

print('\n== timeline (100 ms bins)')
main = max(samples, key=lambda t: sum(1 for ms, s in samples[t] if not waiting(s)) if t != cp else -1)
bins = collections.defaultdict(lambda: [0, 0, 0, 0])
for ms, s in samples[cp]: k = ms // 100; bins[k][0] += 1; bins[k][1] += not waiting(s)
for ms, s in samples[main]: k = ms // 100; bins[k][2] += 1; bins[k][3] += any('822C7C60' in x or '822D8570' in x for x in s)
print(f'main thread = {main}; columns: bin(ms) cp_busy% main_spinwait%')
for k in sorted(bins):
    c, cb, m, ms_ = bins[k]
    if a - 1500 <= k * 100 <= b + 500: print(f'  {k*100:6d}  {100*cb/max(c,1):5.1f}  {100*ms_/max(m,1):5.1f}')
