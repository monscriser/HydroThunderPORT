"""pt-PT -> pt-BR for the Tempest Pack DLC strings: template rules first, then the
hand translations in ptbr_dlc_manual.jsonl win. Writes ptbr_dlc.jsonl and reports
any pt-PT marker left.

usage: rules_dlc.py <dlc_ptpt.jsonl> <ptbr_dlc_manual.jsonl> <out.jsonl>
"""
import json
import re
import sys

RULES = [
    (r" - em exclusivo para quem (?:tem|possuir) a versão completa do Pacote (?:Tempestade|Tempest)!",
     " - exclusivo da versão completa do Pacote Tempestade!"),
    (r"^Ganha (\S+) créditos para desbloqueares", r"Ganhe \1 créditos para desbloquear"),
    (r"Para desbloqueares esta skin, ganha", "Para desbloquear esta skin, ganhe"),
    (r"Para desbloqueares esta skin, encontra", "Para desbloquear esta skin, encontre"),
    (r"^Desbloqueaste", "Você desbloqueou"),
    (r"^Ronda ", "Rodada "),
    (r"está agora disponível", "agora está disponível"),
    (r"Aberto apenas a barcos de Especialista\.\s+", "Aberto apenas para barcos Especialista. "),
    (r"\bnuma\b", "em uma"),
    (r"Babilónia", "Babilônia"),
    (r"Seoul", "Seul"),
    (r" \.$", "."),
    (r"^NÃO DETESTES$", "NÃO ODEIE"),
    (r"^PENSA NISSO$", "PENSE NISSO"),
    (r"^BATATAS QUENTES$", "BATATA QUENTE"),
    (r"^Velocidade$", "VELOCIDADE"),
]
LEFTOVER = re.compile(r"\b(tu|te|teu|tua|teus|tuas|podes|poderás|conseguires|desbloqueares|acabaste|"
                      r"obténs|misturas|encontra|ganha|ecrã|consola|ficheiro|equipa|ventoinha|"
                      r"terramotos|pequeno-almoço|tem cuidado|activ|selecç)\b", re.I)

manual = {json.loads(l)["k"]: json.loads(l)["v"] for l in open(sys.argv[2], encoding="utf-8")}
out = []
for line in open(sys.argv[1], encoding="utf-8"):
    row = json.loads(line)
    v = row["v"]
    for pattern, repl in RULES:
        v = re.sub(pattern, repl, v, flags=re.M)
    v = manual.get(row["k"], v)
    if v != row["v"]:
        out.append({"k": row["k"], "v": v})
    if LEFTOVER.search(v):
        print("REVISAR:", row["k"], "|", v.replace("\n", "\\n"))
with open(sys.argv[3], "w", encoding="utf-8") as f:
    for row in out:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")
print(len(out), "frases da DLC alteradas")
