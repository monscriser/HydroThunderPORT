#!/usr/bin/env python3
"""Read and edit Hydro Thunder Hurricane save files (ht2save.dat).

The save is a typed name/value tree, big-endian throughout:

    magic  'AKJB'
    u32    version (1)
    <node>

    <node> := u32 type, then:
        type 1 -> u32 value
        type 2 -> f32 value (lap / total times)
        type 3 -> u8  value (bool)
        type 4 -> u32 length + chars (string)
        type 6 -> u32 count, then `count` * (u32 keylen + key chars, <node>)

Only 0.4% of the 256 KB file is non-zero; the rest is padding.

    python ht_save_edit.py <save.dat>                    # dump the tree
    python ht_save_edit.py <save.dat> --set KEY=VALUE    # edit in place

KEY is the leaf name (e.g. TotalCredits) or a slash path
(e.g. Data/Unlock/TotalCredits) when a leaf name is ambiguous.

WARNING: the file carries a Crc32 field and the game DOES validate it. The
algorithm is custom - not zlib/BZIP2/MPEG-2/POSIX/JAMCRC/CRC-32C/adler32 over
any range tried - and is not implemented here. Writing without fixing it makes
the game reject the save and reset your progress. --set is therefore unsafe
until the checksum is solved; backups go to ~/.ht_save_backups/.
"""
import argparse
import os
import shutil
import struct
import sys
import time

MAGIC = b"AKJB"
T_NONE, T_U32, T_FLOAT, T_BOOL, T_STR, T_CONTAINER = 0, 1, 2, 3, 4, 6


class Leaf:
    def __init__(self, path, typ, value, value_off, value_size):
        self.path = path
        self.type = typ
        self.value = value
        self.value_off = value_off
        self.value_size = value_size


def _u32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def parse_node(buf, off, path, out):
    """Parse one node at `off`; return the offset just past it."""
    typ = _u32(buf, off)
    off += 4
    if typ == T_NONE:
        # Zero-length value. This is how an *uncollected* HiddenPackage and
        # other "not set" entries are stored - the key exists, the value does
        # not. Setting one therefore needs a rewrite, not an in-place poke.
        out.append(Leaf(path, typ, None, off, 0))
        return off
    if typ == T_U32:
        out.append(Leaf(path, typ, _u32(buf, off), off, 4))
        return off + 4
    if typ == T_FLOAT:
        out.append(Leaf(path, typ, struct.unpack_from(">f", buf, off)[0], off, 4))
        return off + 4
    if typ == T_BOOL:
        out.append(Leaf(path, typ, buf[off], off, 1))
        return off + 1
    if typ == T_STR:
        n = _u32(buf, off)
        out.append(Leaf(path, typ, buf[off + 4:off + 4 + n].decode("ascii", "replace"),
                        off + 4, n))
        return off + 4 + n
    if typ == T_CONTAINER:
        count = _u32(buf, off)
        off += 4
        for _ in range(count):
            klen = _u32(buf, off)
            key = buf[off + 4:off + 4 + klen].decode("ascii", "replace")
            off += 4 + klen
            off = parse_node(buf, off, path + "/" + key if path else key, out)
        return off
    raise ValueError("unknown node type %d at 0x%X" % (typ, off - 4))


def load(path):
    buf = bytearray(open(path, "rb").read())
    if bytes(buf[:4]) != MAGIC:
        sys.exit("not an AKJB save: %s" % path)
    leaves = []
    parse_node(buf, 8, "", leaves)   # skip magic + version
    return buf, leaves


def find(leaves, key):
    exact = [l for l in leaves if l.path == key or l.path.endswith("/" + key)]
    if not exact:
        sys.exit("key not found: %s" % key)
    if len(exact) > 1:
        sys.exit("ambiguous key %r, matches:\n  %s"
                 % (key, "\n  ".join(l.path for l in exact)))
    return exact[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("save")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE")
    ap.add_argument("--no-backup", action="store_true")
    args = ap.parse_args()

    buf, leaves = load(args.save)

    if not args.set:
        width = max(len(l.path) for l in leaves)
        for l in leaves:
            kind = {T_NONE: "none", T_U32: "u32", T_FLOAT: "float",
                    T_BOOL: "bool", T_STR: "str"}.get(l.type, "?")
            print("  %-*s  %-5s %s" % (width, l.path, kind, l.value))
        print("\n%d leaves" % len(leaves))
        return

    if not args.no_backup:
        # Back up OUTSIDE the save directory. The game recreates the whole
        # ht2save.dat/ folder when it rejects a save, which deletes anything
        # kept alongside it - that is how a previous backup was lost.
        stamp = time.strftime("%Y%m%d-%H%M%S")
        dest_dir = os.path.join(os.path.expanduser("~"), ".ht_save_backups")
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, "ht2save-%s.dat" % stamp)
        shutil.copy2(args.save, dest)
        print("backup -> %s" % dest)

    for assignment in args.set:
        key, _, raw = assignment.partition("=")
        leaf = find(leaves, key)
        if leaf.type == T_U32:
            new = int(raw, 0)
            struct.pack_into(">I", buf, leaf.value_off, new)
        elif leaf.type == T_FLOAT:
            new = float(raw)
            struct.pack_into(">f", buf, leaf.value_off, new)
        elif leaf.type == T_BOOL:
            new = 1 if raw.lower() in ("1", "true", "yes", "on") else 0
            buf[leaf.value_off] = new
        elif leaf.type == T_STR:
            if len(raw) != leaf.value_size:
                sys.exit("string %r must stay %d chars (in-place edit only)"
                         % (leaf.path, leaf.value_size))
            buf[leaf.value_off:leaf.value_off + leaf.value_size] = raw.encode("ascii")
            new = raw
        elif leaf.type == T_NONE:
            sys.exit("%r has no stored value (type 0). Setting it needs a tree "
                     "rewrite, which this tool does not do yet." % leaf.path)
        else:
            sys.exit("cannot set type %d" % leaf.type)
        print("  %s: %s -> %s" % (leaf.path, leaf.value, new))

    open(args.save, "wb").write(buf)
    print("written")


if __name__ == "__main__":
    main()
