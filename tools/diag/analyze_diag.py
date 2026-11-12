#!/usr/bin/env python3
"""Per-track report from a diagnostic-build session folder (app/src/ht_diag.h).

usage: analyze_diag.py <session folder> [--log <game log>] [--steady-skip 20]
writes <session>/report.md (pt-BR) and <session>/summary.csv. Python 3 stdlib only.

Segments: segment 0 = boot/menus; every track load (its sound bank opened) starts
a new segment, which lasts until the next track load (so menu time after a race
is counted with that race; the "steady" numbers skip the first N seconds of each
segment = loading + countdown, and the hitch list shows where things happened).
"""
import csv
import math
import pathlib
import re
import statistics
import sys
from collections import defaultdict

KV = re.compile(r"([A-Za-z_][\w]*)=(-?[\d.]+)")
TAGS = ("ht-perf", "ht-cpu", "ht-gpu", "ht-draw", "ht-paint", "ht-vram", "ht-pipe", "ht-present")
SECTION = {"gpu ms/frame": "gpu_", "cpu wait ms/frame": "wait_", "per frame": "pf_",
           "cp thread ms/frame": "cp_", "IssueDraw cpu ms/frame": "", "ms/paint": "paint_"}


def num(v):
    try:
        x = float(v)
        return None if x < 0 or math.isnan(x) else x
    except (TypeError, ValueError):
        return None


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    k = (len(s) - 1) * p / 100.0
    f = math.floor(k)
    c = min(f + 1, len(s) - 1)
    return s[f] + (s[c] - s[f]) * (k - f)


def low_fps(frame_ms, fraction):
    """Average FPS of the worst `fraction` of frames (the usual '1% low')."""
    if not frame_ms:
        return None
    worst = sorted(frame_ms, reverse=True)[:max(1, int(len(frame_ms) * fraction))]
    return 1000.0 / (sum(worst) / len(worst))


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else None


def fmt(x, d=1):
    return "-" if x is None else f"{x:.{d}f}"


def parse_log_line(line):
    for tag in TAGS:
        i = line.find(f"[{tag}]")
        if i < 0:
            continue
        body = line[i + len(tag) + 2:].replace("idle(waiting guest)=", "idle=")
        out = {}
        if tag == "ht-gpu":
            m = re.search(r"busy (\d+(?:\.\d+)?)%", body)
            if m:
                out["busy_pct"] = float(m.group(1))
        parts = body.split("|")
        for part in parts:
            prefix = ""
            for label, p in SECTION.items():
                if label + ":" in part:
                    prefix = p
                    part = part.split(label + ":", 1)[1]
                    break
            for k, v in KV.findall(part):
                key = prefix + k
                while key in out:
                    key += "_2"
                out[key] = float(v)
        return tag, out
    return None, None


class Symbols:
    """module basename -> sorted (address, name) from nm of the exact binaries of the build."""

    def __init__(self, bins):
        self.tables = {}
        if not bins:
            return
        import shutil
        import subprocess
        nm = shutil.which("llvm-nm") or shutil.which("nm")
        if not nm:
            print("aviso: nm nao encontrado; funcoes ficam como modulo+offset", file=sys.stderr)
            return
        for f in pathlib.Path(bins).iterdir():
            if not f.is_file() or not (f.name == "hydrothunder" or ".so" in f.name):
                continue
            try:
                out = subprocess.run([nm, "-C", "--defined-only", str(f)], capture_output=True, text=True,
                                     errors="replace").stdout
            except OSError:
                continue
            rows = []
            for line in out.splitlines():
                p = line.split(" ", 2)
                if len(p) == 3 and p[1] in "TtWw":
                    try:
                        rows.append((int(p[0], 16), p[2]))
                    except ValueError:
                        pass
            rows.sort()
            self.tables[f.name] = ([a for a, _ in rows], [n for _, n in rows])

    def name(self, module, offset, sym=None):
        import bisect
        t = self.tables.get(module)
        if not t:
            return f"{sym}  [{module}]" if sym else f"{module}+0x{offset:x}"
        i = bisect.bisect_right(t[0], offset) - 1
        if i < 0:
            return f"{module}+0x{offset:x}"
        n = t[1][i]
        n = re.sub(r"\(.*", "", n) if len(n) > 90 else n
        return f"{n}  [{module}]" if module != "hydrothunder" else n


