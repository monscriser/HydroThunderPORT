"""Package and validate revision 8.7 without replacing earlier deliverables."""
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

base, src, out, work, win, linux_bin = map(Path, sys.argv[1:])
stage = work / 'Hydro-Thunder-fontes-revisao-8.7-preview'
stage.mkdir(exist_ok=False)

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

with zipfile.ZipFile(base / 'outputs/Hydro-Thunder-fontes-revisao-8.6-preview.zip') as z:
    for name in z.namelist():
        assert not Path(name).is_absolute() and '..' not in Path(name).parts
    z.extractall(stage)

# Refresh every previously curated source entry, then add the new CPU tools.
for path in list(stage.rglob('*')):
    rel = path.relative_to(stage)
    if '_to_delete' in rel.parts:
        continue
    active = src / rel
    if path.is_file() and active.is_file():
        shutil.copy2(active, path)
for folder in ('app/src', 'tools/test', 'tools/wsl'):
    for active in (src / folder).rglob('*'):
        if active.is_file() and '__pycache__' not in active.parts:
            target = stage / active.relative_to(src)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(active, target)
for name in ('NOTAS-revisao-8.7-preview.md', 'RESULTADOS-CPU-revisao-8.7.md'):
    shutil.copy2(out / name, stage / name)
for name in ('hydrothunder.exe', 'librexruntime.dll', 'rexgpu-xenos.dll', 'libamd_fidelityfx_vk.dll'):
    shutil.copy2(win / 'data' / name, stage / 'prebuilt-windows' / name)

def archive(folder, target):
    assert not target.exists(), f'Preserve existing output: {target}'
    files = sorted(p for p in folder.rglob('*') if p.is_file()
                   and '_to_delete' not in p.relative_to(folder).parts
                   and '__pycache__' not in p.relative_to(folder).parts)
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in files:
            z.write(path, path.relative_to(folder).as_posix())
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        assert not any('\\' in n for n in z.namelist())
        assert not any(part in ('game_data', 'user_data', '_to_delete') for n in z.namelist() for part in Path(n).parts)

names = ['Hydro-Thunder-Windows-revisao-8.7-preview.zip',
         'HydroThunder-revisao-8.7-preview.AppImage',
         'Hydro-Thunder-fontes-revisao-8.7-preview.zip']
archive(win, out / names[0])
archive(stage, out / names[2])
with zipfile.ZipFile(out / names[2]) as z:
    forbidden = ('base_ptpt.jsonl', 'dlc_ptpt.jsonl', 'base_en.jsonl', 'ptbr_base.jsonl', 'ptbr_dlc_final.jsonl')
    assert not any(n.endswith(forbidden) for n in z.namelist())
    for name in ('app/src/main.cpp', 'app/src/ht_spin_backoff.h', 'app/src/ht_version.h', 'rexglue-modifications.patch', 'CLAUDE.md', 'AGENTS.md', 'provenance.json'):
        assert hashlib.sha256(z.read(name)).hexdigest() == sha(src / name), name
with zipfile.ZipFile(out / names[0]) as z:
    tested_win = base / 'outputs/_teste_modern/game-test/hydrothunder.exe'
    assert hashlib.sha256(z.read('data/hydrothunder.exe')).hexdigest() == sha(tested_win)
    for name in ('librexruntime.dll', 'rexgpu-xenos.dll', 'libamd_fidelityfx_vk.dll'):
        assert hashlib.sha256(z.read('data/' + name)).hexdigest() == sha(tested_win.parent / name), name

# The Linux smoke copies receive exactly the same rpath normalization.
smoke_bin = Path('/root/ht/pkg/smoke87-20261016-1010')
linux_hashes = {}
for name in ('hydrothunder', 'librexruntime.so', 'librexgpu-xenos.so'):
    assert sha(linux_bin / name) == sha(smoke_bin / name), name
    linux_hashes[name] = sha(linux_bin / name)
assert 'revision-8.7-cpu-preview' in (src / 'app/src/ht_version.h').read_text()
measurements = [json.loads((out / 'provas-cpu' / name / 'measurement.json').read_text(encoding='utf-8-sig')) for name in (
    '53-cpu-corrida-60-off', '54-cpu-corrida-60-on', '55-cpu-corrida-unlimited-off', '56-cpu-corrida-unlimited-on')]
assert measurements[0]['Fps'] == measurements[1]['Fps'] == 60
assert measurements[3]['Fps'] >= measurements[2]['Fps'] * 0.99
report = {'revision': '8.7-preview', 'zip_crc_checks': 'passed',
          'validation_status': 'preview candidate; manual finish/audio check pending',
          'race_finish_confirmed': False,
          'windows_executable_matches_tested_file': True,
          'linux_binaries_match_llvmpipe_test': linux_hashes,
          'source_matches_active_cpu_code': True,
          'source_prebuilt_windows_updated': True,
          'original_game_text_excluded': True,
          'measurements': measurements,
          'new_lossless_scaling_test': False,
          'linux_real_gpu_and_ally_deck_test': False,
          'audio_listening_check': 'not independently verified by assistant'}
(out / 'provas-cpu/build-87/artifact-validation.json').write_text(json.dumps(report, indent=2) + '\n')
(out / 'SHA256-revisao-8.7-preview.txt').write_text(''.join(f'{sha(out / name)}  {name}\n' for name in names))
print('Windows/source archives checked; Linux binaries match the smoke test; hashes saved.')
