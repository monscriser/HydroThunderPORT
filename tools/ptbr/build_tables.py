"""Writes the shipped pt-BR override tables: one 'key<TAB>value' per line, value
escaped as \\\\ \\n \\t (raw inner XML of the .resx <value>).

usage: build_tables.py <ptbr_base.jsonl> <ptbr_dlc_final.jsonl> <out dir>
"""
import json
import re
import sys
from pathlib import Path

BAD_XML = re.compile(r"&(?!amp;|lt;|gt;|quot;|apos;|#x[0-9A-Fa-f]+;|#\d+;)|<|>")


def write(src, dst, header):
    rows = [json.loads(l) for l in open(src, encoding="utf-8")]
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        for r in rows:
            assert "\t" not in r["k"] and BAD_XML.search(r["v"]) is None, r
            v = r["v"].replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")
            f.write(f"{r['k']}\t{v}\n")
    print(dst, len(rows))


out = Path(sys.argv[3])
out.mkdir(parents=True, exist_ok=True)
head = "# Hydro Thunder Hurricane: pt-BR overrides for {} (applied over pt-pt.resx at launch)\n"
write(sys.argv[1], out / "pt-br-base.txt", head.format("the base game"))
write(sys.argv[2], out / "pt-br-dlc.txt", head.format("the Tempest Pack DLC"))