TS = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})\]")
DLC = re.compile(r"pack(\d+)track(\d+)")


def wall_seconds(line):
    m = TS.match(line)
    if not m:
        return None
    y, mo, dd, h, mi, se, ms = (int(x) for x in m.groups())
    return (dd * 24 + h) * 3600 + mi * 60 + se + ms / 1000.0


class Timeline:
    """Segments rebuilt from tracks.csv by TIME (so tracks the game build missed, like the DLC
    banks named 'Xbox360Pack01TrackNN', still get their own segment)."""

    def __init__(self, d):
        self.starts = [(0.0, 0, "boot", "Inicio / menus")]
        self.reloads = defaultdict(int)
        self.game_seg = {0: 0}  # segment number written by the game -> segment here
        self.missed = set()      # segments the game did not see (profile.csv mixes them into the previous one)
        with open(d / "tracks.csv", newline="", encoding="utf-8", errors="replace") as f:
            for r in csv.DictReader(f):
                t = float(r["t_s"])
                m = DLC.search(r["track_id"])
                if r["event"] == "enter" or (r["event"] == "fsb" and m):
                    name = r["track_name"] or (f"DLC Pack{m.group(1)} Pista {int(m.group(2))}" if m else r["track_id"])
                    if r["event"] == "enter":
                        self.game_seg[int(r["segment"])] = len(self.starts)
                    else:
                        self.missed.add(len(self.starts))
                    self.starts.append((t, len(self.starts), r["track_id"], name))
                elif r["event"] == "reload":
                    self.reloads[self.seg(t)] += 1
        self.anchor = None  # wall clock of t=0 in the game log

    def seg(self, t):
        s = 0
        for ts, k, _, _ in self.starts:
            if t >= ts:
                s = k
        return s

    def log_seg(self, line, cur):
        w = wall_seconds(line)
        if w is None:
            return cur
        if self.anchor is None:
            if "[ht-diag] per-track telemetry ON" in line:
                self.anchor = w
            return cur
        return self.seg(w - self.anchor)


def traces_by_segment(log_path, tl):
    """Traced frames: per segment, list of {groups, draws, total} from [ht-trace]/[ht-tdraw]."""
    out = defaultdict(list)
    if not log_path.exists():
        return out
    cur_seg, inside, cur = 0, False, None
    tl.anchor = None
    for line in open(log_path, encoding="utf-8", errors="replace"):
        cur_seg = tl.log_seg(line, cur_seg)
        if "[ht-trace] ===== frame" in line and "begin" in line:
            inside = True
            cur = {"seg": cur_seg, "binds": [], "shaders": {}, "times": {}, "pres": {}, "resolves": defaultdict(int),
                   "rpbreaks": 0, "total": None}
            out[cur_seg].append(cur)
            continue
        if "[ht-trace] ===== frame end" in line:
            inside = False
            continue
        if cur is None:
            continue
        m = re.search(r"\[ht-tdraw\] draw=(\d+) subm=\d+ us=(-?[\d.]+)(?: pre=(-?[\d.]+))?", line)
        if m:
            d, us = int(m.group(1)), float(m.group(2))
            if us > 0:
                cur["times"][d] = cur["times"].get(d, 0.0) + us
            if m.group(3) and float(m.group(3)) > 0:
                cur["pres"][d] = cur["pres"].get(d, 0.0) + float(m.group(3))
            continue
        m = re.search(r"\[ht-tdraw\] total_us=([\d.]+)", line)
        if m:
            cur["total"] = float(m.group(1))
            continue
        if not inside or "[ht-trace]" not in line:
            continue
        t = re.sub(r"^.*?\[ht-trace\]\s+", "", line.rstrip())
        m = re.match(r"draw (\d+) bind (.*?) \|", t)
        if m:
            cur["binds"].append((int(m.group(1)), m.group(2)))
            continue
        m = re.match(r"shaders draw (\d+) vs=(0x[0-9A-F]+) ps=(0x[0-9A-F]+) vtx=(\d+)", t)
        if m:
            cur["shaders"][int(m.group(1))] = (m.group(2), m.group(3), int(m.group(4)))
            continue
        m = re.match(r"resolve after draw (\d+)", t)
        if m:
            cur["resolves"][int(m.group(1))] += 1
            continue
        if t.startswith("rpbreak"):
            cur["rpbreaks"] += 1
    return out


