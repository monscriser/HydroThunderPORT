"""Extracts <data name><value> pairs from a .resx, keeping the raw inner XML of
each value (entities and line breaks as in the file), one JSON object per line.

usage: extract_resx.py <in.resx> <out.jsonl>
"""
import json
import re
import sys

text = open(sys.argv[1], encoding="utf-8-sig").read()
pattern = re.compile(r'<data name="([^"]+)"[^>]*>\s*<value>(.*?)</value>', re.S)
rows = [{"k": m.group(1), "v": m.group(2)} for m in pattern.finditer(text)]
with open(sys.argv[2], "w", encoding="utf-8") as out:
    for r in rows:
        out.write(json.dumps(r, ensure_ascii=False) + "\n")
chars = sum(len(r["v"]) for r in rows)
print(f"{sys.argv[1]}: {len(rows)} entradas, {chars} caracteres")
