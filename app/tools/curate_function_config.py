#!/usr/bin/env python3
"""
Rebuild config/hydrothunder_functions.toml from the binary's own .pdata table.

Why this exists
---------------
The original config was produced by ExportRexGlueFunctions.java, which dumps
every function Ghidra's FunctionManager knows about as a name-only entry:

    "0x82120018" = { name = "sub_82120018" }

A name-only entry still registers the address with FunctionAuthority::CONFIG,
and ReXGlue's phase_register.cpp skips any .pdata record for an address that is
already in the graph:

    if (graph.getFunction(beginAddr) != nullptr) { continue; }   // :600

So every such entry silently discards that function's .pdata-derived size *and*
everything gated behind it: parseExceptionInfo(), setFunctionHasExceptionHandler(),
SEH try/handler scope labels, C++ ip-to-state map labels, prologue frame-size
recovery, and the marking of the 8 bytes of EH metadata that precede an EH
function as data rather than instructions.

Measured against game_data/default.xex: 14,240 of 14,258 .pdata records were
being shadowed, including 81 of 81 records with ExceptionFlag set.

The rule this script applies
----------------------------
An entry earns its place only if it tells ReXGlue something it cannot derive.

  DROP  address already present in .pdata      -> ReXGlue derives name+size+EH
  DROP  address inside a .pdata function body  -> mid-function split, corrupts
                                                  fall-through and register state
  DROP  address whose first word is zero       -> alignment padding, not code;
                                                  codegen emits an empty stub
  KEEP  address in a .pdata gap holding code   -> a function .pdata genuinely
                                                  misses (leaf, or a thunk)
  KEEP  address inside .embsec_* sections      -> not covered by .pdata at all
  KEEP  anything in --overrides                -> hand-authored, always wins

Kept entries are written WITHOUT a name. ReXGlue auto-generates sub_XXXXXXXX
(phase_register.cpp:551), which also fixes the 1,958 entries whose Ghidra-derived
lowercase name disagreed with their own uppercase address.

Output uses unquoted 0x keys to match the format the SDK's own round-trip script
(scripts/ida/export_named_funcs.py) expects.

Usage
-----
    python tools/curate_function_config.py \
        --image     ../basefile.pe \
        --old       config/hydrothunder_functions.toml.ghidra-export.bak \
        --overrides config/hydrothunder_overrides.toml \
        --out       config/hydrothunder_functions.toml

`--old` should stay pointed at the original Ghidra export, not at the generated
output, so the run is reproducible from the same input every time.

`--image` wants the flat virtual-address image produced by xextool: file offset
== VA - 0x82000000 for every section. The script verifies this before trusting
it and refuses to run if the assumption does not hold.
"""

import argparse
import bisect
import re
import struct
import sys
from pathlib import Path

IMAGE_BASE = 0x82000000

# A note on what precedes these addresses, since it is tempting to filter on it:
# of the 2,443 kept starts that sit in a .pdata gap, 1,217 follow zero alignment
# padding, 674 follow blr, 529 follow b/bl, 17 follow bctr/bctrl, and only 6
# follow an ordinary instruction. Filtering the last 6 out looks reasonable and
# is wrong - three of them are shared epilogues that other code branches into,
# and removing them produced unresolved-call errors from codegen. Gap addresses
# shadow no .pdata record, so there is nothing to gain by dropping them.


# ---------------------------------------------------------------- image access

