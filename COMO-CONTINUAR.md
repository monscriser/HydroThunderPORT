# Retomada atual — revisão 9.1.0 empacotada (16/10/2026)

Leia a seção 25 de CLAUDE.md/AGENTS.md (CONTINUAR DAQUI). A 9.1.0 (9.0.0 + envio em segundo plano) está empacotada em
`C:/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane/outputs`
(Windows, AppImage, fontes, SHA256, NOTAS-revisao-9.1.0.md, provas em validacao-9.0.0/f910-*).
Próximo passo: Ian roda `outputs/kit-ally-9.0.0` no ROG Ally (Linux) e manda `~/hydro-medicao`;
otimizar conforme o gargalo medido lá. Não recomeçar e não iniciar UFC. O texto abaixo é histórico.

---
# Retomada atual — testes concluídos, passagem para outro agente

Leia a seção 23 e o documento completo da sessão: `C:/Users/Monscriser/Documents/Codex/2026-10-16/continue-o-projeto-hydro-thunder-hurricane/outputs/CONTINUACAO-PROJETO-HYDRO.md`. Nenhuma release nova foi empacotada. A candidata e suas provas estão preservadas; a próxima confirmação é o A/B dirigido por Ian, depois Linux/Ally. Não recomeçar e não iniciar UFC. O texto abaixo é histórico.

---
# Retomada atual — 16/10/2026, experiências ZPD + constantes dinâmicas

A release preservada continua sendo a **8.9.2 preview**. Leia PLANO-9.0.0.md e as seções 19–22 de AGENTS.md/CLAUDE.md; **a seção 22 é o estado intermediário mais recente**.

Ian atualizou o pedido: otimizar GPU E CPU e FPS no PC NVIDIA atual, pois não consegue enviar os dados do Ally agora. Depois ele valida o Ally com Linux. Não aguardar o Ally, não iniciar UFC.

Duas candidatas foram implementadas e passaram por provas exatas específicas e controles positivos. Ainda falta confirmar FPS numa corrida dirigida: o roteiro automático bate no cais. O kit local está pronto em:
C:\Users\Monscriser\Documents\Codex\2026-10-16\continue-o-projeto-hydro-thunder-hurricane\outputs\kit-A-B-local\MEDIR-A-B.cmd

Foi perguntado se Ian consegue fazer as duas rodadas agora. Enquanto ele mede, não executar compilação ou outro benchmark. Os logs ficam aqui no PC; não precisa pedir envio de arquivos.

Ainda não há novo pacote/release; o patch cumulativo principal não foi regenerado. Scripts de experimento estão nos fontes e o SDK WSL está com a candidata. Não executar rebuild_892.sh para continuar a candidata (ele restaura o caminho 8.9.2). Após definir versão/defaults, refazer provas na DLL final e preservar todos os pacotes anteriores.

## Histórico preservado da retomada anterior
# Retomada do Hydro Thunder Hurricane — 16/10/2026 (revisão 8.9.2 preview → meta 9.0.0)

Estado: **8.9.2 preview** compilada, provada e empacotada (Windows, AppImage, fontes). Pacotes anteriores intactos. Próximo objetivo: **revisão 9.0.0 com salto de FPS no ROG Ally**, seguindo `PLANO-9.0.0.md`. Não iniciar o UFC.

## Mensagem para colar no próximo agente

```text
Continue o projeto Hydro Thunder Hurricane: Recompiled na pasta:
C:\Users\Monscriser\Documents\Codex\2026-10-13\usan\work\Hydro-Thunder-fontes-revisao-7

Sou leigo; explique em português simples. Leia nesta ordem: COMO-CONTINUAR.md, PLANO-9.0.0.md,
e CLAUDE.md/AGENTS.md (seções 19 a 21; a 21 é o ponto de retomada). Use a skill
$rexglue-port-optimization se existir. Não recomece o projeto nem inicie o UFC.

Estado: revisão 8.9.2 preview pronta em
C:\Users\Monscriser\Documents\Codex\2026-10-16\as-instru-es-para-outro-agente\outputs
(Windows zip, AppImage, fontes, SHA256, NOTAS-revisao-8.9.2-preview.md). Preserve todos os pacotes.
A 8.9.2 corrigiu as luzes atravessando paredes (ht_zpd_exact_key) e reduziu envios à GPU
(ht_zpd_flush desligado), com provas exatas 0 diferenças e 60 FPS estáveis.

Objetivo agora: revisão 9.0.0 com salto de FPS no ROG Ally Z1E (hoje ~120 FPS em 720p + FSR 1),
sem perder fidelidade nem estabilidade. Siga o PLANO-9.0.0.md:
- Item 0: eu rodo o kit outputs\kit-diagnostico-gpu-ally no Ally e te mando a pasta
  hydro-diagnostico-gpu; analise o tempo por passada no AMD.
- Item 1 (pode começar já): oclusão (ZPD) sem fechar a render pass a cada evento
  (~25-30 das ~60 render passes por quadro em Asgard).
- Itens 2 a 5 conforme as medições.

Regras: testes de teclado eu faço. Avise antes de ocupar minha tela. Teste só na cópia game-test
(com meu save antigo já copiado: o roteiro cai em Asgard). Não altere o jogo original nem os saves.
Guarde em _to_delete antes de substituir. Cada mudança: cvar liga/desliga, prova exata +
controle positivo, A/B na corrida e custo em FPS/ms; se uma correção custar desempenho,
otimize o custo antes de entregar. Builds não são reprodutíveis: refaça as provas exatas com a
DLL final. Não repita o que já foi descartado (seção 14 e PLANO). Só chame de 9.0.0 se o ganho
medido no Ally for real; senão publique como 8.9.x e me diga.
```

## Arquivos principais

Saída: `C:\Users\Monscriser\Documents\Codex\2026-10-16\as-instru-es-para-outro-agente\outputs`
- `Hydro-Thunder-Windows-revisao-8.9.2-preview.zip`, `HydroThunder-revisao-8.9.2-preview.AppImage`, `Hydro-Thunder-fontes-revisao-8.9.2-preview.zip`, `SHA256-revisao-8.9.2-preview.txt`, `NOTAS-revisao-8.9.2-preview.md`
- `kit-diagnostico-gpu-ally/diagnostico-gpu/` (para o Ian rodar no Ally)
- Provas: `otimizacao-8.9/` (90–98, linux-llvmpipe-892, build-892), `provas-controles-8.9/`, `provas-saida-8.9/`
- `TESTAR-8.9.cmd` abre a cópia game-test (agora 8.9.2)

## Ambientes
- Jogo original (não alterar): `C:\Users\Monscriser\Downloads\hydrothunder\HYDRA JOGO\JOGO BASE`
- Cópia de teste: `C:\Users\Monscriser\Documents\Codex\2026-10-13\usan\outputs\_teste_modern\game-test` (exe/DLL 8.9.2; save antigo do Ian copiado; anteriores em `_to_delete`)
- WSL Ubuntu-24.04 `/root/ht`. SDK 8.9.2 = originais em `/root/sdk-backup-reqrange` (+ `patch_request_range_fast.py`) e `/root/sdk-backup-zpdkey` (+ `patch_zpd_exact_key.py`); script `tools/wsl/rebuild_892.sh` refaz tudo.
