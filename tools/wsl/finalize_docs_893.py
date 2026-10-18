from pathlib import Path
import hashlib,json,re,shutil
from datetime import datetime
task=Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane')
src=Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7')
out=task/'outputs'; val=out/'validacao-8.9.3'
backup=src/('_to_delete/antes-documentacao-final-893-'+datetime.now().strftime('%Y%m%d-%H%M%S'));backup.mkdir(exist_ok=False)
for name in ('AGENTS.md','CLAUDE.md','COMO-CONTINUAR.md','PLANO-9.0.0.md','provenance.json'):shutil.copy2(src/name,backup/name)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
dll=sha(task/'work/final-windows/rexgpu-xenos.dll')
exe=sha(task/'work/final-windows/hydrothunder.exe')
def last(run,tag):
    rows=[l for l in (val/run/'game.log').read_text(errors='replace').splitlines() if f'[{tag}]' in l and '=' in l]
    assert rows,(run,tag)
    return dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)\b',rows[-1]))
normal=('proof-scale1','proof-scale2','rtt-proof-scale1','rtt-proof-scale2')
z=[last(r,'ht-zpd-pair-verify') for r in normal];c=[last(r,'ht-dynamic-constants') for r in normal]
assert all(x['mismatches']==0 and x['nonzero']>0 for x in z)
assert all(x['mismatches']==0 and x['compared']>0 for x in c)
section=f'''
## 23. Revisão 8.9.3 preview — otimizações prontas para A/B manual (16/10/2026) — CONTINUAR DAQUI

Ian preferiu testar o kit depois. A revisão permanece **8.9.3 preview**, sem alegar ganho comprovado de FPS em navegação. Não aguardar diagnóstico Ally que ele não consegue enviar; ele testará Linux/Ally depois. Não iniciar UFC.

- **Final:** exe SHA256 `{exe}`, GPU DLL `{dll}`. Stamp revision-8.9.3-preview. SDK cumulativo regenerado, conferido por reverse-check; command_processor.h/cpp e texture_cache.cpp mudaram em relação ao patch 8.9.2. O terceiro arquivo corrige flags TRANSFER_SRC/DST do buffer escalado usadas pelo verificador de resolve direto, antes inválido na escala2. Primeiras provas e DLL85efe... foram invalidadas e guardadas em outputs/_to_delete; não transferir seus resultados. Preservados exact_key=true, flush=false, qualidade e correções anteriores. Padrões ht_zpd_batch=true e ht_dynamic_constants=true; verificadores e controles positivos false. Sem ht_bind_ticks.
- **Provas com a DLL FINAL:** quatro rodadas normais, escalas 1 e 2, com resolve direto e resolve para textura em rodadas distintas. Soma {sum(x['compared'] for x in z):,} pares ZPD, {sum(x['nonzero'] for x in z):,} não-zero; {sum(x['compared'] for x in c):,} vínculos de constantes; zero diferenças em todos. Texturas, fconst, resolve direto, RequestRange sem diferenças. RTT verificado com trabalho não nulo. Controles positivos separados detectaram os erros propositais. Escala2 confirmada 2560x1440 no log.
- **Linux:** binários normalizados com rpath ORIGIN antes do smoke; mesmos arquivos usados na AppImage, cuja extração deve conferir byte a byte. llvmpipe/Xvfb por 120s renderizou título, >1 milhão de vínculos e provas de texturas/fconst/resolve sem diferenças. Não é GPU AMD real, corrida ou benchmark Ally.
- **Limite do A/B:** roteiro automático continua preso no cais. Resultados medem custo nessa cena, não uma corrida dirigida representativa. Não anunciar +15% nem promessa de FPS/watts/CPU total. Ver tabela e escopo em NOTAS-revisao-8.9.3-preview.md e summary.json. Não usar FPS das provas pesadas.
- **Entregáveis:** tarefa atual outputs/Hydro-Thunder-Windows-revisao-8.9.3-preview.zip, HydroThunder-revisao-8.9.3-preview.AppImage, Hydro-Thunder-fontes-revisao-8.9.3-preview.zip, SHA256-revisao-8.9.3-preview.txt e notas. Validação em outputs/validacao-8.9.3/artifact-validation.json. Todos pacotes anteriores preservados. Jogo original/saves originais intocados; wrappers restauram game-test após cada rodada.
- **Próximo obrigatório:** Ian roda outputs/kit-A-B-local/MEDIR-A-B.cmd quando puder e dirige Asgard >=60s em cada rodada, mesmo barco/condições. A desliga as duas, B liga; mesmos três binários finais, 720p + FSR1, FG off, sem limite. Kit salva/repõe exe+runtime+GPU em game-test, configurações próprias e logs locais resultados-*/config-A/logs e config-B/logs. Nenhum controle automático/teclado injetado pelo kit. Colher resultados deste PC antes de decidir 9.0.0; depois validar Ally Linux. Chegada, áudio, teclado e outra pista ainda dependem de Ian.
- **Retomada técnica:** detalhes da implementação e limites das provas na seção 22, ferramentas no tools/test e tools/wsl. Scripts históricos patch_* partem do SDK 8.9.2; NÃO reaplicar sobre o patch cumulativo atual. NÃO executar rebuild_892.sh, que restaura SDK antigo. Para recompilar 8.9.3, usar setup/patch cumulativo e escolher novo HT_893_BUILD_OUT no rebuild_893.sh; builds não reprodutíveis exigem repetir provas com o novo binário final.
'''.replace('36,736','36.736').replace('13,060','13.060').replace('21,974,015','21.974.015')
agents=(src/'AGENTS.md').read_text()
handoff_anchor='## 23. Retomada salva para outro agente'
if handoff_anchor in agents:
    agents=agents.split(handoff_anchor,1)[0].rstrip()+'\n'
