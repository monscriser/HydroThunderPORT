"""Package 9.2.0 from the exact tested binaries (Windows: hydro-port/final-920-windows; Linux: the
smoke-tested /root/ht/pkg/smoke920-final). Previous releases are only read.
Windows zip root: ONLY HydroThunder.exe, Launcher-Config.exe and data/ (Ian's rule)."""
import hashlib, re, shutil, sys, zipfile, json
from pathlib import Path
version = '9.2.0'
ARCH = Path('/mnt/e/PROJETOS ANTIGOS/Codex-arquivados-2026-10-21/2026-10-16/continue-o-projeto-hydro-thunder-hurricane')
HP = Path('/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port')
SRC = Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7')
OUT = Path(sys.argv[1])
winbins = HP / 'final-920-windows'
appdir = Path('/root/ht/pkg/revision920-final/squashfs-root')
smoke = Path('/root/ht/pkg/smoke920-final')
STAGE = Path('/root/ht/pkg/stage920')

def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''): h.update(block)
    return h.hexdigest()

# --- checks on sources and binaries ---
assert '"revision-9.2.0"' in (SRC / 'app/src/ht_version.h').read_text()
patch = (SRC / 'rexglue-modifications.patch').read_text(errors='replace')
assert 'REXCVAR_DEFINE_BOOL(ht_request_range_lockless, true,' in patch
assert 'REXCVAR_DEFINE_BOOL(ht_fetch_unchanged_skip, false,' in patch
assert 'ht_resume_running' not in patch and 'ht_xma_loop_end' not in patch
# The video DLL and runtime are the ones every exact proof ran with (f911-* runs).
for row in json.loads((HP / 'runs/f911-proof-scale1/binarios.json').read_text(encoding='utf-8-sig')):
    name = row['Path'].replace('\\', '/').split('/')[-1]
    if name != 'hydrothunder.exe':
        assert row['Hash'].lower() == sha(winbins / name), name
linuxbin = appdir / 'usr/bin'
for name in ('hydrothunder', 'librexruntime.so', 'librexgpu-xenos.so', 'libamd_fidelityfx_vk.so'):
    assert sha(linuxbin / name) == sha(smoke / name), name

def update_toml(profile):
    text = profile.read_text()
    text = re.sub(r'(?m)^# 9\.[01]\.0:.*\n', '', text)
    return '# 9.2.0: to compare, set ht_request_range_lockless / ht_zpd_batch / ht_dynamic_constants / ht_cp_fast_registers / ht_async_submit to false and restart.\n' + text

# --- Windows stage ---
if STAGE.exists(): shutil.rmtree(STAGE)
win = STAGE / f'Hydro-Thunder-Windows-revisao-{version}'
shutil.copytree(ARCH / 'work/Hydro-Thunder-Windows-revisao-9.1.0', win)
for p in win.glob('data/NOTAS-revisao-*.md'): p.unlink()          # stage copy only
shutil.copy2(SRC / f'NOTAS-revisao-{version}.md', win / 'data')
for p in winbins.iterdir(): shutil.copy2(p, win / 'data' / p.name)
shutil.copy2(SRC / 'ui/Roboto-Medium.ttf', win / 'data/ui')
shutil.copy2(SRC / 'licenses/LICENSE-Roboto-Apache-2.0.txt', win / 'data/licenses')
(win / 'data/hydrothunder.toml').write_text(update_toml(win / 'data/hydrothunder.toml'))
assert sorted(p.name for p in win.iterdir()) == ['HydroThunder.exe', 'Launcher-Config.exe', 'data']

# --- Linux AppImage dir (smoke-tested binaries already in place) ---
old = appdir.parent / 'old-notes'; old.mkdir(exist_ok=True)
for p in linuxbin.glob('NOTAS-revisao-*.md'): shutil.move(str(p), old / p.name)
shutil.copy2(SRC / f'NOTAS-revisao-{version}.md', linuxbin)
(linuxbin / 'hydrothunder.toml').write_text(update_toml(linuxbin / 'hydrothunder.toml'))
assert (linuxbin / 'ui/Roboto-Medium.ttf').is_file() and (linuxbin / 'licenses/LICENSE-Roboto-Apache-2.0.txt').is_file()