class FlatImage:
    """A PE loaded as a flat VA image, where file offset == RVA."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.sections = self._parse_sections()
        self._verify_flat()

    def _parse_sections(self):
        d = self.data
        if d[:2] != b"MZ":
            sys.exit(f"error: {len(d)} bytes read but no MZ header - not a PE image")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            sys.exit("error: no PE signature")
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        base = pe + 24 + optsz
        out = []
        for i in range(nsec):
            o = base + i * 40
            name = d[o:o + 8].rstrip(b"\0").decode("latin1")
            vsize, rva, rawsize, rawoff = struct.unpack_from("<IIII", d, o + 8)
            chars = struct.unpack_from("<I", d, o + 36)[0]
            out.append({
                "name": name,
                "va": IMAGE_BASE + rva,
                "vsize": vsize,
                "rva": rva,
                "rawoff": rawoff,
                "exec": bool(chars & 0x20000000),
            })
        return out

    def _verify_flat(self):
        """
        xextool emits a loaded-image dump, so offset == RVA. Confirm on a section
        whose rawoff differs from its RVA - if the file were raw-offset laid out,
        reading at the RVA would return something that does not decode.
        """
        text = self.section(".text")
        if text is None:
            sys.exit("error: no .text section")
        probe = self.u32(text["va"])
        if probe is None:
            sys.exit("error: .text VA not addressable under the flat mapping")
        # Every Xbox 360 function region starts with decodable code; a zero word
        # here means the flat assumption is wrong for this file.
        if probe == 0:
            sys.exit(
                "error: flat VA mapping looks wrong - .text starts with a zero word.\n"
                "       This image may use raw section offsets instead. Aborting rather\n"
                "       than producing a config from garbage."
            )

    def section(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def exec_sections(self):
        return [s for s in self.sections if s["exec"]]

    def u32(self, va):
        off = va - IMAGE_BASE
        if 0 <= off <= len(self.data) - 4:
            return struct.unpack_from(">I", self.data, off)[0]
        return None

    def in_exec_section(self, va):
        return any(s["va"] <= va < s["va"] + s["vsize"] for s in self.exec_sections())


# ------------------------------------------------------------------ pdata scan

def read_pdata(img: FlatImage):
    """
    Parse the .pdata exception directory.

    Each record is 8 bytes big-endian:
        uint32 BeginAddress
        uint32 { PrologLength:8, FunctionLength:22, ThirtyTwoBit:1, ExceptionFlag:1 }

    Layout mirrors IMAGE_CE_RUNTIME_FUNCTION in phase_register.cpp:43.
    """
    sec = img.section(".pdata")
    if sec is None:
        sys.exit("error: no .pdata section - cannot derive function boundaries")
    out = []
    for i in range(sec["vsize"] // 8):
        off = sec["rva"] + i * 8
        begin, data = struct.unpack_from(">II", img.data, off)
        if begin == 0:
            continue
        length = ((data >> 8) & 0x3FFFFF) * 4
        out.append({
            "addr": begin,
            "size": length if length else 4,
            "eh": bool((data >> 31) & 1),
        })
    out.sort(key=lambda r: r["addr"])
    return out


# ----------------------------------------------------------------- config i/o

ENTRY_RE = re.compile(r'^"?(0[xX][0-9A-Fa-f]+)"?\s*=\s*\{(.*)\}\s*$')
KV_RE = re.compile(r'(\w+)\s*=\s*("(?:[^"\\]|\\.)*"|0[xX][0-9A-Fa-f]+|\d+)')


def read_config(path: Path):
    """Return {addr: {key: raw_value}} for every [functions] entry."""
    entries = {}
    in_functions = False
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_functions = stripped == "[functions]"
            continue
        if not in_functions:
            continue
        m = ENTRY_RE.match(stripped)
        if not m:
            continue
        addr = int(m.group(1), 16)
        props = {k: v for k, v in KV_RE.findall(m.group(2))}
        entries[addr] = props
    return entries



def find_pdata_gap_starts(img, pdata, known):
    """
    Function starts that live in .pdata gaps.

    .pdata does not describe every function in this image. Where a record ends
    before the next one begins, the bytes in between are often a real function
    that nothing declares - not .pdata, and not the Ghidra export either. Static
    analysis cannot recover them when the only caller reaches them through a
    function pointer, and the result at runtime is:

        [FATAL] Call to invalid or unregistered function at guest address 0x8252E5C8

    An address is accepted when it holds code (a non-zero word) and whatever
    precedes it ended a function - a return, an unconditional branch, or
    alignment padding. Because these addresses shadow no .pdata record,
    declaring them costs nothing even when a guess is wrong.

    A gap frequently holds SEVERAL functions, not one - runs of dispatch thunks
    are packed back to back. Declaring only the gap's first address misses every
    one after it, which is how 0x821C55E0 and 0x8228A548 stayed unregistered.
    Walk the whole gap, starting a new function after each hard terminator.
    """
    # Instructions that end a function. `b` (opcode 18) counts only with LK=0;
    # `bl` is a call and does not end anything.
    def terminates(word):
        if word in (0x4E800020, 0x4E800420):      # blr, bctr
            return True
        return (word >> 26) == 18 and (word & 1) == 0   # b / ba, not bl / bla

    def hard_terminator(word):
        """
        blr / bctr only. These unconditionally leave the function.

        A plain `b` deliberately does NOT count: it is an intra-function jump
        (switch dispatch, loop back-edge, tail-merge). Treating it as a boundary
        strands switch-case bodies that sit in a gap when a .pdata size
        understates its function - observed at 0x8214B290, whose parent record
        0x826A1B0A0 claims only 360 bytes.
        """
        return word in (0x4E800020, 0x4E800420)

    def branch_target(addr, word):
        """Target of a b/ba/bl/bla (op 18) or bc/bca (op 16), else None."""
        op = word >> 26
        if op == 18:
            li = word & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            return (li if (word & 2) else addr + li) & 0xFFFFFFFF
        if op == 16:
            bd = word & 0x0000FFFC
            if bd & 0x8000:
                bd -= 0x10000
            return (bd if (word & 2) else addr + bd) & 0xFFFFFFFF
        return None

    out = []
    for i in range(len(pdata) - 1):
        gap_start = pdata[i]["addr"] + pdata[i]["size"]
        gap_end = pdata[i + 1]["addr"]
        if gap_start >= gap_end:
            continue

        # 1. The gap's own first address, when what precedes it ended a function.
        prev = img.u32(gap_start - 4)
        if (img.u32(gap_start) and prev is not None
                and (prev == 0 or hard_terminator(prev) or (prev >> 26) == 18)
                and gap_start not in known):
            out.append(gap_start)

        # 2. Split inside the gap only where nothing earlier still reaches.
        #
        # A terminator alone is not enough. Large hand-written routines - memcpy
        # and memset live in the gap at 0x826A1E64 - have SEVERAL `blr` exits,
        # and the code after each one is still the same function, reached by a
        # forward branch. Splitting there orphaned those targets and produced
        # "target not in any function" errors for 0x826A20C8 / 0x826A21F8.
        #
        # So track how far anything seen so far branches. A boundary is real only
        # once reach has been satisfied - the standard function-extent condition.
        # Runs of dispatch thunks satisfy it after every thunk; memcpy does not
        # until its final return.
        reach = gap_start
        addr = gap_start
        while addr < gap_end:
            word = img.u32(addr)
            if word is None:
                break
            tgt = branch_target(addr, word)
            if tgt is not None and gap_start <= tgt < gap_end and tgt > reach:
                reach = tgt

            nxt = addr + 4
            if (nxt < gap_end and hard_terminator(word) and reach <= addr
                    and nxt not in known):
                w2 = img.u32(nxt)
                if w2 and (w2 >> 26) != 0:
                    out.append(nxt)
            addr = nxt
    return out


def find_code_pointers_in_data(img, pdata, known):
    """
    Function starts reachable only through a pointer stored in data.

    Virtual dispatch and callback tables put code addresses in .rdata/.data, and
    the thunks they point at come in several shapes - a 4-instruction
    `lwz;lwz;mtctr;bctr`, a 6-instruction adjustor that folds in an offset, and
    others. Pattern-matching each shape is a losing game; matching the *pointer*
    is not. Anything the program stores as a code address is a function entry by
    definition.

    Filters, in order of how much they matter:
      - 4-byte aligned and inside an executable section
      - not already known from .pdata or the config
      - NOT strictly inside a .pdata function body; that is the one case where a
        false positive would fragment a real function
      - points at a decodable instruction

    The last filter matters more than it looks. XEX import thunks live at the end
    of .text and begin with two loader-patched marker words:

        010001A6 / 020001A6 / mtctr r11 / bctr

    0x010001A6 is opcode 0 - not an instruction. Codegen deliberately emits no
    body for anything in the import range, so declaring one as a function yields
    a link error (undefined symbol: sub_827938D4). Rejecting opcode 0 excludes
    the whole thunk table without needing to locate its bounds.

    A wrong guess that survives all four lands in a .pdata gap, shadows no
    record, and costs nothing.
    """
    pd_addrs = [r["addr"] for r in pdata]

    def inside_body(x):
        i = bisect.bisect_right(pd_addrs, x) - 1
        if i < 0:
            return False
        rec = pdata[i]
        return rec["addr"] < x < rec["addr"] + rec["size"]

    out = set()
    for sec in img.sections:
        if sec["exec"] or sec["name"] not in (".rdata", ".data"):
            continue
        for off in range(0, max(sec["vsize"] - 3, 0), 4):
            value = img.u32(sec["va"] + off)
            if value is None or value & 3:
                continue
            if value in known or not img.in_exec_section(value):
                continue
            first = img.u32(value)
            if not first or (first >> 26) == 0:   # padding, or an import thunk
                continue
            if inside_body(value):
                continue
            out.add(value)
    return sorted(out)


def curate(img, pdata, config):
    pd_addrs = [r["addr"] for r in pdata]
    pd_set = set(pd_addrs)

    kept, dropped = {}, {"in_pdata": [], "split": [], "stub": [], "not_code": []}

    for addr, props in sorted(config.items()):
        # Entries carrying real boundary information are always preserved -
        # they are the only kind that adds anything ReXGlue cannot derive.
        informative = any(k in props for k in ("end", "size", "parent"))

        if addr in pd_set and not informative:
            dropped["in_pdata"].append(addr)
            continue

        # An address whose first word is zero is inter-function alignment
        # padding, not a function. ReXGlue cannot build a code region there and
        # emits an empty stub - a function that silently does nothing when
        # called. On this binary the test is exact: all 112 addresses codegen
        # reported as "has no blocks - generating stub" start with 0x00000000,
        # and every address that starts with real code produced a real function.
        #
        # This deliberately replaces an earlier rule that dropped everything
        # listed in missing_funcs.txt. That file holds 119 addresses but only 112
        # of them stub; the other 7 are real code, and dropping them cost the
        # build a live indirect-call target (0x82792238, a lis/addi/b thunk),
        # which failed at runtime as:
        #   [FATAL] Call to invalid or unregistered function at 0x82792238
        if not informative and img.u32(addr) == 0:
            dropped["stub"].append(addr)
            continue

        if not img.in_exec_section(addr):
            dropped["not_code"].append(addr)
            continue

        # Mid-function split: strictly inside a .pdata function's body.
        #
        # This is the only address class worth dropping. The function is already
        # fully described by its .pdata record, and declaring an extra start
        # inside it fragments the body - fall-through becomes a call/return
        # boundary and live register state is lost across it. ReXGlue turns the
        # address into a label within the function instead, which is correct.
        i = bisect.bisect_right(pd_addrs, addr) - 1
        if i >= 0 and not informative:
            rec = pdata[i]
            if rec["addr"] < addr < rec["addr"] + rec["size"]:
                dropped["split"].append(addr)
                continue

        # Anything else lives in a .pdata gap, so there is no record to shadow
        # and keeping it costs nothing. Dropping it, on the other hand, can leave
        # a branch target outside every known function - .pdata has real holes in
        # this binary (e.g. the shared `li r3,0; blr` epilogue at 0x823248CC,
        # which a `b` jumps back into from 0x823248DC). Keep gap starts even when
        # the preceding word is an ordinary instruction.
        #
        # Strip the Ghidra name: ReXGlue auto-generates sub_XXXXXXXX
        # (phase_register.cpp:551), and 1,958 exported names disagreed in case
        # with their own address.
        kept[addr] = {k: v for k, v in props.items() if k != "name"}

    return kept, dropped


def write_config(path: Path, kept, stats):
    lines = [
        "# Function boundary overrides for Hydro Thunder Hurricane codegen.",
        "#",
        "# Generated by tools/curate_function_config.py - do not hand-edit; re-run",
        "# the script instead so the rationale stays attached to the data.",
        "#",
        "# ReXGlue derives function names, sizes and exception metadata from the",
        "# binary's own .pdata table. An entry here SHADOWS that record entirely",
        "# (phase_register.cpp:600), so only list an address .pdata cannot describe:",
        "# a function start it misses, an explicit end/size correction, or a chunk",
        "# parent. Never add a name-only entry - it costs that function its size",
        "# and its exception-handler information for nothing.",
        "#",
        f"#   .pdata records left to ReXGlue : {stats['pdata_total']}",
        f"#   of which carry EH information  : {stats['pdata_eh']}",
        f"#   additional starts declared here: {len(kept)}",
        "",
        "[functions]",
        "",
    ]
    for addr in sorted(kept):
        props = kept[addr]
        if props:
            body = ", ".join(f"{k} = {v}" for k, v in props.items())
            lines.append(f"0x{addr:08X} = {{ {body} }}")
        else:
            lines.append(f"0x{addr:08X} = {{}}")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", required=True, type=Path)
    ap.add_argument("--old", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--overrides", type=Path, default=None,
                    help="hand-authored boundary corrections; merged in and always kept")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    img = FlatImage(args.image)
    pdata = read_pdata(img)
    config = read_config(args.old)
    overrides = read_config(args.overrides) if args.overrides else {}

    pd_eh = sum(1 for r in pdata if r["eh"])
    shadowed = sum(1 for r in pdata if r["addr"] in config)
    shadowed_eh = sum(1 for r in pdata if r["eh"] and r["addr"] in config)

    print(f"image     : {args.image}  ({len(img.data):,} bytes, flat VA mapping verified)")
    print(f"pdata     : {len(pdata):,} records, {pd_eh} with ExceptionFlag")
    print(f"old config: {len(config):,} entries")
    print(f"            shadowing {shadowed:,} pdata records "
          f"({shadowed_eh}/{pd_eh} of the EH ones)")
    print()

    kept, dropped = curate(img, pdata, config)

    gap_starts = find_pdata_gap_starts(img, pdata, set(kept))
    for addr in gap_starts:
        kept.setdefault(addr, {})

    known = set(kept) | {r["addr"] for r in pdata}
    code_ptrs = find_code_pointers_in_data(img, pdata, known)
    for addr in code_ptrs:
        kept.setdefault(addr, {})

    # Hand-authored corrections win outright, and are never subject to the
    # curation rules - they exist precisely because the rules got one wrong.
    for addr, props in overrides.items():
        kept[addr] = props
        for bucket in dropped.values():
            if addr in bucket:
                bucket.remove(addr)

    # An explicit extent wins over heuristically discovered starts inside it.
    # Otherwise multi-return assembly routines are split after their first blr,
    # leaving later switch arms unable to reach shared return blocks.
    for start, props in overrides.items():
        end = int(props.get("end", "0"), 0)
        if "size" in props:
            end = start + int(props["size"], 0)
        for addr in list(kept):
            if start < addr < end and addr not in overrides and not kept[addr]:
                del kept[addr]
                dropped["split"].append(addr)

    print("curation:")
    print(f"  dropped, already in .pdata          : {len(dropped['in_pdata']):,}")
    print(f"  dropped, mid-function split         : {len(dropped['split']):,}")
    print(f"  dropped, known empty-stub address   : {len(dropped['stub']):,}")
    print(f"  dropped, outside any exec section   : {len(dropped['not_code']):,}")
    print(f"  ADDED, function starts in .pdata gaps: {len(gap_starts):,}")
    print(f"  ADDED, code pointers found in data   : {len(code_ptrs):,}")
    print(f"  KEPT, starts .pdata does not cover  : {len(kept):,}")
    print()

    if dropped["split"]:
        print("  sample mid-function splits dropped:")
        for a in dropped["split"][:8]:
            print(f"    0x{a:08X}")
        print()

    if args.dry_run:
        print("dry run - nothing written")
        return

    write_config(args.out, kept, {"pdata_total": len(pdata), "pdata_eh": pd_eh})
    print(f"wrote {args.out}  ({len(kept):,} entries)")
    print(f"  {len(pdata):,} functions now derived from .pdata, "
          f"including all {pd_eh} with exception handlers")


if __name__ == "__main__":
    main()
