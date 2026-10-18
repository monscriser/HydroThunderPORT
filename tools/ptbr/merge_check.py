"""Merges the pt-BR override files over the pt-PT strings, then scans the final
text for pt-PT markers and broken tokens.

usage: merge_check.py <ptpt.jsonl> <out.jsonl> <override.jsonl>...
Writes only changed entries to <out.jsonl>.
"""
import json
import re
import sys

src = [json.loads(l) for l in open(sys.argv[1], encoding="utf-8")]
keys = {r["k"] for r in src}
over = {}
for path in sys.argv[3:]:
    for line in open(path, encoding="utf-8"):
        row = json.loads(line)
        if row["k"] not in keys:
            print("CHAVE INEXISTENTE:", path, row["k"])
        over[row["k"]] = row["v"]

PTPT = re.compile(r"\b(tu|te|teu|tua|teus|tuas|ti|contigo|estás|tens|queres|podes|poderás|sabes|fazes|"
                  r"ganhaste|perdeste|acabaste|desbloqueaste|desbloqueares|premir|prime|premido|"
                  r"ecrã|consola|ficheiro|ficheiros|equipa|equipas|guardar|guardado|"
                  r"a carregar|a guardar|a jogar|à espera|activ\w*|actual\w*|selecc\w*|direcç\w*|"
                  r"controlo|controlos|travão|ganhámos|acabámos|apanh\w+|ena pá|câmara|"
                  r"Babilónia|Seoul|Prémio|Feitos?|ronda|definições|utiliz\w+|transfer\w+)\b", re.I)
TOKEN = re.compile(r"\{\([^)]*\)\}|&#x0A;|&amp;|\\n|\n|\t")

out, issues = [], 0
for r in src:
    v = over.get(r["k"], r["v"])
    if sorted(TOKEN.findall(v)) != sorted(TOKEN.findall(r["v"])) and r["k"] in over:
        # Line breaks may move; button glyphs and entities must not be lost.
        a = [t for t in TOKEN.findall(r["v"]) if t.startswith("{") or t.startswith("&")]
        b = [t for t in TOKEN.findall(v) if t.startswith("{") or t.startswith("&")]
        if sorted(a) != sorted(b):
            print("TOKEN PERDIDO:", r["k"], a, b); issues += 1
    m = PTPT.search(v)
    if m and not r["k"].startswith(("Credits_", "CheatOptions_", "TestString", "Start_0")):
        print(f"PT-PT? [{m.group(0)}] {r['k']}: {v[:110]!r}"); issues += 1
    if v != r["v"]:
        out.append({"k": r["k"], "v": v})
with open(sys.argv[2], "w", encoding="utf-8") as f:
    for row in out:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")
print(f"{len(out)} de {len(src)} frases traduzidas; {issues} pontos para revisar")
