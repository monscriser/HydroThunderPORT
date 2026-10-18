#!/usr/bin/env bash
set -euo pipefail

BASE=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan
SRC="$BASE/work/Hydro-Thunder-fontes-revisao-7"
OUT="$BASE/outputs"
STAGE="$OUT/_teste_modern/Hydro-Thunder-fontes-revisao-8.1-preview"

mkdir -p "$STAGE"
unzip -oq "$OUT/Hydro-Thunder-fontes-revisao-8-preview.zip" -d "$STAGE"
for file in AGENTS.md CLAUDE.md app/src/ht_lossless.h app/src/ht_setup_wizard.cpp app/src/ht_version.h tools/test/lossless_profile.ps1 tools/wsl/build_lossless.sh tools/wsl/package_81.sh tools/wsl/finalize_81.sh; do
  mkdir -p "$STAGE/$(dirname "$file")"
  cp "$SRC/$file" "$STAGE/$file"
done
cp "$OUT/NOTAS-revisao-8.1-preview.md" "$OUT/REVISAO-DESEMPENHO-revisao-8.1.md" "$STAGE/"

python3 "$SRC/tools/make_zip.py" "$STAGE" "$OUT/Hydro-Thunder-fontes-revisao-8.1-preview.zip"
python3 "$SRC/tools/check_sources_zip.py" "$OUT/Hydro-Thunder-fontes-revisao-8.1-preview.zip"

python3 - "$OUT" <<'PY'
import hashlib, json, sys, zipfile
from pathlib import Path
out = Path(sys.argv[1])
names = [
    'Hydro-Thunder-Windows-revisao-8.1-preview.zip',
    'HydroThunder-revisao-8.1-preview.AppImage',
    'Hydro-Thunder-fontes-revisao-8.1-preview.zip',
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
(out / 'SHA256-revisao-8.1-preview.txt').write_text('\n'.join(hash_lines) + '\n', encoding='utf-8')
report = {
    'revision': '8.1-preview',
    'windows_zip_crc': 'passed',
    'linux_appimage_created': True,
    'source_zip_crc': 'passed',
    'source_zip_extracted_game_text': 'absent',
    'windows_executable_sha256_in_zip': win_hash,
    'first_run_profile_test': 'passed without opening Lossless Scaling',
    'installed_Lossless_detection': 'found in Steam library',
    'Lossless_frame_generation_runtime': 'not validated; Steam registry warning prevented launch test',
}
(out / '_teste_modern/artifact-validation-8.1.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('All 8.1-preview archives validated; hashes recorded.')
PY

sha256sum "$OUT/Hydro-Thunder-Windows-revisao-8.1-preview.zip" "$OUT/HydroThunder-revisao-8.1-preview.AppImage" "$OUT/Hydro-Thunder-fontes-revisao-8.1-preview.zip"
