# Análise por pista — ROG Ally Z1 Extreme, 20 W (na tomada), build 1.1.0-DIAG (11/11/2026)

Sessão do Ian: CachyOS (deckify, kernel 7.3.3), Modo Jogo (gamescope), RADV, 1280×720 saída, FSR 1, qualidade 0, limite 120 FPS, 20 W. 11 pistas em sequência, ~34 min.
Dados: `E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\hydro-port\diag-ally-20w\` (zip extraído, `full-game.log` = 5 partes do log juntas). Relatório completo gerado: `diag\20261111-212142\report.md` + `summary.csv` (`tools/wsl/diag/analyze_ally.sh`). 7,58 milhões de amostras de CPU, 0 perdidas; 4 quadros rastreados por pista base.

## Resultado por pista (trecho estável, sem os 20 s iniciais)

| Pista | FPS médio | 1% low | GPU ms/quadro | resolve ms | GPU ocupada | Thread de comandos | Thread do jogo | Desenhos/quadro |
|---|---|---|---|---|---|---|---|---|
| Área 51 | 94,4 | 44,3 | 7,22 | 3,93 | 77% | **98%** | 82% | 2.868 |
| Taça Tsunami (Stadium) | 92,6 | 48,8 | 7,33 | 3,83 | 77% | **102%** | 61% | 3.335 |
| City | 96,3 | 50,0 | 6,69 | 3,69 | 75% | **97%** | 75% | 2.754 |
| Esgotos de Paris | 100,2 | 53,9 | 6,99 | 3,63 | 79% | **103%** | 72% | 2.669 |
| Babilônia Perdida | 103,7 | 54,8 | 7,21 | 3,97 | 85% | 89% | **92%** | 2.217 |
| Ilha dos Monstros | 97,3 | 54,8 | 7,25 | 3,67 | 80% | **104%** | 81% | 2.838 |
| DLC Pista 2 | 92,9 | 55,8 | 6,79 | 3,59 | 73% | **99%** | 69% | 3.099 |
| DLC Pista 3 | 97,9 | 57,1 | 6,66 | 3,38 | 76% | **98%** | 89% | 2.538 |
| Hydroelectric | 106,5 | 59,2 | 6,57 | 3,57 | 81% | 91% | **94%** | 2.002 |
| Invasão de Asgard | 117,9 | 70,6 | 6,56 | 4,16 | 88% | 76% | 82% | 1.360 |
| DLC Pista 1 | 112,4 | 73,3 | 5,89 | 3,94 | 76% | 57% | **93%** | 1.118 |

Em todas: 20,0 W, GPU ~1,3–1,47 GHz, CPU ~2,6–2,8 GHz, 65 °C GPU / 68–70 °C CPU, VRAM ~1 GB de 6,5 GB, RAM do jogo ~1,3 GB, sem swap. Engasgos >33 ms: 0,4–1,6 por minuto (baixo). Só 40% dos quadros chegam no limite de 120; 13% passam de 12,5 ms.

## O que limita (em ordem de impacto)

### 1. Thread de áudio girando à toa — 77% de um núcleo em TODAS as pistas
`Audio Worker` gasta ~0,77 núcleo o tempo todo: `pthread_mutex_trylock` 28%, `clock_gettime` 20%, `PosixConditionBase::WaitMultiple` 16%. Causa no SDK (`src/core/threading_posix.cpp`, `WaitMultiple`): espera por vários eventos "perguntando" com `trylock`; quando algum travamento está ocupado, faz `yield` e repete **sem dormir** → giro contínuo. Num aparelho de 20 W em que CPU e GPU dividem a energia, isso rouba clock dos outros. **Correção:** espera de verdade (dormir em vez de girar) mantendo a mesma latência do áudio. Risco baixo; prova = CPU da thread de áudio cai e áudio continua normal.

> **Correção (mesmo dia):** a coluna "Thread de comandos" da tabela somava duas threads com o mesmo nome curto ("GPU Commands (F"): a de comandos (#8087) e a de envio assíncrono (#8088, ~7–12%). Separadas: a de comandos fica em **86–91%** nas pistas pesadas (Área 51 86%, Stadium 91%, Sewers 91%), a de envio em ~12%. O analisador agora agrupa por `nome#tid`. `[ht-draw]` do próprio Ally na Área 51: IssueDraw **7,5 ms/quadro**, dos quais **bindings 3,17 ms** (PC NVIDIA ~0,9), prim 1,17, rt 0,80, textures 0,52, vb 0,49; a thread de comandos trabalha 10,0 de 11,2 ms por quadro (0,8 ms esperando o jogo).

