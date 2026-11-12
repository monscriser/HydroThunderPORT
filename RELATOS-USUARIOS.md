# Relatos de usuários

## 1. Steam Deck — texto preto "Capturing Vulkan" preso no canto (1.1.0 BETA, recebido 11/11/2026)

Relato original (inglês):
> "This runs great on my steam deck!! thank you! my only gripe is there a dumb black text overlay stuck on the top left of my screen, ive tried looking at every setting to disable it in game but none works, no combo of keys removes it, the overlay says capturing vulkan then says f12 or print screen to capture."

**Causa (achada em 11/11/2026):** esse texto é o overlay do **RenderDoc** (ferramenta de captura de quadros de GPU). O jogo não traz o RenderDoc, mas o SDK (herdado do Xenia) tentava "conectar ao RenderDoc se ele estiver anexado" com um `dlopen("librenderdoc.so")` normal. No Linux isso **carrega** a biblioteca se ela existir em qualquer lugar do sistema — e o **SteamOS já vem com `/usr/lib/librenderdoc.so`**. Resultado: o próprio jogo injetava o RenderDoc em si mesmo → overlay na tela e, de quebra, "GPU debug markers" ligados automaticamente (custo extra de desempenho). Não há tecla nem opção do jogo que desligue, porque o overlay é do RenderDoc.

Era mesmo "uma ferramenta de captura deixada ligada", como o Ian suspeitou — só que no SDK, não numa opção nossa.

**Correção:** `tools/wsl/patch_renderdoc_noload.py` (aplicada ao SDK). Linux: só conecta se o RenderDoc **já** estiver no processo (`dlopen` com `RTLD_NOLOAD`); Windows: só se o módulo já estiver carregado (`GetModuleHandle`). Quem abre o jogo pelo RenderDoc de propósito continua funcionando.

**Prova:** `tools/wsl/diag/stage_diag_linux.sh` põe uma `librenderdoc.so` falsa no caminho de bibliotecas (como o SteamOS faz) que só grava um arquivo-marca se for carregada. Resultado em `hydro-port\linux-llvmpipe-110-diag\`.

Estado: corrigido no SDK; entra na próxima versão (1.1.1 / 1.2.0). Build de diagnóstico Linux já tem a correção.
