"""Package 1.0.0 BETA (Standard and PGO) from the exact tested binaries.
Windows: hydro-port/final-100-win-<v> (exe per variant, the DLLs of the f911 exact proofs).
Linux: /root/ht/pkg/revision100-<v>/squashfs-root, whose binaries are compared with smoke100-<v>.
Windows zip root: ONLY HydroThunder.exe, Launcher-Config.exe and data/ (Ian's rule)."""
import hashlib, json, re, shutil, sys, zipfile
from pathlib import Path
ARCH = Path('/mnt/e/PROJETOS ANTIGOS/Codex-arquivados-2026-10-21/2026-10-16/continue-o-projeto-hydro-thunder-hurricane')
HP = Path('/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/hydro-port')
SRC = Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7')
OUT = Path(sys.argv[1])
STAGE = Path('/root/ht/pkg/stage100')
NOTES = 'NOTAS-1.0.0-BETA.md'
VARIANTS = {'standard': 'Standard', 'pgo': 'PGO'}
skip = {'_to_delete', '__pycache__'}

def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''): h.update(block)
    return h.hexdigest()

# --- checks ---
version = (SRC / 'app/src/ht_version.h').read_text()
assert '"1.0.0-beta-standard"' in version and '"1.0.0-beta-pgo"' in version
patch = (SRC / 'rexglue-modifications.patch').read_text(errors='replace')
assert 'REXCVAR_DEFINE_BOOL(ht_request_range_lockless, true,' in patch
proof = {}
for row in json.loads((HP / 'runs/f911-proof-scale1/binarios.json').read_text(encoding='utf-8-sig')):
    proof[row['Path'].replace('\\', '/').split('/')[-1]] = row['Hash'].lower()
for v in VARIANTS:
    w = HP / f'final-100-win-{v}'
    for dll in ('librexruntime.dll', 'rexgpu-xenos.dll'): assert sha(w / dll) == proof[dll], (v, dll)
    assert f'1.0.0-beta-{v}'.encode() in (w / 'hydrothunder.exe').read_bytes(), v
    lb = Path(f'/root/ht/pkg/revision100-{v}/squashfs-root/usr/bin')
    for n in ('hydrothunder', 'librexruntime.so', 'librexgpu-xenos.so', 'libamd_fidelityfx_vk.so'):
        assert sha(lb / n) == sha(Path(f'/root/ht/pkg/smoke100-{v}') / n), (v, n)
    assert f'1.0.0-beta-{v}'.encode() in (lb / 'hydrothunder').read_bytes(), v
assert sha(HP / 'final-100-win-standard/hydrothunder.exe') != sha(HP / 'final-100-win-pgo/hydrothunder.exe')

def update_toml(profile):
    text = re.sub(r'(?m)^# (9\.[012]\.0|1\.0\.0)[^\n]*\n', '', profile.read_text())
    return '# 1.0.0 BETA: to compare, set ht_request_range_lockless / ht_zpd_batch / ht_dynamic_constants / ht_cp_fast_registers / ht_async_submit to false and restart.\n' + text

if STAGE.exists(): shutil.rmtree(STAGE)
OUT.mkdir(parents=True, exist_ok=True)

def archive(folder, target):
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(folder.rglob('*')):
            rel = p.relative_to(folder)
            if p.is_file() and not skip & set(rel.parts): z.write(p, rel.as_posix())
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        names = z.namelist()
        assert not any('\\' in n or {'game_data', 'user_data', '_to_delete'} & set(Path(n).parts) for n in names)
        forbidden = ('base_ptpt.jsonl', 'dlc_ptpt.jsonl', 'base_en.jsonl', 'ptbr_base.jsonl', 'ptbr_dlc_final.jsonl', '.xex', '.apf', 'ht2save.dat', '.profraw')
        assert not any(n.endswith(forbidden) for n in names), [n for n in names if n.endswith(forbidden)]

