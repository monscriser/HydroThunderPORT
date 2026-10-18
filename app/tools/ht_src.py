#!/usr/bin/env python3
"""Static analysis over the recompiled Hydro Thunder source.

We have the source. Reading it beats scanning memory for most questions - it is
deterministic, needs no running game, and survives restarts. This indexes the
~122 MB of generated C++ once and then answers structural queries fast.

    python ht_src.py index                     # build/refresh the index
    python ht_src.py show sub_82120000         # print a function body
    python ht_src.py callers sub_8269CBF8      # who calls it
    python ht_src.py callees sub_82120000      # what it calls
    python ht_src.py xref 0x82960110           # who touches this guest address
    python ht_src.py grep "tan|atan"           # regex, attributed to functions

`xref` is the important one. PowerPC reaches static data with lis/addi, and the
recompiler emits `lis rN,imm` as a signed decimal:

    // lis r11,-32106
    ctx.r11.s64 = -2104098816;      <- 0x82960000

so a static address is findable by its constant. Note the +1 carry when the low
half has bit 15 set (addi sign-extends its immediate) - both candidates are
searched. Getting this arithmetic wrong once cost an entire investigation, so it
is computed here rather than by hand.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.normpath(os.path.join(HERE, "..", "generated"))
INDEX = os.path.join(HERE, ".ht_src_index.json")

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-Fa-f]+)\)")
CALL_RE = re.compile(r"\b(sub_[0-9A-Fa-f]+)\(ctx, base\)")


def gen_files():
    if not os.path.isdir(GEN):
        sys.exit("generated/ not found at %s" % GEN)
    return sorted(f for f in os.listdir(GEN)
                  if f.startswith("hydrothunder_recomp.") and f.endswith(".cpp"))


def build_index():
    """function -> [file, start_line, end_line] (1-based, end exclusive)."""
    funcs = {}
    for fn in gen_files():
        path = os.path.join(GEN, fn)
        with open(path, encoding="utf-8", errors="replace") as fh:
            cur, start = None, 0
            for i, line in enumerate(fh, 1):
                m = FUNC_RE.match(line)
                if m:
                    if cur:
                        funcs[cur] = [fn, start, i - 1]
                    cur, start = m.group(1), i
            if cur:
                funcs[cur] = [fn, start, i]
    json.dump(funcs, open(INDEX, "w"))
    return funcs


def load_index(rebuild=False):
    if rebuild or not os.path.exists(INDEX):
        return build_index()
    return json.load(open(INDEX))


def body(idx, name):
    if name not in idx:
        sys.exit("unknown function %s (try: index)" % name)
    fn, a, b = idx[name]
    lines = open(os.path.join(GEN, fn), encoding="utf-8", errors="replace").read().splitlines()
    return fn, a, lines[a - 1:b]


def lis_constants(addr):
    """Signed 32-bit constants a `lis` would load to reach `addr`.

    Returns (candidates, low_half). Two candidates because addi sign-extends:
    if the low half has bit 15 set the high half is pre-incremented.
    """
    hi, lo = (addr >> 16) & 0xFFFF, addr & 0xFFFF
    cands = [hi]
    if lo & 0x8000:
        cands.append((hi + 1) & 0xFFFF)
    out = []
    for h in cands:
        v = (h << 16) & 0xFFFFFFFF
        out.append(v - (1 << 32) if v & 0x80000000 else v)
    return out, lo


def cmd_xref(idx, addr, mode="any"):
    """Find code that reaches `addr` via lis+offset.

    Precision matters: the offset must be applied to the SAME register the lis
    loaded, otherwise every unrelated `+ 272` in the file is a false positive.
    We therefore parse the register out of `ctx.rN.s64 = <const>;` and only
    accept `ctx.rN.u32 + <offset>` within the following window, optionally
    filtered to stores (writers) or loads (readers).
    """
    consts, lo = lis_constants(addr)
    signed_lo = lo - 0x10000 if lo & 0x8000 else lo
    print("guest 0x%08X -> lis constants %s ; offset %d (0x%X) ; mode=%s"
          % (addr, consts, signed_lo, lo, mode))
    lis_re = re.compile(r"ctx\.r(\d+)\.s64 = (-?\d+);")
    # "addr" matches materialising the address itself (lis+addi), which is how
    # a pointer to static data - a string literal, a table base - is formed.
    # The load/store modes match dereferencing it at an offset instead.
    kinds = {"any": r"(REX_STORE_\w+|REX_LOAD_\w+)", "write": r"REX_STORE_\w+",
             "read": r"REX_LOAD_\w+", "addr": None}[mode]
    hits = []
    for fn in gen_files():
        text = open(os.path.join(GEN, fn), encoding="utf-8", errors="replace").read()
        if not any(str(c) in text for c in consts):
            continue
        lines = text.splitlines()
        bounds = sorted(((v[1], v[2], k) for k, v in idx.items() if v[0] == fn))
        for i, line in enumerate(lines, 1):
            m = lis_re.search(line)
            if not m or int(m.group(2)) not in consts:
                continue
            reg = m.group(1)
            # the register can be reassigned; stop the window at the next write to it
            use = re.compile(r"%s\(ctx\.r%s\.u32 \+ %d" % (kinds, reg, signed_lo))
            reassign = re.compile(r"ctx\.r%s\.(s64|u64) = " % reg)
            for j in range(i, min(i + 40, len(lines))):
                l = lines[j]
                if use.search(l):
                    owner = next((nm for a, b, nm in bounds if a <= i <= b), "?")
                    hits.append((owner, fn, j + 1, l.strip()[:70]))
                    break
                if j > i and reassign.search(l):
                    break
    if not hits:
        print("no hits — the address may be reached through a pointer rather "
              "than lis+offset, or the offset is applied after an addi chain")
    for owner, fn, line, txt in hits[:40]:
        print("  %-24s %s:%-6d %s" % (owner, fn, line, txt))
    print("%d hit(s)" % len(hits))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["index", "show", "callers", "callees", "xref", "grep"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--rebuild", action="store_true")
    ap.add_argument("--mode", default="any", choices=["any", "read", "write", "addr"],
                    help="xref: match stores, loads, or either")
    a = ap.parse_args()

    if a.cmd == "index":
        idx = build_index()
        print("indexed %d functions across %d files" % (len(idx), len(gen_files())))
        return

    idx = load_index(a.rebuild)
    if not a.arg:
        sys.exit("%s needs an argument" % a.cmd)

    if a.cmd == "show":
        fn, start, lines = body(idx, a.arg)
        print("// %s  %s:%d  (%d lines)" % (a.arg, fn, start, len(lines)))
        print("\n".join(lines))

    elif a.cmd == "callees":
        _, _, lines = body(idx, a.arg)
        seen = []
        for l in lines:
            for m in CALL_RE.finditer(l):
                if m.group(1) not in seen:
                    seen.append(m.group(1))
        print("\n".join("  " + s for s in seen) or "  (none)")
        print("%d callee(s)" % len(seen))

    elif a.cmd == "callers":
        target = re.compile(r"\b%s\(ctx, base\)" % re.escape(a.arg))
        found = []
        for fn in gen_files():
            text = open(os.path.join(GEN, fn), encoding="utf-8", errors="replace").read()
            if a.arg not in text:
                continue
            lines = text.splitlines()
            bounds = sorted(((v[1], v[2], k) for k, v in idx.items() if v[0] == fn))
            for i, line in enumerate(lines, 1):
                if target.search(line):
                    owner = next((nm for x, y, nm in bounds if x <= i <= y), "?")
                    if owner not in [f[0] for f in found]:
                        found.append((owner, fn, i))
        for owner, fn, i in found[:60]:
            print("  %-28s %s:%d" % (owner, fn, i))
        print("%d caller(s)" % len(found))

    elif a.cmd == "xref":
        cmd_xref(idx, int(a.arg, 0), a.mode)

    elif a.cmd == "grep":
        rx = re.compile(a.arg)
        n = 0
        for fn in gen_files():
            lines = open(os.path.join(GEN, fn), encoding="utf-8",
                         errors="replace").read().splitlines()
            bounds = sorted(((v[1], v[2], k) for k, v in idx.items() if v[0] == fn))
            for i, line in enumerate(lines, 1):
                if rx.search(line):
                    owner = next((nm for x, y, nm in bounds if x <= i <= y), "?")
                    print("  %-28s %s:%d  %s" % (owner, fn, i, line.strip()[:90]))
                    n += 1
                    if n >= 60:
                        print("  ... (truncated)")
                        return
        print("%d match(es)" % n)


if __name__ == "__main__":
    main()
