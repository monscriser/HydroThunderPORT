# Resultados de GPU — revisão 8.4 (16/10/2026)

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

## Sem limite de FPS

| Pasta | Configuração | FPS médio | FPS mín. | W |
|---|---|---|---|---|
| 25-sem-limite-nativa-como-83 | Nativa, otimizações novas desligadas | 136,3 | 124 | 138 |
| 24-sem-limite-nativa-84 | Nativa 8.4 | **179,4** | 153 | 143 |
| 03-sem-limite-qualidade | Qualidade 8.2 | 207,6 | 153 | 85 |
| 23-sem-limite-qualidade-84 | Qualidade 8.4 (limite = CPU) | 203,4 | 126 | 72 |

## Teste rápido no menu (gpu_menu.ps1, mesmo programa, cvar desligando/ligando)

| | Nativa total | Qualidade total |
|---|---|---|
| ht_texture_direct=false | 7,20 | 2,96 |
| ht_texture_direct=true | 6,46 | 2,65 |
| + ht_clear_transfer_cutout | 5,17 | — |

## Provas de imagem

- Texturas: modo `ht_texture_direct_verify=true` — 151 bilhões de pixels, 0 diferentes (`19-nativa-direct-prova/game.log`); Linux llvmpipe 4,2 bilhões, 0 diferentes.
- Limpezas: modo `ht_clear_cutout_sentinel=true` — imagem limpa (`comparacao-cutout/img-*-sentinela`, `20-nativa-sentinela-corrida`); controle positivo com lixo visível (`comparacao-cutout/sentinela-controle-positivo`).
- SSIM das fotos paradas: `comparacao-cutout/metrics-still-*.json` (dentro do ruído entre rodadas; rodada sem mudança chegou a 0,67).
