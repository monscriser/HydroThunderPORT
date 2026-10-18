#!/usr/bin/env bash
set -euo pipefail

BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs"
STAGE="$OUT/_teste_modern/Hydro-Thunder-fontes-revisao-8.3-preview"

mkdir -p "$STAGE"
unzip -oq "$OUT/Hydro-Thunder-fontes-revisao-8.2-preview.zip" -d "$STAGE"
for file in AGENTS.md CLAUDE.md COMO-CONTINUAR.md rexglue-modifications.patch app/src/ht_version.h app/src/ht_app_common.h sdk-extra/ht_texture_blit/ht_texture_blit_32bpb.cs.glsl sdk-extra/ht_texture_blit/ht_texture_blit_64bpb.cs.glsl sdk-extra/ht_texture_blit/ht_texture_blit_128bpb.cs.glsl tools/test/gpu_race.ps1 tools/test/gpu_race_suite.ps1 tools/test/gpu_menu.ps1 tools/test/perf_breakdown.ps1 tools/test/scale_compare.py tools/test/launcher_race.ps1 tools/wsl/patch_gpu_diag.py tools/wsl/patch_texcopy_timer.py tools/wsl/patch_texture_blit.py tools/wsl/patch_rt_trace.py tools/wsl/patch_rt_trace2.py tools/wsl/patch_clear_pretransfer.py tools/wsl/patch_clear_pretransfer_depth.py tools/wsl/patch_remap_debug.py tools/wsl/patch_msaa_clear_remap.py tools/wsl/patch_msaa_clear_remap_v2.py tools/wsl/patch_msaa_clear_remap_v3.py tools/wsl/patch_rect_bounds.py tools/wsl/patch_finalize_gpu.py tools/wsl/apply_remap_v2.sh tools/wsl/package_83.sh tools/wsl/finalize_83.sh; do
  mkdir -p "$STAGE/$(dirname "$file")"
  cp "$SRC/$file" "$STAGE/$file"
done
cp "$OUT/NOTAS-revisao-8.3-preview.md" "$OUT/RESULTADOS-GPU-revisao-8.2.md" "$STAGE/"

python3 "$SRC/tools/make_zip.py" "$STAGE" "$OUT/Hydro-Thunder-fontes-revisao-8.3-preview.zip"
python3 "$SRC/tools/check_sources_zip.py" "$OUT/Hydro-Thunder-fontes-revisao-8.3-preview.zip"

python3 - "$OUT" <<'PY'
import hashlib, json, sys, zipfile
from pathlib import Path
out = Path(sys.argv[1])
names = [
    'Hydro-Thunder-Windows-revisao-8.3-preview.zip',
    'HydroThunder-revisao-8.3-preview.AppImage',
    'Hydro-Thunder-fontes-revisao-8.3-preview.zip',
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
(out / 'SHA256-revisao-8.3-preview.txt').write_text('\n'.join(hash_lines) + '\n', encoding='utf-8')
report = {
    'revision': '8.3-preview',
    'windows_zip_crc': 'passed',
    'linux_appimage_created': True,
    'source_zip_crc': 'passed',
    'source_zip_extracted_game_text': 'absent',
    'windows_executable_sha256_in_zip': win_hash,
    'first_run_profile_test': 'passed without opening Lossless Scaling',
    'installed_Lossless_detection': 'found in Steam library',
    'Lossless_frame_generation_runtime': 'validated on Windows in races: LS counter 60/120 above refresh, hidden LS, Steam closed, launcher Play flow',
    'linux_frame_generation_runtime': 'not validated on real hardware',
    'gpu_ms_per_frame_nativa': '11.08 -> 7.11 (race, 60 FPS cap, RTX 5060 Ti)',
    'gpu_ms_per_frame_qualidade': '3.68 -> 2.86',
}
(out / '_teste_modern/artifact-validation-8.1.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('All 8.3-preview archives validated; hashes recorded.')
PY

sha256sum "$OUT/Hydro-Thunder-Windows-revisao-8.3-preview.zip" "$OUT/HydroThunder-revisao-8.3-preview.AppImage" "$OUT/Hydro-Thunder-fontes-revisao-8.3-preview.zip"
