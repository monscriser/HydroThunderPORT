# Diagnóstico de GPU por passada — ROG Ally

Serve para descobrir **onde a GPU AMD do Ally gasta tempo** em cada quadro, e assim escolher as otimizações da 9.0.0. Não muda nada permanente: no fim, o script devolve a DLL e a configuração originais.

1. Copie a pasta `diagnostico-gpu` para **dentro da pasta do jogo** no Ally (a pasta onde está o `Launcher-Config.exe`).
2. Dê dois cliques em `MEDIR-GPU-ALLY.cmd`.
3. O jogo abre sozinho. Jogue **uma corrida inteira em Asgard** (pista 2), acelerando e virando, com as configurações de sempre (720p, FSR 1, sem limite de FPS).
4. Feche o jogo. A janela preta termina sozinha.
5. Na Área de Trabalho aparece a pasta `hydro-diagnostico-gpu`. Zipe e mande para o Claude.

A DLL de diagnóstico mede o tempo de cada desenho só em ~25 quadros espalhados (um a cada ~10 s). No resto do tempo o jogo roda normal.
