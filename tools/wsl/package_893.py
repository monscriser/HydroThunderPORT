"""Package only the final tested binaries. Preserve all previous releases."""
import hashlib,json,re,shutil,sys,zipfile
from pathlib import Path
src,out,work,oldout,oldwork,appdir=map(Path,sys.argv[1:])
version='8.9.3-preview'
validation=out/'validacao-8.9.3'
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def last(text,tag):
    lines=[l for l in text.splitlines() if '['+tag+']' in l and '=' in l]
    assert lines,tag
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)\b',lines[-1])}
proofs={}
for run in ('proof-scale1','proof-scale2','rtt-proof-scale1','rtt-proof-scale2'):
    log=(validation/run/'game.log').read_text(errors='replace')
    assert 'Validation Error' not in log and 'Vulkan Error' not in log,run
    counters={t:last(log,t) for t in ('ht-fconst-verify','ht-direct-verify','ht-rrfast-verify','ht-zpd-pair-verify','ht-dynamic-constants')}
    assert counters['ht-fconst-verify']['bytes']>0
    assert counters['ht-direct-verify']['texels']>0
    assert counters['ht-rrfast-verify']['fast_hits']>0 and counters['ht-rrfast-verify']['original_uploads']==0
    assert counters['ht-zpd-pair-verify']['nonzero']>0
    assert counters['ht-dynamic-constants']['compared']>0
    assert all(c.get('mismatches',0)==0 for c in counters.values()),run
    if run.startswith('rtt-'):
        counters['ht-rtt']=last(log,'ht-rtt')
        assert counters['ht-rtt']['to_texture']>0 and counters['ht-rtt']['verified']>0
    else:
        counters['ht-dres-verify']=last(log,'ht-dres-verify')
        assert counters['ht-dres-verify']['words']>0 and counters['ht-dres-verify']['mismatches']==0
    proofs[run]=counters
for run in ('positive-scale1','rtt-positive-scale1'):
    log=(validation/run/'game.log').read_text(errors='replace')
    counters={t:last(log,t) for t in ('ht-fconst-verify','ht-rrfast-verify','ht-zpd-pair-verify','ht-dynamic-constants')}
    assert counters['ht-rrfast-verify']['original_uploads']>0
    assert all(c['mismatches']>0 for t,c in counters.items() if 'mismatches' in c)
    tag='ht-direct-verify' if run.startswith('rtt-') else 'ht-dres-verify'
    counters[tag]=last(log,tag)
    assert counters[tag]['mismatches']>0
    proofs[run]=counters
winbins=work/'final-windows'
for manifest in validation.glob('*-binaries.json'):
    rows=json.loads(manifest.read_text(encoding='utf-8-sig'))
    for row in rows:
        name=row['Path'].replace('\\','/').split('/')[-1]
        assert row['Hash'].lower()==sha(winbins/name),(manifest,name)
linuxbin=appdir/'usr/bin'
smoke=Path('/root/ht/pkg/smoke893-final')
for name in ('hydrothunder','librexruntime.so','librexgpu-xenos.so','libamd_fidelityfx_vk.so'):
    assert sha(linuxbin/name)==sha(smoke/name),name
linuxlog=(validation/'linux-llvmpipe/game.log').read_text(errors='replace')
for tag in ('ht-fconst-verify','ht-direct-verify','ht-dynamic-constants','ht-dres-verify'):
    c=last(linuxlog,tag)
    assert c.get('mismatches')==0 and (c.get('bytes',c.get('texels',c.get('compared',c.get('words',0)))))>0,tag
