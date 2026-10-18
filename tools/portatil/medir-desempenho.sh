#!/bin/bash
# Medicao de desempenho para ROG Ally / Steam Deck / CachyOS (Hydro Thunder 8.6+, AppImage).
# Liga o registro de desempenho so durante a medicao, espera uma corrida, restaura a
# configuracao e gera um resumo + copia do log em ~/hydro-medicao.
set -u
ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/HydroThunderRecompiled"
TOML="$ROOT/hydrothunder.toml"
LOGS="$ROOT/logs"
if [ ! -f "$TOML" ]; then
  echo "Nao achei $TOML. Abra o jogo uma vez, feche, e rode este arquivo de novo."; read -r -p "Enter para sair"; exit 1
fi
cp "$TOML" "$TOML.antes-da-medicao"
# Sem limite de FPS so durante a medicao: mostra se o limite e a GPU ou a CPU.
for pair in 'ht_perf_log = true' 'log_level = "warn"' 'fps_limit = 0'; do
  k="${pair%% = *}"
  if grep -q "^$k *=" "$TOML"; then sed -i "s|^$k *=.*|$pair|" "$TOML"; else sed -i "1i $pair" "$TOML"; fi
done
before=$(ls -1 "$LOGS"/*.log 2>/dev/null | sort)
echo
echo "=== Medicao de desempenho do Hydro Thunder ==="
echo "1. Abra o jogo do jeito de sempre (no Modo Jogo da Steam ou pela area de trabalho)."
echo "2. Jogue UMA corrida em Asgard por pelo menos 1 minuto, acelerando e virando."
echo "3. Feche o jogo."
echo "4. Volte para esta janela e aperte Enter."
read -r -p "Aperte Enter DEPOIS de fechar o jogo"
mv "$TOML.antes-da-medicao" "$TOML"
LOG=$(comm -13 <(echo "$before") <(ls -1 "$LOGS"/*.log 2>/dev/null | sort) | xargs -r ls -t 2>/dev/null | head -1)
[ -z "$LOG" ] && { echo "Nao achei um log novo. O jogo foi aberto?"; read -r -p "Enter para sair"; exit 1; }
avg() { grep -E "$1" "$LOG" | tail -15 | grep -oE "[ (]$2=[0-9.]+" | cut -d= -f2 | awk '{s+=$1;n++} END{if(n) printf "%.2f", s/n; else print "n/a"}'; }
OUT="$HOME/hydro-medicao"; mkdir -p "$OUT"; cp "$LOG" "$OUT/"; cp "$TOML" "$OUT/hydrothunder.toml"
{
  echo "Placa: $(grep -m1 '\* 0: ' "$LOG" | sed 's/.*\* 0: //')"
  echo "Log: $(basename "$LOG")"
  echo "FPS medio (ultimos ~30 s): $(avg '\[ht-perf\] t=' fps)"
  echo "GPU ms por quadro: $(avg '\[ht-perf\] t=' total)  (resolve $(avg '\[ht-perf\] t=' resolve), transfers $(avg '\[ht-perf\] t=' transfers))"
  echo "CPU (thread de GPU) ms por quadro: $(avg '\[ht-cpu\]' busy)"
  echo "Pior quadro ms: $(avg '\[ht-perf\] t=' max_frame)"
} | tee "$OUT/resumo.txt"
echo; echo "Pronto. Configuracao restaurada. Mande a pasta $OUT para o Claude."
read -r -p "Enter para sair"