def trace_report(tr, top=8):
    times, pres, binds = tr["times"], tr["pres"], tr["binds"]
    if not times:
        return ["  - (quadro rastreado sem tempos de GPU: a GPU ainda nao tinha terminado ou o jogo fechou)"]
    total = sum(times.values()) + sum(pres.values())
    lines = [f"  - quadro rastreado: {total / 1000:.2f} ms de GPU (desenhos {sum(times.values()) / 1000:.2f} + entre desenhos "
             f"{sum(pres.values()) / 1000:.2f}), {len(times)} desenhos, {len(binds)} grupos de render target, "
             f"{sum(tr['resolves'].values())} resolves, {tr['rpbreaks']} barreiras que quebram passada"]
    groups = []
    for i, (d0, desc) in enumerate(binds):
        d1 = binds[i + 1][0] - 1 if i + 1 < len(binds) else max(times or [d0])
        dr = sum(v for k, v in times.items() if d0 <= k <= d1)
        pr = sum(v for k, v in pres.items() if d0 <= k <= d1)
        groups.append((dr + pr, dr, pr, d0, d1, desc))
    for us, dr, pr, d0, d1, desc in sorted(groups, reverse=True)[:top]:
        lines.append(f"    - {us:6.0f} us ({100 * us / total:4.1f}%) desenhos {d0}-{d1} ({d1 - d0 + 1}), entre-desenhos {pr:.0f} us: {desc[:110]}")
    costly = sorted(((us, d) for d, us in times.items()), reverse=True)[:6]
    for us, d in costly:
        vs, ps, vtx = tr["shaders"].get(d, ("?", "?", 0))
        lines.append(f"    - desenho {d}: {us:.0f} us  vs={vs} ps={ps} vertices={vtx}")
    return lines


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    d = pathlib.Path(args[0])
    log_path = d / "game.log"
    skip = 20.0
    if "--log" in args:
        log_path = pathlib.Path(args[args.index("--log") + 1])
    if "--steady-skip" in args:
        skip = float(args[args.index("--steady-skip") + 1])
    syms = Symbols(args[args.index("--bins") + 1] if "--bins" in args else None)
    tl = Timeline(d)

    # CPU profile (profile.csv) aggregated by FUNCTION after symbolization.
    prof_self = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))  # seg -> thread -> func -> n
    prof_incl = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))
    prof_meta = ""
    if (d / "profile.csv").exists():
        with open(d / "profile.csv", newline="", encoding="utf-8", errors="replace") as f:
            for r in csv.DictReader(f):
                if r["kind"] == "meta":
                    prof_meta = f"{r['offset']} amostras, {r['samples']}"
                    continue
                fn = syms.name(r["module"], int(r["offset"], 16), r.get("sym"))
                (prof_self if r["kind"] == "self" else prof_incl)[tl.game_seg.get(int(r["segment"]), int(r["segment"]))][r["thread"]][fn] += int(r["samples"])
    hitch_rows = defaultdict(lambda: defaultdict(list))  # seg -> (hitch_t, ms) -> rows
    if (d / "hitches.csv").exists():
        with open(d / "hitches.csv", newline="", encoding="utf-8", errors="replace") as f:
            for r in csv.DictReader(f):
                hitch_rows[tl.seg(float(r["hitch_t_s"]))][(float(r["hitch_t_s"]), float(r["frame_ms"]))].append(r)
    traces = traces_by_segment(log_path, tl)

    # Segments.
    segs = {k: {"id": i, "name": n, "start": t, "reloads": tl.reloads[k], "order": k} for t, k, i, n in tl.starts}

    # Frames.
    frames = defaultdict(list)  # seg -> [(t, ms)]
    with open(d / "frames.csv", newline="") as f:
        for r in csv.DictReader(f):
            t = float(r["t_s"])
            frames[tl.seg(t)].append((t, float(r["frame_ms"])))
    for s, fr in frames.items():
        if s in segs:
            segs[s]["end"] = fr[-1][0]

    # System + threads.
    system = defaultdict(list)
    if (d / "system.csv").exists():
        with open(d / "system.csv", newline="") as f:
            for r in csv.DictReader(f):
                system[tl.seg(float(r["t_s"]))].append(r)
    threads = defaultdict(lambda: defaultdict(list))
    if (d / "threads.csv").exists():
        with open(d / "threads.csv", newline="", encoding="utf-8", errors="replace") as f:
            for r in csv.DictReader(f):
                threads[tl.seg(float(r["t_s"]))][(r["t_s"], r["name"] + "#" + r["tid"])].append(float(r["cpu_pct"]))
    opens = []
    if (d / "opens.csv").exists():
        with open(d / "opens.csv", newline="", encoding="utf-8", errors="replace") as f:
            opens = [(float(r["t_s"]), r["guest_path"]) for r in csv.DictReader(f)]

    # Game log: 2 s reports, assigned to the segment of the last [ht-track] marker.
    reports = defaultdict(lambda: defaultdict(list))  # seg -> "tag.key" -> values
    shader_ms = defaultdict(list)  # seg -> [ms of each pipeline/shader compile]
    cur = 0
    tl.anchor = None
    if log_path.exists():
        for line in open(log_path, encoding="utf-8", errors="replace"):
            cur = tl.log_seg(line, cur)
            if "[ht-track]" in line:
                continue
            if "[ht-shader-time]" in line:
                m = re.search(r"phase=(\w+).*?ms=([\d.]+)", line)
                if m:
                    shader_ms[cur].append((m.group(1), float(m.group(2))))
                continue
            if "[ht-trace]" in line or "[ht-tdraw]" in line:
                continue
            tag, kv = parse_log_line(line)
            if tag:
                for k, v in kv.items():
                    reports[cur][f"{tag}.{k}"].append(v)

    rows = []
    md = ["# Relatorio de desempenho por pista", "",
          f"Sessao: `{d.name}`  |  'estavel' = ignora os primeiros {skip:.0f} s de cada pista (carregamento + largada).", ""]
    info = (d / "info.txt").read_text(encoding="utf-8", errors="replace") if (d / "info.txt").exists() else ""
    if prof_meta:
        md.append(f"- Perfil de CPU: {prof_meta}")
    for key in ("build:", "cpu model name", "os PRETTY_NAME", "MemTotal", "sysfs gpu:", "kernel:", "cpu profiler:"):
        for line in info.splitlines():
            if line.startswith(key):
                md.append(f"- {line}")
                break
    for line in info.splitlines():
        if line.startswith("mapped ") and "renderdoc" in line.lower():
            md.append(f"- **ATENCAO: RenderDoc carregado no processo**: {line[7:]}")
    md.append("")

    for s in sorted(segs, key=lambda k: segs[k]["order"]):
        seg = segs[s]
        fr = frames.get(s, [])
        if not fr:
            continue
        t0 = seg["start"]
        dur = fr[-1][0] - fr[0][0]
        ms_all = [m for _, m in fr]
        steady = [m for t, m in fr if t - t0 >= skip]
        ms = steady if len(steady) > 100 else ms_all
        rep = reports.get(s, {})

        def R(key, agg=mean):
            v = rep.get(key)
            return agg(v) if v else None

        sysr = system.get(s, [])
        steady_sys = [r for r in sysr if float(r["t_s"]) - t0 >= skip] or sysr

        def S(col, agg=mean):
            return agg([num(r.get(col)) for r in steady_sys]) if steady_sys else None

        def smax(xs):
            xs = [x for x in xs if x is not None]
            return max(xs) if xs else None

        avg_ms = statistics.fmean(ms) if ms else None
        fps = 1000.0 / avg_ms if avg_ms else None
        load_hitch = max((m for t, m in fr if t - t0 < skip), default=None)
        h33 = sum(1 for m in ms if m > 33.4)
        h50 = sum(1 for m in ms if m > 50)
        h100 = sum(1 for m in ms if m > 100)
        minutes = max(1e-9, (sum(ms) / 1000.0) / 60.0)
        compiles = shader_ms.get(s, [])
        pipe_ms = [ms_ for ph, ms_ in compiles if ph == "pipeline"]
        new_pipes = len(pipe_ms) if compiles else None
        # Threads: average CPU% per thread name over the steady samples.
        per_name = defaultdict(list)
        samples = len(steady_sys) or 1
        for (ts, name), vals in threads.get(s, {}).items():
            if float(ts) - t0 >= skip or not steady_sys:
                per_name[name].append(sum(vals))
        top_threads = sorted(((sum(v) / samples, n) for n, v in per_name.items()), reverse=True)[:8]

        gpu_ms = R("ht-perf.gpu_total")
        cp_busy = R("ht-cpu.cp_busy")
        frame_cpu = R("ht-cpu.frame")
        gpu_busy_sys = S("gpu_busy_pct")
        gpu_busy_span = R("ht-gpu.busy_pct")
        vram_used = R("ht-vram.device_used", smax)
        vram_budget = R("ht-vram.device_budget", smax)

        # Diagnosis (heuristic, to direct the next optimization).
        notes = []
        if gpu_busy_sys is not None and gpu_busy_sys >= 90:
            notes.append(f"GPU no limite ({gpu_busy_sys:.0f}% ocupada): pista limitada pela GPU")
        elif gpu_ms and avg_ms and gpu_ms > 0.85 * avg_ms:
            notes.append(f"GPU gasta {gpu_ms:.1f} de {avg_ms:.1f} ms por quadro: limitada pela GPU")
        if top_threads and top_threads[0][0] >= 90:
            notes.append(f"thread '{top_threads[0][1]}' a {top_threads[0][0]:.0f}% de um nucleo: limite de CPU nessa thread")
        if vram_used and vram_budget and vram_used > 0.9 * vram_budget:
            notes.append(f"VRAM perto do limite ({vram_used:.0f}/{vram_budget:.0f} MB)")
        if new_pipes and new_pipes > 0 and h50:
            notes.append(f"{new_pipes:.0f} pipelines/shaders novos criados durante a pista (travadas de compilacao)")
        if S("majflt_per_s", smax) and S("majflt_per_s", smax) > 50:
            notes.append("leitura de disco/page faults altos (travadas de carregamento/swap)")
        if S("sys_swap_used_mb", smax) and S("sys_swap_used_mb", smax) > 256:
            notes.append(f"sistema usando swap ({S('sys_swap_used_mb', smax):.0f} MB)")
        for k, label in (("ht-perf.gpu_resolve", "resolve"), ("ht-perf.gpu_texload", "carga de texturas"),
                         ("ht-perf.gpu_transfers", "transferencias de RT")):
            v = R(k)
            if v and gpu_ms and v > 0.2 * gpu_ms:
                notes.append(f"{label} = {v:.2f} ms/quadro ({100 * v / gpu_ms:.0f}% da GPU)")
        if h33 / minutes > 6:
            notes.append(f"{h33 / minutes:.1f} quadros >33 ms por minuto (engasgos perceptiveis)")

        row = {
            "ordem": seg["order"], "segmento": s, "pista_id": seg["id"], "pista": seg["name"],
            "reinicios": seg["reloads"], "duracao_s": round(dur, 1), "quadros": len(ms_all),
            "fps_medio": fps, "fps_1pct_low": low_fps(ms, 0.01), "fps_0_1pct_low": low_fps(ms, 0.001),
            "ms_p50": pct(ms, 50), "ms_p95": pct(ms, 95), "ms_p99": pct(ms, 99), "ms_p99_9": pct(ms, 99.9),
            "ms_max": max(ms) if ms else None, "travada_carregamento_ms": load_hitch,
            "quadros_33ms": h33, "quadros_50ms": h50, "quadros_100ms": h100,
            "gpu_ms_quadro": gpu_ms, "gpu_resolve_ms": R("ht-perf.gpu_resolve"),
            "gpu_texload_ms": R("ht-perf.gpu_texload"), "gpu_transfers_ms": R("ht-perf.gpu_transfers"),
            "gpu_upload_ms": R("ht-perf.gpu_upload"), "gpu_ocupada_span_pct": gpu_busy_span,
            "cp_ocupada_ms": cp_busy, "cp_esperando_jogo_ms": R("ht-cpu.cp_idle"), "quadro_cpu_ms": frame_cpu,
            "cp_espera_gpu_ms": R("ht-cpu.cp_gpu_fence"), "desenhos_quadro": R("ht-perf.pf_draws"),
            "renderpasses_quadro": R("ht-perf.pf_rp"), "resolves_quadro": R("ht-perf.pf_resolves"),
            "upload_kb_quadro": R("ht-perf.pf_upload_kb"), "issuedraw_cpu_ms": R("ht-draw.total"),
            "pipelines_novos": new_pipes, "vram_usada_max_mb": vram_used, "vram_orcamento_mb": vram_budget,
            "gpu_ocupada_sysfs_pct": gpu_busy_sys, "gpu_clock_mhz": S("gpu_sclk_mhz"),
            "vram_sysfs_max_mb": S("vram_used_mb", smax), "gtt_max_mb": S("gtt_used_mb", smax),
            "cpu_processo_pct": S("proc_cpu_pct"), "cpu_sistema_pct": S("sys_cpu_pct"),
            "cpu_mhz": S("cpu_mhz_avg"), "ram_processo_max_mb": S("rss_mb", smax),
            "ram_sistema_usada_max_mb": S("sys_mem_used_mb", smax), "swap_max_mb": S("sys_swap_used_mb", smax),
            "gpu_temp_max_c": S("gpu_temp_c", smax), "cpu_temp_max_c": S("cpu_temp_c", smax),
            "gpu_watts": S("gpu_power_w"), "bateria_watts": S("battery_w"), "fan_rpm": S("fan_rpm"),
            "diagnostico": " ; ".join(notes),
        }
        rows.append(row)

        md += [f"## {seg['order']}. {seg['name']}  (`{seg['id']}`, segmento {s})", ""]
        md.append(f"- Tempo: {dur / 60:.1f} min, {len(ms_all)} quadros, reinicios: {seg['reloads']}")
        md.append(f"- **FPS medio {fmt(fps)}** | 1% low {fmt(row['fps_1pct_low'])} | 0,1% low {fmt(row['fps_0_1pct_low'])}")
        md.append(f"- Tempo de quadro ms: p50 {fmt(row['ms_p50'], 2)} / p95 {fmt(row['ms_p95'], 2)} / p99 {fmt(row['ms_p99'], 2)} "
                  f"/ p99,9 {fmt(row['ms_p99_9'], 2)} / max {fmt(row['ms_max'])}")
        md.append(f"- Engasgos (estavel): >33 ms: {h33}  >50 ms: {h50}  >100 ms: {h100}  | maior travada no carregamento: {fmt(load_hitch, 0)} ms")
        md.append(f"- GPU: {fmt(gpu_ms, 2)} ms/quadro (resolve {fmt(row['gpu_resolve_ms'], 2)}, texturas {fmt(row['gpu_texload_ms'], 2)}, "
                  f"transfer {fmt(row['gpu_transfers_ms'], 2)}, upload {fmt(row['gpu_upload_ms'], 2)}) | ocupada {fmt(gpu_busy_sys, 0)}% (sysfs) "
                  f"| clock {fmt(row['gpu_clock_mhz'], 0)} MHz | {fmt(row['gpu_watts'], 1)} W | {fmt(row['gpu_temp_max_c'], 0)} C")
        md.append(f"- CPU thread de comandos: ocupada {fmt(cp_busy, 2)} ms, esperando o jogo {fmt(row['cp_esperando_jogo_ms'], 2)} ms, "
                  f"esperando GPU {fmt(row['cp_espera_gpu_ms'], 2)} ms | desenhos/quadro {fmt(row['desenhos_quadro'], 0)}, "
                  f"render passes {fmt(row['renderpasses_quadro'], 1)}, resolves {fmt(row['resolves_quadro'], 1)}")
        md.append(f"- Memoria: VRAM {fmt(vram_used, 0)}/{fmt(vram_budget, 0)} MB (Vulkan), sysfs VRAM {fmt(row['vram_sysfs_max_mb'], 0)} + GTT {fmt(row['gtt_max_mb'], 0)} MB "
                  f"| RAM do jogo {fmt(row['ram_processo_max_mb'], 0)} MB | swap {fmt(row['swap_max_mb'], 0)} MB")
        md.append(f"- CPU: processo {fmt(row['cpu_processo_pct'], 0)}% de 1 nucleo, sistema {fmt(row['cpu_sistema_pct'], 0)}%, "
                  f"{fmt(row['cpu_mhz'], 0)} MHz, {fmt(row['cpu_temp_max_c'], 0)} C | bateria {fmt(row['bateria_watts'], 1)} W | shaders novos: {fmt(new_pipes, 0)}")
        if top_threads:
            md.append("- Threads mais ocupadas: " + ", ".join(f"{n} {v:.0f}%" for v, n in top_threads))
        if notes:
            md.append("- **Diagnostico:** " + "; ".join(notes))
        if compiles:
            by_phase = defaultdict(list)
            for ph, m_ in compiles:
                by_phase[ph].append(m_)
            md.append("- Compilacao de shaders/pipelines: " + ", ".join(
                f"{ph} {len(v)}x total {sum(v):.0f} ms max {max(v):.0f} ms (>=10 ms: {sum(1 for x in v if x >= 10)})"
                for ph, v in by_phase.items()))
        # GPU per pass (traced frames).
        if traces.get(s):
            md.append(f"- **GPU por etapa** ({len(traces[s])} quadro(s) rastreado(s)):")
            for tr in traces[s]:
                md += trace_report(tr)
        # CPU per function.
        if s in tl.missed:
            md.append("- CPU por funcao e GPU por etapa: nao separados nesta sessao (a build nao reconheceu esta pista; "
                      "o perfil dela esta somado ao da pista anterior reconhecida)")
        if prof_self.get(s):
            mixed = s + 1 in tl.missed
            md.append("- **CPU por funcao** (amostras de 1 ms de CPU por thread; % = do tempo daquela thread)"
                      + (" - ATENCAO: inclui as pistas seguintes nao reconhecidas:" if mixed else ":"))
            threads_by_n = sorted(((sum(fs.values()), th) for th, fs in prof_self[s].items()), reverse=True)[:5]
            for n_th, th in threads_by_n:
                top = sorted(((n, fn) for fn, n in prof_self[s][th].items()), reverse=True)[:12]
                md.append(f"  - thread `{th}` ({n_th} amostras = {n_th / 1000:.1f} s de CPU):")
                for n, fn in top:
                    md.append(f"    - {100 * n / n_th:5.1f}%  {fn}")
                inc = prof_incl.get(s, {}).get(th)
                if inc:
                    topi = sorted(((n, fn) for fn, n in inc.items()), reverse=True)[:6]
                    md.append("    - inclusivo (com o que ela chama; so onde a pilha e legivel): " +
                              "; ".join(f"{fn} {100 * n / n_th:.0f}%" for n, fn in topi))
        worst = sorted(((m, t) for t, m in fr if m > 50), reverse=True)[:6]
        if worst:
            md.append("- Piores quadros:")
            hr = hitch_rows.get(s, {})
            for m, t in worst:
                near = [p for ot, p in opens if abs(ot - t) < 0.75]
                md.append(f"  - {m:.0f} ms em {t - t0:.1f} s da pista" + (f" (arquivos abertos perto: {', '.join(near[:3])})" if near else ""))
                key = min(hr, key=lambda k: abs(k[0] - t), default=None)
                if key and abs(key[0] - t) < 0.01:
                    per = defaultdict(lambda: defaultdict(int))
                    for r in hr[key]:
                        if r["module"] and r["module"] != "(no samples)":
                            per[r["thread"]][syms.name(r["module"], int(r["offset"] or "0", 16), r.get("sym"))] += int(r["samples"] or 1)
                    for th, fs in sorted(per.items(), key=lambda kv: -sum(kv[1].values()))[:3]:
                        tot = sum(fs.values())
                        md.append(f"    - `{th}` ({tot} amostras): " + "; ".join(
                            f"{fn} {n}" for fn, n in sorted(fs.items(), key=lambda kv: -kv[1])[:3]))
        md.append("")

    if rows:
        with open(d / "summary.csv", "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            for r in rows:
                w.writerow({k: (f"{v:.3f}" if isinstance(v, float) else v) for k, v in r.items()})
        md += ["## Ranking (mais pesada primeiro, por 1% low)", "", "| pista | FPS medio | 1% low | GPU ms | engasgos >33ms/min | diagnostico |", "|---|---|---|---|---|---|"]
        for r in sorted(rows[1:] or rows, key=lambda r: r["fps_1pct_low"] or 0):
            mins = max(1e-9, r["duracao_s"] / 60)
            md.append(f"| {r['pista']} | {fmt(r['fps_medio'])} | {fmt(r['fps_1pct_low'])} | {fmt(r['gpu_ms_quadro'], 2)} | "
                      f"{r['quadros_33ms'] / mins:.1f} | {r['diagnostico'] or '-'} |")
    (d / "report.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
