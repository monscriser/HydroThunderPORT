# Hydro Thunder Hurricane: Recompiled — 1.2.0 BETA (Windows e Linux, PGO)

Versão feita a partir da **telemetria por pista do ROG Ally** (11 pistas, 20 W, CachyOS Modo Jogo): cada mudança ataca um custo que apareceu nos dados. Só a variante **PGO** (a Standard deixou de ser gerada).

## Novidades
- **Corrigido: texto preto "Capturing Vulkan… F12 or Print Screen" no canto (Steam Deck / SteamOS).** Era o RenderDoc (ferramenta de captura de GPU) que o próprio jogo carregava sem querer, porque o SteamOS já vem com ele instalado. Agora o jogo só se conecta ao RenderDoc se ele já estiver aberto de propósito. Isso também desliga marcações extras de GPU que custavam desempenho.
- **Thread de áudio não gira mais à toa (Linux).** No Ally ela gastava **77% de um núcleo o tempo todo**, em todas as pistas: a espera por eventos "dormia 0 ms" no último milissegundo e repetia milhões de vezes por segundo. Agora dorme o tempo exato. Num aparelho de 20 W, em que CPU e GPU dividem a energia, isso devolve energia para o resto do jogo. (O Windows usa a espera nativa e não tinha esse problema.)
- **Menos trabalho do driver por desenho (thread de comandos da GPU).** Era o que limitava o FPS em 8 das 11 pistas no Ally (Área 51, Taça Tsunami, City, Esgotos…). O jogo pedia ao driver para escrever de novo o conjunto de texturas de **cada** desenho. Como o Hydro desenha cada cena duas vezes por quadro (as duas metades do "predicated tiling" do Xbox 360), quase todas as combinações se repetem. Agora um conjunto já escrito naquele quadro é reaproveitado (cerca de 96%).
- **Novo launcher:** menu lateral com ícones, aba **Novidades**, textos que se ajustam ao tamanho da janela (janela pequena, tela cheia, Steam Deck, ROG Ally), navegação por teclado/controle.

## Medições e provas
| O quê | Resultado |
|---|---|
| Thread de áudio, Linux (WSL/llvmpipe reproduz o giro do Ally: 76,7%) | **76,65% → 1,27%** de um núcleo; pacotes de áudio entregues iguais (~566/s) |
| Conjuntos de textura reaproveitados | **95,9%** (Linux) |
| Prova exata do cache (escreve tudo e compara cada entrada) | Linux 643.275 / Windows 1.445.835 comparações, **0 diferenças** |
| Controle positivo (erro proposital) | Linux 451.943/451.943 e Windows 5.615.749/5.615.749 acusados |
| Verificadores exatos de sempre (resolve, texturas, constantes, registradores, ZPD, envio…) | **0 diferenças** no Linux e no Windows; validação Vulkan no Windows **0 erros** |
| Windows (RTX 5060 Ti, sem limite, mesma cena, 2 rodadas) | escrita de descritores por desenho 0,33 → **0,26 µs (−21%)**; thread de comandos 3,7–3,8 → **3,5–3,6 ms/quadro (−5%)**; FPS igual (~240: neste PC o limite é a thread do jogo) |

**De onde vem o ganho esperado no Ally** (dados do próprio Ally, Área 51): a thread de comandos trabalha ~10 ms por quadro e **3,17 ms** são a escrita de descritores (no PC NVIDIA ~0,8 ms). Com 96% das escritas a menos, a estimativa é ~2 ms a menos por quadro nessa thread. **Não medido no Ally** (sem nova rodada de teste): o ganho real de FPS por pista ainda não foi confirmado.

## Para comparar
No `hydrothunder.toml`: `ht_texture_set_cache = false` (cache de descritores) e, no Linux, `ht_wait_precise_sleep = false` (espera do áudio). Reinicie o jogo.

## Não testado
- FPS no Ally / Steam Deck com esta versão.
- Nitidez das fontes do launcher em telas com zoom (150%).
