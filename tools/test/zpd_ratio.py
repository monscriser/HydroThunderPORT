"""Per-light occlusion visibility from a ZPD trace (tools/wsl/patch_zpd_trace.py).

Hydro Thunder draws each light's test sphere twice: depth test off (interval closed by the
report at slot K, = total samples) and depth test on (closed by slot K+0x80, = visible
samples). Pairs are matched by host query index (visible = total + 2).
usage: python zpd_ratio.py <game.log>
"""
import re
import sys

res = {}
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = re.search(r'\[ht-zpd-res\] host=(\d+) key=([0-9A-F]+) samples=(\d+)', line)
    if m:
        res[int(m.group(1))] = (int(m.group(2), 16), int(m.group(3)))
pairs = []
for host, (key, total) in sorted(res.items()):
    if key == 0xFFFFFFFF or key & 0x80:
        continue
    vis = res.get(host + 2)
    if vis and vis[0] == key + 0x80:
        pairs.append((host, key, total, vis[1]))
hidden = sum(1 for p in pairs if p[2] and p[3] == 0)
partial = sum(1 for p in pairs if p[2] and 0 < p[3] < p[2])
full = sum(1 for p in pairs if p[2] and p[3] >= p[2])
offscreen = sum(1 for p in pairs if not p[2])
print(f'pairs={len(pairs)} fully_visible={full} partial={partial} hidden={hidden} offscreen={offscreen}')
for host, key, total, vis in pairs:
    if total:
        print(f'  host={host} key={key:03X} total={total} visible={vis} ratio={vis / total:.2f}')
