"""Ordered ZPD timeline of one traced frame: each closed interval with the occlusion test
draws inside it (depth off = total, depth on = visible) and the measured samples.
usage: python zpd_sequence.py <game.log> <frame number> [max rows]
"""
import re
import sys

log, frame = sys.argv[1], sys.argv[2]
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 80
lines = open(log, encoding='utf-8', errors='replace').read().splitlines()
res = {}
for l in lines:
    m = re.search(r'\[ht-zpd-res\] host=(\d+) key=([0-9A-F]+) samples=(\d+)', l)
    if m:
        res[int(m.group(1))] = int(m.group(3))
inside = False
pending = []  # draws since the previous ZPD event
rows = []
last_addr = None
for l in lines:
    if f'frame {frame} begin' in l:
        inside = True
        continue
    if inside and 'frame end' in l:
        break
    if not inside:
        continue
    m = re.search(r'\[ht-zpd-draw\] draw=(\d+) vs=([0-9A-F]+) depth_raw=([0-9A-F]+)', l)
    if m:
        z = 'Z' if int(m.group(3), 16) & 2 else 'noZ'
        pending.append(f'{m.group(2)[:6]}:{z}')
        continue
    m = re.search(r'\[ht-zpd-close\] host=(\d+) key=([0-9A-F]+) est=(\d+)', l)
    if m:
        host = int(m.group(1))
        rows.append(f'  [{last_addr} -> key {m.group(2)}] host={host} draws={pending or "-"} samples={res.get(host, "?")} est={m.group(3)}')
        pending = []
        continue
    m = re.search(r'\[ht-zpd\] draw=\d+ addr=([0-9A-F]+) end=(\d)', l)
    if m:
        last_addr = m.group(1)
for r in rows[:limit]:
    print(r)
print(f'... {len(rows)} intervals')
