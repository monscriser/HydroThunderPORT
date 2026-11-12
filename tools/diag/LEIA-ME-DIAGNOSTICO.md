# Hydro Thunder 1.1.0 — build de DIAGNÓSTICO (Linux, PGO)

É a 1.1.0 PGO com duas diferenças:
1. **Grava tudo o que cada pista custa** e **reconhece sozinha qual pista foi aberta**. Uma rodada pelas 11 pistas basta.
2. **Já tem a correção do texto "Capturing Vulkan / F12 or Print Screen"** do Steam Deck.

Não é para distribuir: gravar tudo custa um pouco de desempenho. Os números servem para comparar as pistas e achar o que melhorar.

## Como jogar
1. Copie `HydroThunder-1.1.0-DIAG-Linux-PGO.AppImage` para o aparelho. Pode ficar ao lado do AppImage normal: usa a mesma configuração e os mesmos saves.
2. Abra o AppImage (no Modo Jogo, adicione-o à Steam como jogo não-Steam, igual ao normal). Abre o configurador de sempre:
   - para liberar as pistas: **Saves e registros → Editor de save → Créditos do save**;
   - jogue com as **suas configurações normais** de vídeo.
3. Clique em **Jogar** e corra **da pista 1 até a última da DLC, em sequência**. Para cada pista:
   - corra **a corrida inteira até a chegada**, **acelerando e virando**. A build mede sozinha alguns quadros completos aos 30 s, 55 s, 85 s e 120 s depois que a pista carrega;
   - pode reiniciar a mesma pista se quiser; isso é anotado como "reinício", não como pista nova.
4. Depois da última pista da DLC, **feche o jogo normalmente**.

Se uma pista travar ou engasgar, continue: a build guarda o que cada parte do jogo estava fazendo em todo quadro acima de 50 ms.

## O que me enviar
A pasta da sessão:
`~/.local/share/HydroThunderRecompiled/diag/<data-hora>/`

No Steam Deck: Modo Desktop → Dolphin → menu ☰ → "Mostrar arquivos ocultos" → Pasta pessoal → `.local` → `share` → `HydroThunderRecompiled` → `diag`. Compacte a pasta mais nova (botão direito → Compactar) e me mande o .zip. Ela pode ter algumas dezenas de MB.

## O que é gravado
| Arquivo | Conteúdo |
|---|---|
| `info.txt` | aparelho, sistema, driver, sua configuração |
| `tracks.csv` | pistas na ordem jogada (e reinícios) |
| `frames.csv` | tempo de **cada** quadro |
| `system.csv` | 1x/s: CPU, RAM, swap, disco, clocks, GPU ocupada, VRAM/GTT, temperaturas, watts, bateria, ventoinha |
| `threads.csv` | 1x/s: CPU de cada thread |
| `profile.csv` | **perfil de CPU por função** (amostra a cada 1 ms de CPU de cada thread ocupada, por pista) |
| `hitches.csv` | para cada quadro ≥ 50 ms: o que cada thread estava executando |
| `opens.csv` | arquivos que o jogo abriu |
| `game.log` | relatórios de GPU/CPU a cada 2 s, VRAM, **cada shader compilado com o tempo**, e os **quadros rastreados** (cada desenho com seu tempo de GPU, render targets, resolves, barreiras, shaders) |

## Para quem analisa
`python3 tools/diag/analyze_diag.py <pasta da sessão> --bins <pasta com hydrothunder e .so desta build>` → `report.md` + `summary.csv`. Por pista: FPS médio, 1%/0,1% low, percentis, engasgos, GPU ms/quadro por categoria, **GPU por etapa** (grupos de render target, desenhos mais caros com hashes de shader), **CPU por função** por thread, compilação de shaders, VRAM/GTT/RAM/swap, clocks, temperaturas, watts, diagnóstico automático e **raio-X dos piores quadros**. Os binários da build estão dentro do próprio AppImage (`--appimage-extract`, pasta `usr/bin`) e em `hydro-port\diag-110-linux`.
