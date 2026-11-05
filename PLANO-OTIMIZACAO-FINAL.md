# Plano de otimização para a build final do Hydro Thunder (04/11/2026)

Base: 9.1.0 + B56 + launcher novo (ainda não empacotada). Regras de sempre: medir antes, mudança com liga/desliga, prova exata + controle positivo, A/B curto, e só entra o que der ganho medido. Testes com o jogo **minimizado** e controle virtual.

## Onde o custo está hoje

| Lugar | PC (RTX 5060 Ti) | ROG Ally (dados de 16/10) |
|---|---|---|
| Thread do jogo (lógica recompilada) | **É o limite** (~240 FPS sem limite) | Divide energia com a GPU |
| Thread de comandos de vídeo | ~3,5 ms/quadro, sobra | 6,8 ms; limita em cenas pesadas |
| GPU | ~1,7–2,1 ms (Qualidade) | **6,7 ms, dos quais o resolve é 3,6 ms** (no PC: 0,3) |

No Ally a 20 W, CPU e GPU disputam a mesma energia. Gastar menos CPU a 60 FPS deixa mais energia para a GPU.

## Fases (por ordem de chance de ganho)

### Fase 0: linha de base (curta)
A 60 FPS e sem limite, minimizado: CPU de cada thread (ms/s), GPU ms/quadro e FPS. É a referência de todas as comparações.

### Fase 1: CPU da thread do jogo (o limite no PC)
1. **PGO (otimização guiada por perfil) do executável.** O compilador aprende com uma corrida real quais caminhos são quentes. Não muda a lógica do jogo, só como o código é organizado. Ganho típico em código grande e cheio de desvios: 5–15%. Prova: a corrida dá o mesmo resultado (mesmo replay), e o A/B mede ms/quadro da thread do jogo.
2. **Perfil das threads a 60 FPS.** Procurar mais esperas ativas como a da 8.7 (que gastava um núcleo inteiro), relógios consultados em excesso e cópias de memória do jogo. Só corrigir o que aparecer no perfil.

### Fase 2: thread de comandos (ajuda o Ally em cenas pesadas)
Medir por dentro os dois maiores pedaços que sobraram: "prim" (~0,7 ms) e "bindings" (~0,6 ms). Cortar só o que for desperdício provado. No UFC, ideias como essa deram entre 0% e 10%; aqui a B57 já ficou de fora por não ganhar.

### Fase 3: GPU no AMD (o maior custo do Ally)
O resolve custa 3,6 ms no Ally contra 0,3 ms no PC. Isso aponta para o driver RADV (compressão/estado das imagens MSAA). As ideias prontas são o kit de flags RADV, o layout GENERAL só no AMD e a cópia por compute. **Sem medir no Ally não há como provar ganho**: precisa de uma rodada de kit lá (~10 minutos).

### Fase 4: build final
- Refazer **todas** as provas exatas com a DLL final.
- Windows e Linux.
- Pacotes no formato de sempre (raiz: HydroThunder.exe, Launcher-Config.exe, data/), com a fonte Roboto e a licença.
- Notas, texto do GitHub e fontes no GitHub.

## O que não vale tentar de novo (já medido)
FIFO, aniso, dither, fsi, escalas 2x1, textureGrad no NVIDIA, cache de descritores de textura, pular transferências de leque, A1, A15, A3 e B57 (não deram ganho ou não têm caso no Hydro). Reduzir MSAA ou escala está fora: perde fidelidade.
