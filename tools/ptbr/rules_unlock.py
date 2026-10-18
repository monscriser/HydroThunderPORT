"""pt-PT -> pt-BR for the repetitive Unlock_* / Award_* templates (keys 'Unlock_' and
'Award_'), written to ptbr_04.jsonl. Every output is printed for review, and any
pt-PT marker left over is reported.

usage: rules_unlock.py <base_ptpt.jsonl> <out.jsonl>
"""
import json
import re
import sys

RULES = [
    (r"Desbloqueia-o agora e poderás ficar com ele!", "Desbloqueie agora e fique com ele!"),
    (r"Desbloqueia-a agora e poderás ficar com ela!", "Desbloqueie agora e fique com ela!"),
    (r"Desbloqueia-os agora e poderás ficar com eles!", "Desbloqueie agora e fique com eles!"),
    (r"Desbloqueia o jogo completo agora e poderás ficar com ele!", "Desbloqueie o jogo completo agora e fique com ele!"),
    (r"Desbloqueia o jogo completo agora e podes ficar com ela!", "Desbloqueie o jogo completo agora e fique com ela!"),
    (r"^Ganha (\S+) créditos para desbloqueares", r"Ganhe \1 créditos para desbloquear"),
    (r"Para desbloqueares esta skin,? ganha", "Para desbloquear esta skin, ganhe"),
    (r"Para desbloqueares esta skin,? descobre", "Para desbloquear esta skin, encontre"),
    (r"Para desbloqueares deves", "Para desbloquear, você deve"),
    (r"^Desbloqueaste", "Você desbloqueou"),
    (r"^Ganhaste", "Você ganhou"),
    (r"^Perdeste!", "Você perdeu!"),
    (r"estão agora disponíveis", "agora estão disponíveis"),
    (r"está agora disponível para jogares", "agora está disponível"),
    (r"está agora disponível", "agora está disponível"),
    (r"Os eventos Profissionais são mais difíceis, mas ganhas mais créditos\.",
     "Os eventos Profissionais são mais difíceis, mas você ganha mais créditos."),
    (r"\bnuma\b", "em uma"),
    (r"Babilónia", "Babilônia"),
    (r"Seoul", "Seul"),
    (r"Supatómico", "Supatômico"),
    (r"Unidade Vector", "Vector Unit"),
    (r"Apenas Especialistas", "Só Especialistas"),
    (r"Prémio", "Prêmio"),
    (r"Asgard \.", "Asgard."),
]
LEFTOVER = re.compile(r"\b(tu|teu|tua|teus|tuas|ganhaste|podes|poderás|desbloqueares|desbloqueia-[oa]s?|"
                      r"ecrã|consola|guardar|ficheiro|equipa|activ|selecç|accion|objectiv)\b", re.I)

out = []
for line in open(sys.argv[1], encoding="utf-8"):
    row = json.loads(line)
    if not (row["k"].startswith("Unlock_") or row["k"].startswith("Award_")):
        continue
    v = row["v"]
    for pattern, repl in RULES:
        v = re.sub(pattern, repl, v, flags=re.M)
    if v != row["v"]:
        out.append({"k": row["k"], "v": v})
    if LEFTOVER.search(v):
        print("REVISAR:", row["k"], "|", v.replace("\n", "\\n"))
with open(sys.argv[2], "w", encoding="utf-8") as f:
    for row in out:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")
print(len(out), "frases alteradas")
for row in out[:6] + out[-4:]:
    print(" ", row["k"], "->", row["v"].replace("\n", "\\n"))
