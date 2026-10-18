"""Package and validate revision 8.9 without replacing earlier deliverables."""
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

base, src, out, work, win, linux_bin = map(Path, sys.argv[1:])
stage = work / 'Hydro-Thunder-fontes-revisao-8.9-preview'
stage.mkdir(exist_ok=False)

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

with zipfile.ZipFile(out / 'Hydro-Thunder-fontes-revisao-8.8-preview.zip') as z:
    for name in z.namelist():
        assert not Path(name).is_absolute() and '..' not in Path(name).parts
    z.extractall(stage)

# Refresh every previously curated source entry, then add new tools/notes.
for path in list(stage.rglob('*')):
    rel = path.relative_to(stage)
    if '_to_delete' in rel.parts:
        continue
    active = src / rel
    if path.is_file() and active.is_file():
        shutil.copy2(active, path)
for folder in ('app/src', 'tools/test', 'tools/wsl', 'sdk-extra'):
    for active in (src / folder).rglob('*'):
        if active.is_file() and '__pycache__' not in active.parts:
            target = stage / active.relative_to(src)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(active, target)
shutil.copy2(out / 'NOTAS-revisao-8.9-preview.md', stage / 'NOTAS-revisao-8.9-preview.md')
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

names = ['Hydro-Thunder-Windows-revisao-8.9-preview.zip',
         'HydroThunder-revisao-8.9-preview.AppImage',
         'Hydro-Thunder-fontes-revisao-8.9-preview.zip']
archive(win, out / names[0])
archive(stage, out / names[2])
with zipfile.ZipFile(out / names[2]) as z:
    forbidden = ('base_ptpt.jsonl', 'dlc_ptpt.jsonl', 'base_en.jsonl', 'ptbr_base.jsonl', 'ptbr_dlc_final.jsonl')
    assert not any(n.endswith(forbidden) for n in z.namelist())
    for name in ('app/src/main.cpp', 'app/src/ht_spin_backoff.h', 'app/src/ht_version.h', 'sdk-extra/include/rex/graphics/ht_float_constant_copy.h', 'rexglue-modifications.patch', 'CLAUDE.md', 'AGENTS.md', 'provenance.json'):
        assert hashlib.sha256(z.read(name)).hexdigest() == sha(src / name), name
with zipfile.ZipFile(out / names[0]) as z:
    tested_win = base / 'outputs/_teste_modern/game-test/hydrothunder.exe'
    assert hashlib.sha256(z.read('data/hydrothunder.exe')).hexdigest() == sha(tested_win)
    for name in ('librexruntime.dll', 'rexgpu-xenos.dll', 'libamd_fidelityfx_vk.dll'):
        assert hashlib.sha256(z.read('data/' + name)).hexdigest() == sha(tested_win.parent / name), name

# The Linux smoke copies receive exactly the same rpath normalization.
smoke_bin = Path('/root/ht/pkg/smoke89-20261016-1320')
linux_hashes = {}
for name in ('hydrothunder', 'librexruntime.so', 'librexgpu-xenos.so'):
    assert sha(linux_bin / name) == sha(smoke_bin / name), name
    linux_hashes[name] = sha(linux_bin / name)
assert 'revision-8.9-exit-preview' in (src / 'app/src/ht_version.h').read_text()

# Exit proof: two fixed runs log [ht-exit]; the control (fix off) does not.
proofs = out / 'provas-saida-8.9'
exit_lines = {p.name: len(re.findall(r'\[ht-exit\]', p.read_text(errors='replace')))
              for p in sorted(proofs.glob('hydrothunder_*.log'))}
assert sorted(exit_lines.values()) == [0, 1, 1], exit_lines
assert 'MnkInputDriver::~MnkInputDriver' in (proofs / 'controle-pilha.txt').read_text(errors='replace')
log = (out / 'otimizacao-8.9/linux-llvmpipe-89/game.log').read_text(errors='replace')
fconst = re.findall(r'\[ht-fconst-verify\] copies=(\d+) bytes=(\d+) mismatches=(\d+)', log)
assert fconst and all(int(r[2]) == 0 for r in fconst)

report = {'revision': '8.9-preview', 'zip_crc_checks': 'passed',
          'change': 'guest self-exit hard-exits like a window close (ht_guest_exit_hard)',
          'exit_proof_logs': exit_lines,
          'control_crash_same_stack': True,
          'dlls_identical_to_8_8': True,
          'race_finish_confirmed': False,
          'windows_executable_matches_tested_file': True,
          'linux_binaries_match_llvmpipe_test': linux_hashes,
          'linux_exit_path_exercised': False,
          'linux_real_gpu_and_ally_deck_test': False,
          'audio_listening_check': 'not independently verified by assistant'}
(out / 'otimizacao-8.9/build-89/artifact-validation.json').write_text(json.dumps(report, indent=2) + '\n')
(out / 'SHA256-revisao-8.9-preview.txt').write_text(''.join(f'{sha(out / name)}  {name}\n' for name in names))
print('Windows/source archives checked; Linux binaries match the smoke test; hashes saved.')
