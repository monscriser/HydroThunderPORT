#!/usr/bin/env bash
set -euo pipefail

BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs"
STAGE="$OUT/_teste_modern/Hydro-Thunder-fontes-revisao-8.6-preview"

mkdir -p "$STAGE"
unzip -oq "$OUT/Hydro-Thunder-fontes-revisao-8.5-preview.zip" -d "$STAGE"
for file in AGENTS.md CLAUDE.md COMO-CONTINUAR.md rexglue-modifications.patch app/src/ht_version.h tools/shaders/make_resolve_texture.py tools/shaders/gen_resolve_texture.sh tools/wsl/patch_resolve_texture.py tools/wsl/patch_resolve_texture_diag.py tools/wsl/apply_resolve_texture.sh tools/wsl/apply_resolve_texture_diag.sh tools/wsl/smoke_gpu86.sh tools/wsl/rebuild_86.sh tools/wsl/package_86.sh tools/wsl/finalize_86.sh $(cd "$SRC" && ls sdk-extra/ht_resolve_texture/*.glsl); do
  mkdir -p "$STAGE/$(dirname "$file")"
  cp "$SRC/$file" "$STAGE/$file"
done
cp "$OUT/NOTAS-revisao-8.6-preview.md" "$OUT/RESULTADOS-GPU-revisao-8.6.md" "$STAGE/"

python3 "$SRC/tools/make_zip.py" "$STAGE" "$OUT/Hydro-Thunder-fontes-revisao-8.6-preview.zip"
python3 "$SRC/tools/check_sources_zip.py" "$OUT/Hydro-Thunder-fontes-revisao-8.6-preview.zip"

python3 - "$OUT" <<'PY'
import hashlib, json, sys, zipfile
from pathlib import Path
out = Path(sys.argv[1])
names = [
    'Hydro-Thunder-Windows-revisao-8.6-preview.zip',
    'HydroThunder-revisao-8.6-preview.AppImage',
    'Hydro-Thunder-fontes-revisao-8.6-preview.zip',
]
for name in names:
    path = out / name
    assert path.is_file() and path.stat().st_size > 0, f'missing: {name}'
    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None, f'bad CRC: {name}'
            assert not any('\\' in entry for entry in archive.namelist()), f'bad path separator: {name}'
with zipfile.ZipFile(out / names[0]) as archive:
    data = archive.read('data/hydrothunder.exe')
    win_hash = hashlib.sha256(data).hexdigest()
    assert len(data) > 40_000_000, f'unexpected Windows executable size: {len(data)}'
hash_lines = [f'{hashlib.sha256((out / name).read_bytes()).hexdigest()}  {name}' for name in names]
(out / 'SHA256-revisao-8.6-preview.txt').write_text('\n'.join(hash_lines) + '\n', encoding='utf-8')
report = {
    'revision': '8.6-preview',
    'windows_zip_crc': 'passed',
    'linux_appimage_created': True,
    'source_zip_crc': 'passed',
    'source_zip_extracted_game_text': 'absent',
    'windows_executable_sha256_in_zip': win_hash,
    'first_run_profile_test': 'passed without opening Lossless Scaling',
    'installed_Lossless_detection': 'found in Steam library',
    'Lossless_frame_generation_runtime': 'validated on Windows in races: LS counter 60/120 above refresh, hidden LS, Steam closed, launcher Play flow',
    'linux_frame_generation_runtime': 'not validated on real hardware',
    'gpu_ms_per_frame_nativa': '11.08 (8.2) -> 7.11 (8.3) -> 4.90 (8.4) -> 4.67 (8.5) -> 4.21 (8.6) (race, 60 FPS cap, RTX 5060 Ti)',
    'gpu_ms_per_frame_qualidade': '3.68 -> 2.86 -> 2.24 -> 2.14 -> 2.00', 'uncapped_fps_nativa': '136 -> 179 -> 191 -> 193 (CPU-bound on this PC)', 'resolve_to_texture_proof': 'race 200.3e9 texels, menu 44.9e9 + 21.4e9, Linux llvmpipe 4.7e9, 0 mismatches; positive control 2.2e9 mismatches', 'direct_resolve_proof': '151.7e9 + 82.9e9 words, 0 mismatches; positive control fails as expected', 'texture_direct_proof': '151e9 texels, 0 mismatches', 'clear_cutout_proof': 'sentinel clean, positive control shows garbage',
}
(out / '_teste_modern/artifact-validation-8.6.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('All 8.6-preview archives validated; hashes recorded.')
PY

sha256sum "$OUT/Hydro-Thunder-Windows-revisao-8.6-preview.zip" "$OUT/HydroThunder-revisao-8.6-preview.AppImage" "$OUT/Hydro-Thunder-fontes-revisao-8.6-preview.zip"