### 2. Thread de comandos da GPU perto do limite (86–91%) nas pistas pesadas — é o que segura o FPS
Perfil da `GPU Commands`: **~20% dentro do driver AMD, no `vkUpdateDescriptorSets`** (chamado por `VulkanCommandProcessor::UpdateBindings`, identificado pelo endereço de retorno), `IssueDraw` 7,5%, `copy_and_swap_32_unaligned` 5,2%, `UpdateBindings` 3,5%, `EnableAccessCallbacks` 2,6% (+ tempo de kernel do mprotect, que não aparece na amostra). No PC NVIDIA esse custo era pequeno (o reaproveitamento de descritores de textura de 8.6 foi descartado por não ganhar lá: 93% reaproveitáveis), mas **no RADV é exatamente onde a thread perde tempo**. Candidatos: (a) reaproveitar o conjunto de descritores de texturas quando idêntico ao anterior (código existe: `patch_reuse_texture_sets.py`); (b) `vkUpdateDescriptorSetWithTemplate` (mais barato no RADV); (c) menos cópias com troca de bytes por desenho. Pistas pesadas (Stadium, Área 51, City) têm 2.700–3.300 desenhos/quadro, e o quadro rastreado mais pesado teve 6.719 desenhos.

### 3. Thread do jogo (lógica recompilada) ~90% em Hydroelectric, Babilônia, DLC 1
Tempo espalhado em funções do jogo (`sub_8218B350` 7,6%, `sub_823A7AD8` 5,8%, `sub_821D4230` 5,1%…), sem espera ativa óbvia; já tem PGO. Alvo difícil — só depois dos itens 1 e 2.

### 4. GPU: resolve = 51–67% do tempo de GPU (3,4–4,2 ms; NVIDIA 0,3 ms)
GPU ocupada 73–88%: hoje não é o limite principal, mas vira assim que a CPU for aliviada. Quadros rastreados (Área 51): mapa de sombra 1024 = 23–31% do quadro; 2 metades da cena 4× MSAA ~10% cada; "entre desenhos" (resolves/transferências/barreiras) 2,2–4,2 ms por quadro. Ideias (exigem A/B no Ally): layout/compressão das imagens de RT no RADV.

### 5. Shaders novos na primeira vez da DLC
24–60 pipelines por pista DLC, 50–194 ms cada, compilados em segundo plano; os engasgos ficaram baixos (0,4–0,6/min) e o cache guarda para a próxima vez. Prioridade baixa. (No início, 1.309 pipelines foram aquecidos na tela de abertura.)

## Limites desta coleta
- As 3 pistas da DLC não foram reconhecidas pela build (o jogo abre `Xbox360Pack01TrackNN.fsb`, sem barra). Separadas depois pelo horário: FPS, tempos de quadro, sistema, threads, engasgos e relatórios de 2 s estão certos; **perfil de CPU por função e quadros rastreados delas ficaram somados à Área 51**. Corrigido em `app/src/ht_diag.h` para a próxima build.
- Na tomada a 20 W: na bateria o comportamento de energia pode mudar.
- "bateria 0 W" = ligado na tomada.

## O que entrou na 1.2.0 (feito sem nova rodada de teste, a pedido do Ian)
- Item 1: `tools/wsl/patch_waitmultiple_sleep.py` — causa exata medida (`patch_measure_waitmultiple.py`): `Audio Worker` espera 9 eventos com prazo de 1 ms e o sono `duration_cast<milliseconds>` arredondava para 0 → giro. WSL: 76,65% → 1,27%.
- Item 2: `tools/wsl/patch_texture_set_cache.py` — cache por quadro dos conjuntos de textura: 95,9% reaproveitados; verificador 0 diferenças, controle positivo acusa tudo.
- Investigado e descartado: trocas de MXCSR na thread do jogo (só 2,3% das amostras logo após `ldmxcsr`); `EnableAccessCallbacks` (2,8%, loop do vigia de memória, risco alto); `copy_and_swap_32_unaligned` (já SSSE3, é cópia real dos registradores do ring).

## Plano proposto (original)
1. Corrigir a espera ativa do áudio (SDK) — prova com a build diag: CPU do `Audio Worker` e clock da CPU/GPU, FPS por pista.
2. Reduzir `vkUpdateDescriptorSets` na thread de comandos (reaproveitar descritores idênticos / templates) com verificador exato + controle positivo.
3. Nova build diag com 1 + 2 → Ian roda **só 2–3 pistas** (ex.: Área 51, Stadium, Asgard) para o A/B contra esta sessão.
