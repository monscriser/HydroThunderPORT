# Resultados de GPU — revisão 8.6 (16/10/2026)

PC do Ian: NVIDIA RTX 5060 Ti, 1080p, pista Asgard, corrida de 55 s acelerando e virando, média do s20 ao s55.
Medida principal: tempo de GPU por quadro (`[ht-perf] total=`), média de `perf_breakdown.ps1`.

## 60 FPS travados (ms de GPU por quadro)

| Pasta | Configuração | total | transfers | resolve | texload | W |
|---|---|---|---|---|---|---|
| 15-nativa-final | 8.3 Nativa | 7,11 | 1,74 | 0,97 | 0,55 (+0,92 texcopy) | 80,5 |
| 17-nativa-direct | + texturas diretas | 6,20 | 1,72 | 0,97 | 0,77 | 73,4 |
| 21-nativa-cutout | 8.4 Nativa | **4,90** | 0,40 | 0,97 | 0,77 | 64,6 |
| 16-qualidade-final | 8.3 Qualidade | 2,86 | 0,55 | 0,29 | 0,21 (+0,33) | — |
| 18-qualidade-direct | + texturas diretas | 2,57 | 0,54 | 0,27 | 0,31 | 35,3 |
| 22-qualidade-cutout | 8.4 Qualidade | **2,24** | 0,15 | 0,28 | 0,32 | 32,7 |
| 28-nativa-dres | 8.5 Nativa | **4,69** | 0,36 | 0,61 | 0,75 | 61,0 |
| 29-qualidade-dres | 8.5 Qualidade | **2,14** | 0,14 | 0,19 | 0,31 | 33,5 |
| 30-nativa-dres-desligado | mesmo programa, resolve direto desligado | 5,02 | 0,40 | 0,96 | 0,77 | — |
| 31-nativa-dres-ligado | mesmo programa, ligado (corrida seguinte) | **4,67** | 0,36 | 0,61 | 0,75 | — |
| 35-nativa-rtt-desligado | 8.6, resolve na textura desligado | 4,72 | 0,36 | 0,61 | 0,75 | 58,5 |
| 36-nativa-rtt-ligado | 8.6 Nativa (corrida seguinte) | **4,21** | 0,36 | 0,99 | 0,00 | 56,9 |
| 37-qualidade-rtt-desligado | 8.6, resolve na textura desligado | 2,13 | 0,14 | 0,19 | 0,31 | 33,5 |
| 38-qualidade-rtt-ligado | 8.6 Qualidade (corrida seguinte) | **2,00** | 0,14 | 0,38 | 0,00 | 32,4 |

## Sem limite de FPS

| Pasta | Configuração | FPS médio | FPS mín. | W |
|---|---|---|---|---|
| 25-sem-limite-nativa-como-83 | Nativa, otimizações novas desligadas | 136,3 | 124 | 138 |
| 24-sem-limite-nativa-84 | Nativa 8.4 | 179,4 | 153 | 143 |
| 32-sem-limite-nativa-85 | Nativa 8.5 | 190,8 | 156 | 140 |
| 39-sem-limite-nativa-86 | Nativa 8.6 (perto do limite da CPU) | **193,0** | 152 | 134 |
| 03-sem-limite-qualidade | Qualidade 8.2 | 207,6 | 153 | 85 |
| 23-sem-limite-qualidade-84 | Qualidade 8.4 (limite = CPU) | 203,4 | 126 | 72 |

## Teste rápido no menu (gpu_menu.ps1, mesmo programa, cvar desligando/ligando)

| | Nativa total | Qualidade total |
|---|---|---|
| ht_texture_direct=false | 7,20 | 2,96 |
| ht_texture_direct=true | 6,46 | 2,65 |
| + ht_clear_transfer_cutout | 5,17 | — |

## Menu, 8.6 (gpu_menu.ps1, mesmo programa)

| | Nativa total | resolve | texload | recargas/quadro |
|---|---|---|---|---|
| ht_resolve_to_texture=false | 4,77 | 0,61 | 0,73 | 18 |
| ht_resolve_to_texture=true | 4,40 | 0,97 | 0,00 | 1 |

## Resolve: onde estava o tempo (menu Nativa, medição temporária)

Dos 1,0 ms de resolve, 0,69 ms eram o "dump" (render target inteiro, todas as amostras, para o buffer EDRAM, ~250 MB/quadro) e ~0,3 ms a cópia. O resolve direto elimina o dump: menu 5,15 → 4,77 ms.

## Provas de imagem

- Texturas: modo `ht_texture_direct_verify=true` — 151 bilhões de pixels, 0 diferentes (`19-nativa-direct-prova/game.log`); Linux llvmpipe 4,2 bilhões, 0 diferentes.
- Limpezas: modo `ht_clear_cutout_sentinel=true` — imagem limpa (`comparacao-cutout/img-*-sentinela`, `20-nativa-sentinela-corrida`); controle positivo com lixo visível (`comparacao-cutout/sentinela-controle-positivo`).
- SSIM das fotos paradas: `comparacao-cutout/metrics-still-*.json` (dentro do ruído entre rodadas; rodada sem mudança chegou a 0,67).
- Resolve direto: modo `ht_direct_resolve_verify=true` — Nativa 151,7 bi valores, Qualidade 82,9 bi, 0 diferentes (`26-*`, `27-*`); controle positivo `ht_direct_resolve_verify_break=true` = bilhões de diferenças; Linux llvmpipe 4,2 bi, 0 diferentes.
- Resolve na textura (8.6): modo `ht_resolve_texture_verify=true` (com `ht_texture_direct_verify=true`) — depois de cada resolve roda a recarga antiga e compara pixel a pixel com a textura gravada pelo resolve. Corrida Nativa 200,3 bi pixels / 76.755 texturas, 0 diferentes (`34-nativa-rtt-prova`); menu Nativa 44,9 bi e Qualidade 21,4 bi, 0 diferentes (`33-menu-rtt-provas`); controle positivo `ht_resolve_texture_verify_break=true` (1 pixel de deslocamento) = 2,2 bi diferenças; Linux llvmpipe 4,7 bi, 0 diferentes (`linux-llvmpipe-86`).
