# Hydro Thunder Hurricane: Recompiled — revisão 9.1.0

A 9.1.0 é a 9.0.0 com mais uma otimização de CPU: a gravação dos comandos para a placa de vídeo foi dividida em **duas threads**. A imagem do jogo continua a mesma: não houve redução de resolução, MSAA, efeitos ou precisão.

## O que mudou desde a 9.0.0

- **CPU — envio em segundo plano (`ht_async_submit`, novo):** antes, a thread que traduz os comandos do jogo também gravava tudo no driver de vídeo e enviava à GPU no fim de cada lote. Agora os lotes do meio do quadro (~3 por quadro) são entregues inteiros a uma segunda thread, que os grava e envia **na mesma ordem**, enquanto a primeira já monta o lote seguinte.
  - O lote do fim do quadro continua síncrono, porque a tela usa a imagem dele logo em seguida.
  - Antes de qualquer espera pela GPU, a primeira thread confere se a segunda já entregou tudo.
  - Se um envio falhar na segunda thread, o caso é tratado como perda da placa, para o jogo nunca ficar esperando para sempre.
- Continuam ativas as otimizações da 9.0.0: `ht_zpd_batch`, `ht_dynamic_constants` e `ht_cp_fast_registers`.

## Ganho medido (PC do Ian: RTX 5060 Ti, 1280×720 interno → 1080p, FSR 1)

Mesma DLL, cena sempre igual (barco parado no cais de Asgard), sem limite de FPS, 2 rodadas alternadas:

| | 2ª thread desligada | 2ª thread ligada |
|---|---:|---:|
| FPS | 222 / 222 | **235 / 237 (+6%)** |
| CPU da thread de comandos por quadro | 4,47 / 4,56 ms | **3,76 / 3,77 ms (−16%)** |
| GPU por quadro | 2,0 ms | 2,0 ms (igual) |

No PC, depois disso, quem limita o FPS passa a ser a lógica do próprio jogo; por isso o FPS sobe menos do que a CPU cai. No ROG Ally, a thread de comandos limitava o FPS nas cenas pesadas (8–10 ms por quadro), então o ganho deve ser maior lá. Isso **não foi medido no Ally**.

Somando com a 9.0.0 (mesma cena do cais): de ~178 FPS na 8.9.2 para ~236 FPS.

## Provas (DLL final, `validacao-9.0.0/f910-*`)

Quatro corridas de prova (escalas 1 e 2; resolve direto e resolve-para-textura em rodadas separadas), com todos os verificadores, a validação do Vulkan e a checagem de uso por duas threads ligados: **0 diferenças** e **0 erros**.
- Envio em segundo plano: cada lote é conferido (impressão digital dos comandos na entrega e depois da gravação). **0 diferenças.**
- Todos os verificadores anteriores também deram 0 diferenças, agora com a segunda thread ligada.
- Controles positivos (erro proposital só na referência): todos os verificadores acusaram o erro.
- Linux (llvmpipe, sem GPU real): tela de título e 0 diferenças.

As provas rodam lentas de propósito; esse FPS não representa o jogo normal.

## Liga/desliga

No `hydrothunder.toml`: `ht_async_submit`, `ht_cp_fast_registers`, `ht_dynamic_constants` e `ht_zpd_batch` (padrão `true`). Use `false` e reinicie o jogo para comparar.

## Pacote

- Windows: na raiz ficam só `HydroThunder.exe` (lançador), `Launcher-Config.exe` (configurador) e a pasta `data` (as notas estão dentro de `data`).
- Linux: `HydroThunder-revisao-9.1.0.AppImage`.
- Fontes: patch cumulativo do SDK + ferramentas de prova (`tools/wsl/patch_async_submit.py`).

Todos os pacotes anteriores foram preservados, e o jogo original e os saves não foram alterados. Ainda pendentes: ROG Ally com GPU AMD real, consumo em watts, chegada da corrida, áudio e teclado (este o Ian testa). Hashes em `SHA256-revisao-9.1.0.txt`.