assert '## 23.' not in agents
agents=agents.replace('## 22. Retomada 9.0.0 — duas otimizações candidatas, validação de FPS em andamento (16/10/2026) — CONTINUAR DAQUI','## 22. Retomada 9.0.0 — duas otimizações candidatas (16/10/2026) — histórico; continuar na seção 23')
for name in ('AGENTS.md','CLAUDE.md'):(src/name).write_text(agents+section,encoding='utf8')
como=src/'COMO-CONTINUAR.md'
como.write_text('''# Retomada atual: revisão 8.9.3 preview

Leia a seção 23 de AGENTS.md/CLAUDE.md. A prévia contém ZPD em passadas e constantes dinâmicas, com provas repetidas nos binários finais. Ainda não chamar de 9.0.0: falta o A/B com Ian pilotando. Ele escolheu testar depois.

Kit nesta tarefa: outputs/kit-A-B-local/MEDIR-A-B.cmd. Não executar carga pesada enquanto Ian estiver medindo. Os resultados ficam localmente; depois analisar CPU/GPU/FPS e decidir a versão. Ally Linux é a etapa posterior. Jogo original e saves originais preservados.

Patch principal e fontes agora incluem as mudanças. NÃO executar rebuild_892.sh; ele restaura o SDK anterior. Qualquer nova DLL exige novas provas. O texto abaixo é histórico da retomada anterior.

---
'''+como.read_text(),encoding='utf8')
plano=src/'PLANO-9.0.0.md'
plano.write_text('''> Estado 16/10, 8.9.3 preview: item1 implementado e validado nas provas finais; otimização CPU de constantes também pronta. Ian deixou A/B dirigido para depois. O kit usa os binários finais da prévia. Não atribuir9.0.0 sem ganho real de CPU/GPU e FPS confirmado; dados Ally ficam para teste posterior. Ver seção 23.

'''+plano.read_text(),encoding='utf8')
p=src/'provenance.json';data=json.loads(p.read_text())
data['localPreview']='revision-8.9.3-preview'
data['buildStatus']='8.9.3 preview: batched ZPD and dynamic constant descriptors; final Windows exact proofs in scales1/2, positive controls and Linux llvmpipe smoke passed. Representative driven-race FPS, total CPU/power and real AMD Linux/Ally testing pending. Previous releases preserved.'
data['validation893']={'windowsExeSHA256':exe,'windowsGpuDLLSHA256':dll,'zpdPairs':sum(x['compared'] for x in z),'zpdNonzeroPairs':sum(x['nonzero'] for x in z),'constantBindings':sum(x['compared'] for x in c),'mismatches':0,'representativeDrivenRaceFPSGainConfirmed':False,'realAMDLinuxGPU':False}
p.write_text(json.dumps(data,indent=2)+'\n')
notes=(task/'work/NOTAS-893-rascunho.md').read_text()
notes+='\n## Contadores finais e custo local\n\n'
notes+=f'Nas quatro provas Windows: **{sum(x["compared"] for x in z):,} pares de oclusão**, dos quais {sum(x["nonzero"] for x in z):,} com amostras visíveis, e **{sum(x["compared"] for x in c):,} vínculos de constantes**: zero diferenças. Controles positivos detectaram os erros. DLL final SHA256: `{dll}`.\n\n'
summary=json.loads((val/'summary.json').read_text())
rows=[r for r in summary if r['run'].startswith('ab-scale1-')]
notes+='A/B da cena automática presa no cais, sem verificadores; números não representam ganho durante navegação:\n\n| Rodada | FPS | GPU ms/quadro | CPU comandos ms/quadro | Desenhos/quadro | Vínculos CPU ms/quadro |\n|---|---:|---:|---:|---:|---:|\n'
for r in rows:notes+=f'| {r["run"]} | {r["fps"]} | {r["gpu_ms"]} | {r["cpu_command_ms"]} | {r["draws"]} | {r["cpu_bindings_ms"]} |\n'
notes+='\nAs trajetórias e quantidades de trabalho variam. Essa tabela não confirma economia total de CPU nem redução de watts. Logs com os verificadores ligados ficam fora da medição de custo.\n'
notes+='\nComparação adicional limitada a 60 FPS, sem capturas durante o trecho medido:\n\n| Rodada | FPS médio dos registros | GPU ms/quadro | CPU comandos ms/quadro | Pior quadro registrado ms |\n|---|---:|---:|---:|---:|\n'
for r in summary:
    if r['run'].startswith('cap-ab-scale1-'):
        notes+=f'| {r["run"]} | {r["fps"]} | {r["gpu_ms"]} | {r["cpu_command_ms"]} | {r["worst_logged_frame_ms"]} |\n'
notes+='\nUma rodada anterior da primeira DLL, com capturas, registrou 59,8 FPS de média e um pico isolado de108,7 ms. A causa não foi determinada; o pico permanece documentado em _to_delete/validacao-8.9.3-primeira-DLL/stable-scale1/result.json. Um máximo isolado não demonstra regressão nem redução de engasgos.\n'
notes+='\nFoi corrigida a declaração de uso do buffer de resolve escalado: o verificador antigo faz cópias e precisava das flags de origem/destino de transferência nas alocações esparsas e comuns. Não foram acrescentadas cópias ao jogo normal. As primeiras provas em escala2, embora com comparações iguais, foram rejeitadas por erro de validação Vulkan; as provas finais foram repetidas após a correção. Regra da API: [documentação Vulkan de vkCmdCopyBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html).\n'
notes=notes.replace('36,736','36.736').replace('13,060','13.060').replace('21,974,015','21.974.015').replace('de108,7','de 108,7')
(out/'NOTAS-revisao-8.9.3-preview.md').write_text(notes,encoding='utf8')
shutil.copy2(out/'NOTAS-revisao-8.9.3-preview.md',src/'NOTAS-revisao-8.9.3-preview.md')
print('Final source documents and release notes updated.')
