"""Fails if the sources zip carries extracted original game text."""
import sys
import zipfile

names = zipfile.ZipFile(sys.argv[1]).namelist()
bad = [n for n in names if n.endswith(("base_ptpt.jsonl", "dlc_ptpt.jsonl", "base_en.jsonl",
                                       "ptbr_base.jsonl", "ptbr_dlc_final.jsonl"))]
print("entradas:", len(names), "| texto original do jogo no zip:", bad or "nenhum")
sys.exit(1 if bad else 0)