# --- Windows, one zip per variant ---
for v, label in VARIANTS.items():
    win = STAGE / f'Hydro-Thunder-1.0.0-BETA-Windows-{label}'
    shutil.copytree(ARCH / 'work/Hydro-Thunder-Windows-revisao-9.1.0', win)
    for p in win.glob('data/NOTAS-revisao-*.md'): p.unlink()
    shutil.copy2(SRC / NOTES, win / 'data')
    for p in (HP / f'final-100-win-{v}').iterdir(): shutil.copy2(p, win / 'data' / p.name)
    shutil.copy2(SRC / 'ui/Roboto-Medium.ttf', win / 'data/ui')
    shutil.copy2(SRC / 'licenses/LICENSE-Roboto-Apache-2.0.txt', win / 'data/licenses')
    (win / 'data/hydrothunder.toml').write_text(update_toml(win / 'data/hydrothunder.toml'))
    assert sorted(p.name for p in win.iterdir()) == ['HydroThunder.exe', 'Launcher-Config.exe', 'data']
    target = OUT / f'{win.name}.zip'
    archive(win, target)
    with zipfile.ZipFile(target) as z:
        for p in (HP / f'final-100-win-{v}').iterdir(): assert hashlib.sha256(z.read('data/' + p.name)).hexdigest() == sha(p), p.name
        assert {n.split('/')[0] for n in z.namelist()} == {'HydroThunder.exe', 'Launcher-Config.exe', 'data'}
        assert 'data/ui/Roboto-Medium.ttf' in z.namelist()

# --- Linux AppImage dirs (packed by package_100.sh) ---
for v in VARIANTS:
    lb = Path(f'/root/ht/pkg/revision100-{v}/squashfs-root/usr/bin')
    old = lb.parents[2] / 'old-notes'; old.mkdir(exist_ok=True)
    for p in lb.glob('NOTAS-*.md'): shutil.move(str(p), old / p.name)
    shutil.copy2(SRC / NOTES, lb)
    (lb / 'hydrothunder.toml').write_text(update_toml(lb / 'hydrothunder.toml'))

# --- sources: 9.1.0 sources + everything changed since + PGO profiles ---
(SRC / 'pgo').mkdir(exist_ok=True)
shutil.copy2('/root/ht/pgo-both.profdata', SRC / 'pgo/hydrothunder-windows.profdata')
shutil.copy2('/root/ht/pgo-linux-both.profdata', SRC / 'pgo/hydrothunder-linux.profdata')
src_stage = STAGE / 'Hydro-Thunder-1.0.0-BETA-fontes'
with zipfile.ZipFile(ARCH / 'outputs/Hydro-Thunder-fontes-revisao-9.1.0.zip') as z:
    for n in z.namelist(): assert not Path(n).is_absolute() and '..' not in Path(n).parts
    z.extractall(src_stage)
for p in list(src_stage.rglob('*')):
    rel = p.relative_to(src_stage)
    if p.is_file() and (SRC / rel).is_file(): shutil.copy2(SRC / rel, p)
for folder in ('app/src', 'tools/test', 'tools/wsl', 'tools/portatil', 'sdk-extra', 'ui', 'licenses', 'pgo'):
    for p in (SRC / folder).rglob('*'):
        if p.is_file() and not skip & set(p.parts):
            t = src_stage / p.relative_to(SRC); t.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p, t)
for name in ('app/CMakeLists.txt', 'AGENTS.md', 'CLAUDE.md', 'provenance.json', 'rexglue-modifications.patch',
             'RESULTADOS-PORTE-UFC-911.md', 'PLANO-OTIMIZACAO-FINAL.md', NOTES):
    t = src_stage / name; t.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(SRC / name, t)
for p in src_stage.glob('NOTAS-*.md'):
    if p.name != NOTES: p.unlink()
pre = src_stage / 'prebuilt-windows'; pre.mkdir(exist_ok=True)
for p in (HP / 'final-100-win-standard').iterdir(): shutil.copy2(p, pre / p.name)
target = OUT / 'Hydro-Thunder-1.0.0-BETA-fontes.zip'
archive(src_stage, target)
with zipfile.ZipFile(target) as z:
    for name in ('app/src/ht_version.h', 'app/CMakeLists.txt', 'app/src/main.cpp', 'rexglue-modifications.patch', 'pgo/hydrothunder-linux.profdata', 'tools/wsl/build_profile_runtime.sh'):
        assert hashlib.sha256(z.read(name)).hexdigest() == sha(SRC / name), name
print('Windows (Standard, PGO) and source packages verified against the tested binaries and the sources.')
