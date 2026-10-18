#!/bin/bash
# Checks that every rev7 package ships the FidelityFX runtime and its MIT notice,
# and lists what the FidelityFX binaries link against.
set -uo pipefail
O="$1"   # outputs dir (WSL path)
REF=/root/ht/build-win/_deps/fidelityfx-src/LICENSE.txt
T=$(mktemp -d)
echo "== Windows zip"
python3 - "$O/Hydro-Thunder-Windows-revisao-7.zip" "$REF" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1]); names = z.namelist()
lic = [n for n in names if 'FidelityFX' in n]; dll = [n for n in names if 'fidelityfx' in n.lower() and n.endswith('.dll')]
print('licenca:', lic, '| identica:', bool(lic) and z.read(lic[0]) == open(sys.argv[2], 'rb').read())
print('runtime:', dll)
PY
echo "== AppImage"
cp "$O/HydroThunder-revisao-7.AppImage" "$T/a.AppImage"; chmod +x "$T/a.AppImage"
(cd "$T" && ./a.AppImage --appimage-extract >/dev/null 2>&1)
B="$T/squashfs-root/usr/bin"
ls "$B/licenses" | grep -i fidelity; cmp -s "$B/licenses/AMD-FidelityFX-SDK-LICENSE.txt" "$REF" && echo "identica: True"
ls -la "$B/libamd_fidelityfx_vk.so" | awk '{print "runtime:", $5, $9}'
echo "== tarball"
tar xzf "$O/Hydro-Thunder-Linux-revisao-7-portatil.tar.gz" -C "$T"
P="$T/Hydro-Thunder-Linux-revisao-7-portatil"
cmp -s "$P/licenses/AMD-FidelityFX-SDK-LICENSE.txt" "$REF" && echo "licenca identica: True"
cmp -s "$P/libamd_fidelityfx_vk.so" "$B/libamd_fidelityfx_vk.so" && echo "runtime igual ao do AppImage: True"
echo "== dependencias do runtime Linux (NEEDED)"
readelf -d "$B/libamd_fidelityfx_vk.so" | grep NEEDED | sed 's/.*\[\(.*\)\]/  \1/'
echo "== quem carrega o runtime"
for f in hydrothunder librexruntime.so librexgpu-xenos.so; do readelf -d "$B/$f" | grep -q 'libamd_fidelityfx_vk' && echo "  $f"; done
echo "== importacoes do runtime Windows"
W="$T/w"; mkdir -p "$W"; python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$O/Hydro-Thunder-Windows-revisao-7.zip" "$W"
/root/ht/llvm-mingw/bin/llvm-objdump -p "$W/data/libamd_fidelityfx_vk.dll" | grep 'DLL Name' | sed 's/^/ /'
for f in hydrothunder.exe librexruntime.dll rexgpu-xenos.dll; do
  /root/ht/llvm-mingw/bin/llvm-objdump -p "$W/data/$f" | grep -q 'DLL Name: libamd_fidelityfx_vk.dll' && echo "  carregado por: $f"
done
echo "== exportacoes ffx da DLL"
/root/ht/llvm-mingw/bin/llvm-objdump -p "$W/data/libamd_fidelityfx_vk.dll" | grep -oE 'ffx[A-Za-z]+' | sort -u | tr '\n' ' '; echo
rm -rf "$T"
