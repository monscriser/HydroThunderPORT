#!/bin/bash
# Hydro Thunder 9.0.0 - rodadas com opcoes do driver RADV (AMD) no ROG Ally / Steam Deck.
# O script abre o jogo sozinho em cada rodada, com uma opcao diferente do driver (sem limite de FPS),
# o jogador corre ~1 minuto em Asgard e fecha o jogo. No fim a configuracao volta a ser
# exatamente a de antes e sai um resumo em ~/hydro-medicao-rodadas.
set -u
ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/HydroThunderRecompiled"
TOML="$ROOT/hydrothunder.toml"
LOGS="$ROOT/logs"
OUT="$HOME/hydro-medicao-radv"
HERE="$(cd "$(dirname "$0")" && pwd)"
APP=$(ls "$HERE"/HydroThunder-revisao-*.AppImage 2>/dev/null | head -1)
if [ -z "$APP" ]; then echo "Nao achei o AppImage ao lado deste script."; read -r -p "Enter para sair"; exit 1; fi
chmod +x "$APP" 2>/dev/null
if [ ! -f "$TOML" ]; then
  echo "Nao achei $TOML. Abra o jogo uma vez, feche, e rode este arquivo de novo."; read -r -p "Enter para sair"; exit 1
fi
mkdir -p "$OUT"
BACKUP="$OUT/hydrothunder.toml.original"
MARK='# hydro-medicao-em-andamento'
# Medicao anterior interrompida (ex.: bateria acabou): volta a configuracao original primeiro.
if grep -q "^$MARK" "$TOML" && [ -f "$BACKUP" ]; then cp "$BACKUP" "$TOML"; echo "Configuracao original recuperada de uma medicao interrompida."; fi
cp "$TOML" "$BACKUP"
restore() { cp "$BACKUP" "$TOML"; }
trap 'restore; echo; echo "Configuracao restaurada."; exit 1' INT TERM

# nome|ajustes (chave = valor; separados por ;)
# nome|variaveis do driver (so nesta rodada)
ROUNDS=(
  "1-padrao|"
  "2-radv-nodcc|RADV_DEBUG=nodcc"
  "3-radv-nohiz|RADV_DEBUG=nohiz"
  "4-radv-nodcc-nohiz|RADV_DEBUG=nodcc,nohiz"
)
setkey() { # setkey "chave = valor"
  local k="${1%% = *}"
  if grep -q "^$k *=" "$TOML"; then sed -i "s|^$k *=.*|$1|" "$TOML"; else sed -i "1i $1" "$TOML"; fi
}
summary() { # summary <log> <nome>
  awk -v name="$2" '
    /\[ht-perf\] t=/ {
      if (match($0, /t=[0-9]+s/)) t = substr($0, RSTART, RLENGTH); else next
      d = 0; if (match($0, /draws=[0-9.]+/)) d = substr($0, RSTART + 6, RLENGTH - 6) + 0
      if (d < 1000) next
      if (!seen++) next   # 1a amostra da corrida inclui o carregamento da pista
      race[t] = 1; n++
      if (match($0, /fps=[0-9.]+/)) fps += substr($0, RSTART + 4, RLENGTH - 4)
      seg = $0; sub(/.*gpu ms\/frame:/, "", seg); sub(/\|.*/, "", seg)
      if (match(seg, /total=[0-9.]+/)) gpu += substr(seg, RSTART + 6, RLENGTH - 6)
      if (match(seg, /resolve=[0-9.]+/)) res += substr(seg, RSTART + 8, RLENGTH - 8)
      if (match(seg, /transfers=[0-9.]+/)) tr += substr(seg, RSTART + 10, RLENGTH - 10)
      if (match($0, /max_frame=[0-9.]+/)) { m = substr($0, RSTART + 10, RLENGTH - 10) + 0; if (m > worst) worst = m }
      next
    }
    /\[ht-cpu\] t=/ {
      if (match($0, /t=[0-9]+s/)) t = substr($0, RSTART, RLENGTH); else next
      if (match($0, /busy=[0-9.]+/)) cpu[t] = substr($0, RSTART + 5, RLENGTH - 5)
      if (match($0, /gpu_fence=[0-9.]+/)) fen[t] = substr($0, RSTART + 10, RLENGTH - 10)
    }
    END {
      for (t in race) if (t in cpu) { c += cpu[t]; f += fen[t]; k++ }
      if (!n) { printf "%-28s sem dados de corrida (jogou pelo menos 1 minuto?)\n", name; exit }
      printf "%-28s FPS %6.1f | GPU %5.2f ms (resolve %4.2f, transfers %4.2f) | CPU %5.2f ms | espera GPU %4.2f ms | pior %6.1f ms | %d amostras\n",
        name, fps/n, gpu/n, res/n, tr/n, (k ? c/k : 0), (k ? f/k : 0), worst, n
    }' "$1"
}

echo
echo "=== Hydro Thunder 9.0.0 - medicao em ${#ROUNDS[@]} rodadas ==="
echo "Em cada rodada o jogo abre sozinho: jogue Asgard por ~1 minuto acelerando e"
echo "virando e feche o jogo (Alt+F4 serve). A rodada seguinte comeca quando voce apertar Enter."
echo "O FPS fica sem limite durante a medicao; a imagem pode tremer no Modo Desktop, e normal."
for i in "${!ROUNDS[@]}"; do
  name="${ROUNDS[$i]%%|*}"; adj="${ROUNDS[$i]#*|}"
  restore
  sed -i "1i $MARK" "$TOML"
  for pair in 'ht_perf_log = true' 'log_level = "warn"' 'fps_limit = 0'; do setkey "$pair"; done
  before=$(ls -1 "$LOGS"/*.log 2>/dev/null | sort)
  echo
  echo "--- Rodada $((i+1)) de ${#ROUNDS[@]}: $name ---"
  read -r -p "Aperte Enter para o jogo abrir sozinho; jogue ~1 minuto e feche o jogo (Alt+F4)"
  # --ht_direct_launch=true: abre direto no jogo (sem o lancador).
  if [ -n "$adj" ]; then env "$adj" "$APP" --ht_direct_launch=true > "$OUT/$name.stdout.txt" 2>&1
  else "$APP" --ht_direct_launch=true > "$OUT/$name.stdout.txt" 2>&1; fi
  NEW=$(comm -13 <(echo "$before") <(ls -1 "$LOGS"/*.log 2>/dev/null | sort) | xargs -r ls -t 2>/dev/null)
  LOG=$(for f in $NEW; do grep -q '\[ht-perf\] t=' "$f" && { echo "$f"; break; }; done)
  [ -z "$LOG" ] && LOG=$(echo "$NEW" | head -1)
  if [ -z "$LOG" ]; then echo "Nao achei um log novo nesta rodada (o jogo foi aberto?)."; continue; fi
  cp "$LOG" "$OUT/$name.log"
  summary "$LOG" "$name" | tee -a "$OUT/resumo.txt"
done
restore
trap - INT TERM
{ echo; echo "Driver: $(vulkaninfo --summary 2>/dev/null | grep -m1 -i driverInfo | sed 's/^ *//')"; echo "Disco do jogo: $(df -h "$ROOT" 2>/dev/null | tail -1)"; echo "Placa: $(grep -m1 '\* 0: ' "$OUT"/*.log 2>/dev/null | head -1 | sed 's/.*\* 0: //')"; } >> "$OUT/resumo.txt"
echo
echo "Pronto. Configuracao restaurada. Mande a pasta $OUT para o Claude."
cat "$OUT/resumo.txt"
read -r -p "Enter para sair"
