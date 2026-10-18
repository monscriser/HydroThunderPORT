# Hydro Thunder Hurricane — revisão 7

Foco desta revisão: **FSR 3.1 funcionando**, **qualidade da imagem que acompanha a resolução** escolhida no configurador e **anti-serrilhado** no perfil Qualidade.

## Como abrir
- **Windows:** `Launcher-Config.exe` abre o configurador; `HydroThunder.exe` abre o jogo direto. Mantenha os dois e a pasta `data` juntos.
- **Linux (AppImage):** `HydroThunder-revisao-7.AppImage`. Marque como executável (Propriedades → Permissões, ou `chmod +x`). Precisa de FUSE (CachyOS/Arch: `fuse2` ou `fuse3`).
- **Linux (portátil, sem FUSE):** extraia `Hydro-Thunder-Linux-revisao-7-portatil.tar.gz` e rode `Jogar.sh`. `Criar-atalho-no-menu.sh` coloca o jogo no menu.

A pasta do jogo e os saves já registrados continuam valendo.

## O que mudou

**Nova opção "Qualidade da imagem"** (aba Vídeo, abaixo de "Resolução de saída")
O jogo de Xbox 360 só consegue desenhar em múltiplos inteiros de 720p (1x, 2x, 3x). A opção escolhe o múltiplo a partir da resolução de saída, e o configurador mostra o tamanho real logo abaixo:

| Saída | Nativa | Qualidade | Equilibrado | Desempenho | Ultra desempenho |
|---|---|---|---|---|---|
| 720p | 1x | 1x | 1x | 1x | 1x |
| 1080p | 2x (reduz p/ 1080p) | 1x + ampliação | 1x | 1x | 1x |
| 1440p | 2x | 1x + ampliação | 1x | 1x | 1x |
| 4K | 3x | 2x + ampliação | 2x | 1x | 1x |

**Ampliação da imagem: FSR 1 (recomendado), FSR 3.1 ou Bilinear**
- O FSR 3.1 agora funciona sem fechar o jogo (corrigido o travamento na biblioteca AMD).
- Medido lado a lado na mesma cena: este jogo não fornece vetores de movimento, então o FSR 3.1 reconstrói cada quadro sozinho e sai mais suave. Com nitidez RCAS no máximo (ligada automaticamente ao escolher FSR 3.1) ele fica igual ao FSR 1 em nitidez e custo. Por isso o padrão é o **FSR 1**.
- Trocar entre FSR 1 e FSR 3.1 agora é salvo na hora (antes não era).
- **O contador de FPS (F6) mostra a ampliação em uso**: `FSR 3.1 ativo 1280x720 -> 1920x1080` (verde) só quando o FSR 3.1 realmente rodou naquele quadro; `FSR 1 ...`, `Nativa ...` ou `Sem ampliacao` nos outros casos. Se a placa de vídeo não aceitar o FSR 3.1, aparece `FSR 3.1 indisponivel - usando FSR 1` e o jogo segue com o FSR 1, sem tentar de novo a cada quadro.

**Barra "Nitidez" (0 a 10)** logo abaixo da ampliação: 7 é o padrão do FSR 1, 8 o do FSR 3.1; 9 e 10 ligam também a nitidez própria do FSR 3.1. Bordas de contraste altíssimo (textos e contornos do HUD) são protegidas da nitidez. Se a imagem ficar granulada, diminua.

**Anti-serrilhado FXAA opcional, desligado por padrão.** Ele roda na imagem de 720p antes da ampliação e deixa os textos pequenos do HUD (números do velocímetro) turvos e com contorno duplo. Ligue só se preferir bordas mais suaves.

**Configurador em português, inglês ou espanhol**: seletor ao lado do título. O idioma dos textos do jogo continua em "Idioma do jogo".

**Textos em português do Brasil.** Com o idioma "Portugues", menus, dicas, HUD da corrida, legendas do copiloto e a DLC Pacote Tempestade aparecem em pt-BR (788 frases do jogo + 128 da DLC). Os arquivos originais do jogo não são alterados: uma cópia traduzida é gerada ao abrir (`content_ui`), como já acontecia com o texto de "Conteúdo comprado". As traduções ficam em `translations` e podem ser corrigidas ali.

**Capas da Steam.** "Adicionar à Steam" agora coloca capa, banner, imagem do topo (hero), logo e ícone, com o logo no canto superior esquerdo para não cobrir o barco. Banner e hero são as artes do Monscriser no SteamGridDB (créditos em `steam_art\ORIGEM.md`). Feche a Steam antes de adicionar.

## Medições (PC de teste com placa NVIDIA; Asgard, corrida real, 1 minuto acelerando e virando)
| Saída / nível | Imagem interna | FPS médio (mín.) | GPU (Windows) |
|---|---|---|---|
| 1080p Nativa | 2560x1440 | 85 (80) | 99% |
| 1080p Qualidade + FSR 3.1 | 1280x720 → 1080p | 120 (120) | 52% |
| 1080p Qualidade + FSR 1 | 1280x720 → 1080p | 120 (120) | 46% |
| 1440p Nativa | 2560x1440 | 89 (84) | 100% |
| 1440p Qualidade + FSR 3.1 | 1280x720 → 1440p | 120 (120) | 53% |
| 4K Nativa | 3840x2160 | 39 (37) | 100% |
| 4K Qualidade + FSR 3.1 | 2560x1440 → 4K | 77 (74) | 100% |
| 4K Desempenho + FSR 3.1 | 1280x720 → 4K | 120 (120) | 56% |

24 corridas com FSR 3.1/FSR 1, nenhum travamento. Ainda não medido no ROG Ally nem no Steam Deck.

## Estado real
- Windows: compilado e testado no PC de teste (todas as resoluções e níveis, launcher salvando cada opção).
- Linux: compilado com FSR 3.1; testado só sem GPU (abre e para por falta de driver Vulkan na máquina de compilação). Precisa ser testado no CachyOS/Ally.

Teclado: Enter=Start, Espaço=A, E=acelerar (RT), WASD/setas=direção, Backspace=B. O pacote não inclui jogo, DLC, update ou saves. Licenças em `licenses`.
