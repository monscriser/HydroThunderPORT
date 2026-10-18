#!/bin/sh
# usage: portable_files.sh <AppDir> <outdir>
set -e
A="$1"; O="$2"
rm -rf "$O"; mkdir -p "$O"
cp -a "$A/usr/bin/." "$O/"
cp "$A/hydrothunder.svg" "$O/ui/hydrothunder.svg"
cat > "$O/Jogar.sh" <<'EOS'
#!/bin/sh
# Versao portatil (sem AppImage/FUSE). Configuracao, logs e cache ficam em
# ${XDG_DATA_HOME:-~/.local/share}/HydroThunderRecompiled.
HERE="$(dirname -- "$(readlink -f -- "$0")")"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/HydroThunderRecompiled"
mkdir -p "$DATA/logs" 2>/dev/null
LOG="$DATA/logs/portatil-launch.log"
if [ -z "${SDL_VIDEODRIVER:-}" ] && [ -z "${SDL_VIDEO_DRIVER:-}" ]; then
  export SDL_VIDEO_DRIVER="x11,wayland"
fi
cd -- "$HERE" || exit 1
chmod +x ./hydrothunder ./ht-import ./xextool-re 2>/dev/null
start=$(date +%s)
./hydrothunder "$@" >"$LOG" 2>&1
rc=$?
if [ "$rc" -ne 0 ] && [ $(( $(date +%s) - start )) -lt 15 ]; then
  msg="Hydro Thunder fechou ao iniciar (codigo $rc).
Detalhes em: $LOG"
  if command -v kdialog >/dev/null 2>&1; then kdialog --title "Hydro Thunder" --error "$msg"
  elif command -v zenity >/dev/null 2>&1; then zenity --error --title="Hydro Thunder" --text="$msg"
  elif command -v notify-send >/dev/null 2>&1; then notify-send "Hydro Thunder" "$msg"; fi
fi
exit "$rc"
EOS
cat > "$O/Criar-atalho-no-menu.sh" <<'EOS'
#!/bin/sh
# Cria "Hydro Thunder Hurricane" no menu de aplicativos (KDE/GNOME/etc.).
HERE="$(dirname -- "$(readlink -f -- "$0")")"
D="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$D"
chmod +x "$HERE/Jogar.sh" "$HERE/hydrothunder" "$HERE/ht-import" "$HERE/xextool-re"
cat > "$D/hydrothunder-recompiled.desktop" <<EOD
[Desktop Entry]
Type=Application
Name=Hydro Thunder Hurricane
Comment=Configurar e jogar
Exec="$HERE/Jogar.sh"
Path=$HERE
Icon=$HERE/ui/hydrothunder.svg
Categories=Game;
Terminal=false
EOD
chmod +x "$D/hydrothunder-recompiled.desktop"
echo "Atalho criado em $D/hydrothunder-recompiled.desktop"
EOS
chmod 755 "$O/Jogar.sh" "$O/Criar-atalho-no-menu.sh" "$O/hydrothunder" "$O/ht-import" "$O/xextool-re"
