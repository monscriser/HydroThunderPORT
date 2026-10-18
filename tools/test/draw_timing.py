"""Groups the per-draw GPU times ([ht-tdraw], tools/wsl/patch_draw_timing.py) of a traced
frame (HT_TRACE_FRAME) by render target binding (the trace's "draw N bind ..." lines)
and prints the most expensive groups, with the resolves/transfers inside them.
usage: python draw_timing.py <game.log> [top]
"""
import re
import sys

log = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
times = {}
pres = {}
binds = []  # (first draw, description)
events = {}  # draw -> list of strings
inside = False
for line in log:
    if "[ht-trace] ===== frame" in line and "begin" in line:
        inside = True
        continue
    if "[ht-trace] ===== frame end" in line:
        inside = False
    m = re.search(r"\[ht-tdraw\] draw=(\d+) subm=\d+ us=(-?[\d.]+)(?: pre=(-?[\d.]+))?", line)
    if m:
        d, us = int(m.group(1)), float(m.group(2))
        if us > 0:
            times[d] = times.get(d, 0.0) + us
        if m.group(3) and float(m.group(3)) > 0:
            pres[d] = pres.get(d, 0.0) + float(m.group(3))
        continue
    if not inside:
        continue
    t = re.sub(r"^.*?\[ht-trace\] ", "", line)
    m = re.match(r"draw (\d+) bind (.*?) \|", t)
    if m:
        binds.append((int(m.group(1)), m.group(2)))
        continue
    m = re.match(r"resolve after draw (\d+) (.*)", t)
    if m:
        events.setdefault(int(m.group(1)), []).append("resolve " + m.group(2)[:90])
total = sum(times.values()) + sum(pres.values())
groups = []
for i, (d0, desc) in enumerate(binds):
    d1 = binds[i + 1][0] - 1 if i + 1 < len(binds) else max(times or [d0])
    dr = sum(v for k, v in times.items() if d0 <= k <= d1)
    pr = sum(v for k, v in pres.items() if d0 <= k <= d1)
    ev = [e for k in range(d0, d1 + 1) for e in events.get(k, [])]
    groups.append((dr + pr, dr, pr, d0, d1, desc, ev))
print(f"total {total:.0f} us (draws {sum(times.values()):.0f}, between draws {sum(pres.values()):.0f}), "
      f"{len(times)} timed draws, {len(binds)} bind groups")
for us, dr, pr, d0, d1, desc, ev in sorted(groups, reverse=True)[:top]:
    print(f"{us:7.0f} us {100 * us / total:4.1f}% (draw {dr:.0f} + between {pr:.0f})  "
          f"draws {d0}-{d1} ({d1 - d0 + 1})  {desc}")
    for e in ev[:3]:
        print(f"            {e}")
