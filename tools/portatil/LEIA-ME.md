# Kit de medição — ROG Ally / Steam Deck

Mede o desempenho da revisão 8.6 (ou mais nova) numa corrida de verdade. Não altera o jogo nem os saves: liga o registro de desempenho só durante a medição e depois devolve a configuração como estava.

## ROG Ally (Windows)
1. Instale a revisão 8.6 no Ally (zip `Hydro-Thunder-Windows-revisao-8.6-preview.zip`) e abra o jogo uma vez pelo launcher para ele ficar configurado.
2. Copie esta pasta para o Ally e clique duas vezes em `medir-desempenho.cmd`.
3. Siga o que aparece na janela: abra o jogo pelo launcher, jogue **uma corrida inteira em Asgard** acelerando e virando, feche o jogo e aperte Enter na janela.
4. Na Área de Trabalho aparece a pasta `hydro-medicao` (resumo + log). Mande essa pasta para o Claude.

Dica: meça duas vezes, uma com "Qualidade da imagem" = Qualidade e outra com Nativa (no launcher, aba Vídeo).

## Steam Deck / CachyOS (Linux)
1. Abra o jogo (AppImage da 8.6) uma vez e feche.
2. No modo Área de Trabalho, abra o Konsole nesta pasta e rode: `bash medir-desempenho.sh`
3. Jogue uma corrida inteira em Asgard (pode ser no Modo Jogo), feche o jogo, volte ao Konsole e aperte Enter.
4. Mande a pasta `~/hydro-medicao` para o Claude.