# --- source stage: 9.1.0 sources + everything changed since ---
src_stage = STAGE / f'Hydro-Thunder-fontes-revisao-{version}'
with zipfile.ZipFile(ARCH / 'outputs/Hydro-Thunder-fontes-revisao-9.1.0.zip') as z:
    for n in z.namelist(): assert not Path(n).is_absolute() and '..' not in Path(n).parts
    z.extractall(src_stage)
skip = {'_to_delete', '__pycache__'}
for p in list(src_stage.rglob('*')):
    rel = p.relative_to(src_stage)
    if p.is_file() and (SRC / rel).is_file(): shutil.copy2(SRC / rel, p)
for folder in ('app/src', 'tools/test', 'tools/wsl', 'tools/portatil', 'sdk-extra', 'ui', 'licenses'):
    for p in (SRC / folder).rglob('*'):
        if p.is_file() and not skip & set(p.parts):
            t = src_stage / p.relative_to(SRC); t.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p, t)
for name in ('AGENTS.md', 'CLAUDE.md', 'provenance.json', 'rexglue-modifications.patch',
             'RESULTADOS-PORTE-UFC-911.md', 'PLANO-OTIMIZACAO-FINAL.md', f'NOTAS-revisao-{version}.md'):
    shutil.copy2(SRC / name, src_stage / name)
for p in src_stage.glob('NOTAS-revisao-*.md'):
    if p.name != f'NOTAS-revisao-{version}.md': p.unlink()
(src_stage / 'prebuilt-windows').mkdir(exist_ok=True)
for name in ('hydrothunder.exe', 'librexruntime.dll', 'rexgpu-xenos.dll', 'libamd_fidelityfx_vk.dll', 'hydrothunder.toml'):
    shutil.copy2(win / 'data' / name, src_stage / 'prebuilt-windows' / name)

def archive(folder, target):
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(folder.rglob('*')):
            rel = p.relative_to(folder)
            if p.is_file() and not skip & set(rel.parts): z.write(p, rel.as_posix())
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        names = z.namelist()
        assert not any('\\' in n or {'game_data', 'user_data', '_to_delete'} & set(Path(n).parts) for n in names)
        forbidden = ('base_ptpt.jsonl', 'dlc_ptpt.jsonl', 'base_en.jsonl', 'ptbr_base.jsonl', 'ptbr_dlc_final.jsonl', '.xex', '.apf', 'ht2save.dat')
        assert not any(n.endswith(forbidden) for n in names), [n for n in names if n.endswith(forbidden)]
OUT.mkdir(parents=True, exist_ok=True)
archive(win, OUT / f'Hydro-Thunder-Windows-revisao-{version}.zip')
archive(src_stage, OUT / f'Hydro-Thunder-fontes-revisao-{version}.zip')
with zipfile.ZipFile(OUT / f'Hydro-Thunder-Windows-revisao-{version}.zip') as z:
    for p in winbins.iterdir(): assert hashlib.sha256(z.read('data/' + p.name)).hexdigest() == sha(p), p.name
    assert {n.split('/')[0] for n in z.namelist()} == {'HydroThunder.exe', 'Launcher-Config.exe', 'data'}
    assert 'data/ui/Roboto-Medium.ttf' in z.namelist()
with zipfile.ZipFile(OUT / f'Hydro-Thunder-fontes-revisao-{version}.zip') as z:
    for name in ('app/src/ht_version.h', 'app/src/ht_setup_wizard.cpp', 'app/src/ht_user_data.h', 'rexglue-modifications.patch', 'CLAUDE.md'):
        assert hashlib.sha256(z.read(name)).hexdigest() == sha(SRC / name), name
print('Windows and source packages verified against the tested binaries and the sources.')
