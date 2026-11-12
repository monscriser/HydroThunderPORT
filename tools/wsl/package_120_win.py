"""1.2.0 BETA Windows PGO zip from the 1.1.0 PGO zip layout + the exact binaries of the W120 proof runs.
Zip root: ONLY HydroThunder.exe, Launcher-Config.exe and data/ (Ian's rule).
usage: python3 -I package_120_win.py"""
import hashlib, json, re, shutil, zipfile
from pathlib import Path

HP = Path('/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port')
SRC = Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7')
OUTS = Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs')
BASE = OUTS / '1.1.0-BETA/Hydro-Thunder-1.1.0-BETA-Windows-PGO.zip'
OUT = OUTS / '1.2.0-BETA'
BIN = HP / 'final-120-win-pgo'
STAGE = Path('/root/ht/pkg/stage120win')
NAME = 'Hydro-Thunder-1.2.0-BETA-Windows-PGO'
NOTES = 'NOTAS-1.2.0-BETA.md'


def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


# The binaries that ran the proof race (race_911.ps1 records their hashes).
proof = {Path(r['Path'].replace('\\', '/')).name: r['Hash'].lower()
         for r in json.loads((HP / 'runs/W120-prova/binarios.json').read_text(encoding='utf-8-sig'))}
for n in ('hydrothunder.exe', 'librexruntime.dll', 'rexgpu-xenos.dll'):
    assert sha(BIN / n) == proof[n], n
assert b'1.2.0-beta-pgo' in (BIN / 'hydrothunder.exe').read_bytes()
assert b'ht_texture_set_cache' in (BIN / 'rexgpu-xenos.dll').read_bytes()

if STAGE.exists():
    shutil.rmtree(STAGE)
win = STAGE / NAME
with zipfile.ZipFile(BASE) as z:
    for n in z.namelist():
        assert not Path(n).is_absolute() and '..' not in Path(n).parts
    z.extractall(win)
for p in win.glob('data/NOTAS-*.md'):
    p.unlink()
shutil.copy2(SRC / NOTES, win / 'data')
for n in ('hydrothunder.exe', 'librexruntime.dll', 'rexgpu-xenos.dll'):
    shutil.copy2(BIN / n, win / 'data' / n)
shutil.copy2(SRC / 'ui/Roboto-Medium.ttf', win / 'data/ui')
toml = win / 'data/hydrothunder.toml'
text = re.sub(r'(?m)^# (9\.[012]\.0|1\.[012]\.0)[^\n]*\n', '', toml.read_text())
toml.write_text('# 1.2.0 BETA: to compare, set ht_texture_set_cache / ht_request_range_lockless / ht_zpd_batch / '
                'ht_dynamic_constants / ht_cp_fast_registers / ht_async_submit to false and restart.\n' + text)
assert sorted(p.name for p in win.iterdir()) == ['HydroThunder.exe', 'Launcher-Config.exe', 'data']
for need in ('data/steam_art/logo.png', 'data/steam_art/monscriser/hero.png', 'data/ui/Roboto-Medium.ttf',
             'data/libamd_fidelityfx_vk.dll'):
    assert (win / need).is_file(), need

OUT.mkdir(parents=True, exist_ok=True)
target = OUT / f'{NAME}.zip'
if target.exists():
    target.unlink()
with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for p in sorted(win.rglob('*')):
        if p.is_file():
            z.write(p, p.relative_to(win).as_posix())
with zipfile.ZipFile(target) as z:
    assert z.testzip() is None
    names = z.namelist()
    assert not any('\\' in n or {'game_data', 'user_data', '_to_delete'} & set(Path(n).parts) for n in names)
    assert not any(n.endswith(('.xex', '.apf', 'ht2save.dat', '.profraw')) for n in names)
    assert {n.split('/')[0] for n in names} == {'HydroThunder.exe', 'Launcher-Config.exe', 'data'}
    for n in ('hydrothunder.exe', 'librexruntime.dll', 'rexgpu-xenos.dll'):
        assert hashlib.sha256(z.read('data/' + n)).hexdigest() == proof[n], n
print(target, sha(target))
