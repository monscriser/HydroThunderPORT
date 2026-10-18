#!/bin/bash
# How the FidelityFX runtime gets loaded, and the newest glibc/libstdc++ symbol
# versions each Linux binary needs (rev7 vs the rev6 AppImage).
set -uo pipefail
O="$1"
OBJ=/root/ht/llvm-mingw/bin/llvm-objdump
T=$(mktemp -d)
python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$O/Hydro-Thunder-Windows-revisao-7.zip" "$T/w"
echo "== Windows: imports de libamd_fidelityfx_vk.dll"
for f in hydrothunder.exe librexruntime.dll rexgpu-xenos.dll; do
  $OBJ -p "$T/w/data/$f" | grep -iE 'DLL Name: .*fidelity' | sed "s/^/  $f: /"
  grep -c -a 'libamd_fidelityfx_vk' "$T/w/data/$f" | sed "s/^/  $f menciona o nome (vezes): /"
done
echo "== Linux: maior versao exigida (GLIBC / GLIBCXX)"
for tag in 7 6; do
  F=$([ $tag = 7 ] && echo "$O/HydroThunder-revisao-7.AppImage" || echo "$O/HydroThunder-revisao-6.AppImage")
  mkdir -p "$T/a$tag"; cp "$F" "$T/a$tag/x.AppImage"; chmod +x "$T/a$tag/x.AppImage"
  (cd "$T/a$tag" && ./x.AppImage --appimage-extract >/dev/null 2>&1)
  for f in "$T/a$tag"/squashfs-root/usr/bin/*; do
    file -b "$f" | grep -q ELF || continue
    g=$(objdump -T "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)
    c=$(objdump -T "$f" 2>/dev/null | grep -oE 'GLIBCXX_[0-9.]+' | sort -Vu | tail -1)
    echo "  rev$tag $(basename "$f"): $g $c"
  done
done
rm -rf "$T"