assert 'revision-8.9.3-preview' in (src/'app/src/ht_version.h').read_text()
patch=(src/'rexglue-modifications.patch').read_text(errors='replace')
for tag in ('ht_zpd_batch','ht_dynamic_constants','ht_zpd_exact_key','ht_request_range_fast'):assert tag in patch
assert 'REXCVAR_DEFINE_BOOL(ht_zpd_batch, true,' in patch
assert 'REXCVAR_DEFINE_BOOL(ht_dynamic_constants, true,' in patch
assert 'ht_bind_ticks' not in patch
win=work/f'Hydro-Thunder-Windows-revisao-{version}'
assert not win.exists()
shutil.copytree(oldwork/'Hydro-Thunder-Windows-revisao-8.9.2-preview',win)
backup=work/'_to_delete/windows-stage-893-old-notes'
backup.mkdir(parents=True,exist_ok=False)
for p in win.glob('NOTAS-revisao-*.md'):shutil.move(str(p),backup/p.name)
shutil.copy2(out/f'NOTAS-revisao-{version}.md',win)
for p in winbins.iterdir():shutil.copy2(p,win/'data'/p.name)
assert sha(win/'data/libamd_fidelityfx_vk.dll')==sha(Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/outputs/_teste_modern/game-test/libamd_fidelityfx_vk.dll'))
profile_backup=work/'_to_delete/profiles-before-893';profile_backup.mkdir(exist_ok=False)
for label,profile in (('windows',win/'data/hydrothunder.toml'),('linux',linuxbin/'hydrothunder.toml')):
    assert profile.is_file(),profile
    shutil.copy2(profile,profile_backup/(label+'.toml'))
    text=profile.read_text()
    values={'ht_zpd_batch':'true','ht_dynamic_constants':'true','ht_zpd_exact_key':'true','ht_zpd_flush':'false'}
    for key,value in values.items():
        line=f'{key} = {value}'
        if re.search(rf'(?m)^{key}\s*=',text):text=re.sub(rf'(?m)^{key}\s*=.*$',line,text)
        else:text=line+'\n'+text
    profile.write_text('# 8.9.3 preview: set batch/dynamic_constants to false and restart to compare.\n'+text)
prebuilt=src/'prebuilt-windows';prebuilt.mkdir(exist_ok=True)
prebuilt_backup=src/'_to_delete/prebuilt-before-893';prebuilt_backup.mkdir(exist_ok=False)
for name in ('hydrothunder.exe','librexruntime.dll','rexgpu-xenos.dll','hydrothunder.toml'):
    if (prebuilt/name).exists():shutil.copy2(prebuilt/name,prebuilt_backup/name)
    shutil.copy2(win/'data'/name,prebuilt/name)
stage=work/f'Hydro-Thunder-fontes-revisao-{version}'
stage.mkdir(exist_ok=False)
with zipfile.ZipFile(oldout/'Hydro-Thunder-fontes-revisao-8.9.2-preview.zip') as z:
    for n in z.namelist():assert not Path(n).is_absolute() and '..' not in Path(n).parts
    z.extractall(stage)
for p in list(stage.rglob('*')):
    rel=p.relative_to(stage)
    if p.is_file() and (src/rel).is_file():shutil.copy2(src/rel,p)
for folder in ('app/src','tools/test','tools/wsl','tools/portatil','sdk-extra'):
    for p in (src/folder).rglob('*'):
        if p.is_file() and not {'_to_delete','__pycache__'}&set(p.parts):
            target=stage/p.relative_to(src);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
for name in ('COMO-CONTINUAR.md','PLANO-9.0.0.md','AGENTS.md','CLAUDE.md','provenance.json','rexglue-modifications.patch'):
    shutil.copy2(src/name,stage/name)
shutil.copy2(out/f'NOTAS-revisao-{version}.md',stage)
for name in ('hydrothunder.exe','librexruntime.dll','rexgpu-xenos.dll','libamd_fidelityfx_vk.dll'):
    shutil.copy2(win/'data'/name,stage/'prebuilt-windows'/name)
shutil.copy2(win/'data/hydrothunder.toml',stage/'prebuilt-windows/hydrothunder.toml')
def archive(folder,target):
    with zipfile.ZipFile(target,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(folder.rglob('*')):
            rel=p.relative_to(folder)
            if p.is_file() and not {'_to_delete','__pycache__'}&set(rel.parts):z.write(p,rel.as_posix())
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        assert not any('\\' in n or {'game_data','user_data','_to_delete'}&set(Path(n).parts) for n in z.namelist())
        forbidden=('base_ptpt.jsonl','dlc_ptpt.jsonl','base_en.jsonl','ptbr_base.jsonl','ptbr_dlc_final.jsonl')
        assert not any(n.endswith(forbidden) for n in z.namelist())
archive(win,out/f'Hydro-Thunder-Windows-revisao-{version}.zip')
archive(stage,out/f'Hydro-Thunder-fontes-revisao-{version}.zip')
with zipfile.ZipFile(out/f'Hydro-Thunder-Windows-revisao-{version}.zip') as z:
    for p in winbins.iterdir():assert hashlib.sha256(z.read('data/'+p.name)).hexdigest()==sha(p)
with zipfile.ZipFile(out/f'Hydro-Thunder-fontes-revisao-{version}.zip') as z:
    for name in ('app/src/ht_version.h','AGENTS.md','CLAUDE.md','provenance.json','rexglue-modifications.patch','tools/test/test_893.ps1','tools/wsl/patch_dynamic_constants.py'):
        assert hashlib.sha256(z.read(name)).hexdigest()==sha(src/name),name
report={'revision':version,'ready_for_manual_ab':True,'normal_driven_race_fps_gain_confirmed':False,
        'real_amd_linux_gpu_tested':False,'keyboard_tested_by_assistant':False,'race_finish_and_audio_confirmed':False,
        'proofs_with_final_windows_dll':proofs,
        'windows_binaries':{p.name:sha(p) for p in winbins.iterdir()},
        'linux_binaries':{n:sha(linuxbin/n) for n in ('hydrothunder','librexruntime.so','librexgpu-xenos.so','libamd_fidelityfx_vk.so')},
        'source_patch_sha256':sha(src/'rexglue-modifications.patch'),'zip_crc':'passed'}
(validation/'artifact-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Windows and source packages verified against final binaries and source.')
