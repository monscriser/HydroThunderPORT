"""Package and validate revision 8.9.2 without replacing earlier deliverables."""
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

base, src, out, work, win, linux_bin = map(Path, sys.argv[1:])
stage = work / 'Hydro-Thunder-fontes-revisao-8.9.2-preview'
stage.mkdir(exist_ok=False)
opt = out / 'otimizacao-8.9'

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

with zipfile.ZipFile(out / 'Hydro-Thunder-fontes-revisao-8.9.1-preview.zip') as z:
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
for folder in ('app/src', 'tools/test', 'tools/wsl', 'tools/portatil', 'sdk-extra'):
    for active in (src / folder).rglob('*'):
        if active.is_file() and '__pycache__' not in active.parts:
            target = stage / active.relative_to(src)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(active, target)
for name in ('NOTAS-revisao-8.9.2-preview.md', 'NOTAS-revisao-8.9.1-preview.md', 'NOTAS-revisao-8.9-preview.md'):
    shutil.copy2(out / name, stage / name)
for name in ('COMO-CONTINUAR.md', 'PLANO-9.0.0.md'):
    shutil.copy2(src / name, stage / name)
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

names = ['Hydro-Thunder-Windows-revisao-8.9.2-preview.zip',
         'HydroThunder-revisao-8.9.2-preview.AppImage',
         'Hydro-Thunder-fontes-revisao-8.9.2-preview.zip']
archive(win, out / names[0])
archive(stage, out / names[2])
with zipfile.ZipFile(out / names[2]) as z:
    forbidden = ('base_ptpt.jsonl', 'dlc_ptpt.jsonl', 'base_en.jsonl', 'ptbr_base.jsonl', 'ptbr_dlc_final.jsonl')
    assert not any(n.endswith(forbidden) for n in z.namelist())
    for name in ('app/src/main.cpp', 'app/src/ht_version.h', 'app/src/ht_setup_wizard.cpp', 'app/src/ht_keyboard_binds.h',
                 'tools/wsl/patch_request_range_fast.py', 'tools/wsl/patch_zpd_exact_key.py', 'rexglue-modifications.patch', 'CLAUDE.md', 'AGENTS.md', 'provenance.json', 'COMO-CONTINUAR.md', 'PLANO-9.0.0.md'):
        assert hashlib.sha256(z.read(name)).hexdigest() == sha(src / name), name
with zipfile.ZipFile(out / names[0]) as z:
    tested_win = base / 'outputs/_teste_modern/game-test/hydrothunder.exe'
    assert hashlib.sha256(z.read('data/hydrothunder.exe')).hexdigest() == sha(tested_win)
    for name in ('librexruntime.dll', 'rexgpu-xenos.dll', 'libamd_fidelityfx_vk.dll'):
        assert hashlib.sha256(z.read('data/' + name)).hexdigest() == sha(tested_win.parent / name), name

# The Linux smoke copies receive exactly the same rpath normalization.
smoke_bin = Path('/root/ht/pkg/smoke892-20261016-1520')
linux_hashes = {}
for name in ('hydrothunder', 'librexruntime.so', 'librexgpu-xenos.so'):
    assert sha(linux_bin / name) == sha(smoke_bin / name), name
    linux_hashes[name] = sha(linux_bin / name)
assert 'revision-8.9.2-preview' in (src / 'app/src/ht_version.h').read_text()
patch_text = (src / 'rexglue-modifications.patch').read_text(errors='replace')
assert 'ht_request_range_fast' in patch_text and 'ht_zpd_exact_key' in patch_text and 'ht-zpd-stats' not in patch_text

def last(pattern, text):
    rows = re.findall(pattern, text)
    assert rows, pattern
    return rows

# Exact proofs with the final DLL (race, all verifiers on) and the A/B timing.
race = (opt / '96-892-prova-exata-corrida/game.log').read_text(errors='replace')
fconst = last(r'\[ht-fconst-verify\] copies=(\d+) bytes=(\d+) mismatches=(\d+)', race)
direct = last(r'\[ht-direct-verify\] loads=(\d+) texels=(\d+) groups=\d+ mismatches=(\d+)', race)
rrfast = last(r'\[ht-rrfast-verify\] fast_hits=(\d+) original_uploads=(\d+)', race)
assert all(int(r[2]) == 0 for r in fconst) and all(int(r[2]) == 0 for r in direct) and all(int(r[1]) == 0 for r in rrfast)
menu_break = (opt / '82-rrfast/menu-verify-break.log').read_text(errors='replace')
assert int(last(r'original_uploads=(\d+)', menu_break)[-1]) > 0, 'positive control must fail'
normal = json.loads((opt / '97-892-corrida-60/result.json').read_text(encoding='utf-8-sig'))
assert normal['FPS'] == 60 and not normal['TravouOuFechou']
linux = (opt / 'linux-llvmpipe-892/game.log').read_text(errors='replace')
assert all(int(r[2]) == 0 for r in last(r'\[ht-fconst-verify\] copies=(\d+) bytes=(\d+) mismatches=(\d+)', linux))

zx = (opt / '94-zpd-chave-exata/X-chave-antiga/game.log').read_text(errors='replace')
zy = (opt / '94-zpd-chave-exata/Y-chave-exata/game.log').read_text(errors='replace')
stat = lambda t: [int(v) for v in re.findall(r'\[ht-zpd-stats\] intervals=(\d+) leak=(\d+) miss=(\d+)', t)[-1]]
old, new = stat(zx), stat(zy)
assert new[1] * 100 < old[1] and new[2] * 100 < old[2], (old, new)
assert all(int(r[2]) == 0 for r in last(r'\[ht-direct-verify\] loads=(\d+) texels=(\d+) groups=\d+ mismatches=(\d+)', linux))
report = {'revision': '8.9.2-preview', 'zip_crc_checks': 'passed',
          'changes': ['launcher Controls tab (keyboard binds)', 'Saves menu only saves; Add to Steam under Manage installation',
                      'footer: Play + current keys', 'ht_request_range_fast (SDK, CPU)'],
          'race_exact_proofs': {'fconst_bytes': int(fconst[-1][1]), 'direct_texels': int(direct[-1][1]),
                                'rrfast_hits': int(rrfast[-1][0]), 'mismatches': 0},
          'rrfast_positive_control_failed_as_expected': True,
          'zpd_leaks_old_vs_exact': {'intervals': old[0], 'leak_old': old[1], 'leak_exact': new[1], 'miss_old': old[2], 'miss_exact': new[2]},
          'normal_race': {k: normal[k] for k in ('FPS', 'FPSmin', 'PiorQuadro_ms', 'SemFoco')},
          'request_range_ms_per_frame': {'off': 0.456, 'on': 0.334, 'us_per_call_off': 0.195, 'us_per_call_on': 0.132},
          'windows_executable_matches_tested_file': True,
          'linux_binaries_match_llvmpipe_test': linux_hashes,
          'keyboard_binds_in_race_tested_by_player': False,
          'race_finish_confirmed': False,
          'linux_real_gpu_and_ally_deck_test': False}
(opt / 'build-892/artifact-validation.json').write_text(json.dumps(report, indent=2) + '\n')
(out / 'SHA256-revisao-8.9.2-preview.txt').write_text(''.join(f'{sha(out / name)}  {name}\n' for name in names))
print('Windows/source archives checked; Linux binaries match the smoke test; hashes saved.')
