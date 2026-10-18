# Comparação A/B neste PC

Kit de laboratório para confirmar os FPS durante uma corrida dirigida por você. Não é o pacote final da revisão 9.0.0.

1. Abra `MEDIR-A-B.cmd` e pressione Enter quando estiver pronto.
2. Na rodada A, escolha Asgard e o barco de sempre. Jogue por pelo menos 60 segundos, acelerando e virando. Feche a janela do jogo sem voltar aos menus.
3. Pressione Enter para a rodada B e repita a mesma pista e barco pelo mesmo tempo. Feche o jogo e aguarde a mensagem de restauração.
4. Volte ao chat e diga que terminou. Os registros já estarão neste computador; não precisa enviar arquivos.

As teclas e o controle funcionam normalmente. O kit não controla a sua pilotagem.

A usa o caminho anterior; B liga as duas otimizações. Mesmos binários finais da 8.9.3, imagem interna 720p, FSR 1, sem geração de quadros e sem limite de FPS. Configurações ficam em pastas próprias do kit. Somente os três binários da cópia `game-test` são trocados temporariamente, com backup em `_to_delete` e restauração no final. O jogo e os saves originais não são usados.

DLL candidata SHA256: `4269017ba74c9c602c988309c90cd74e45f52015e61bea538249dabe33cc2f40`.

Provas desta candidata: buffers/endereço/tamanho dos vínculos de constantes conferidos contra os originais, sem diferenças; esferas de oclusão comparadas no mesmo quadro contra consultas do caminho original, sem diferenças e com amostras visíveis. Os controles positivos detectaram os erros propositais. Verificadores pesados e erros propositais estão desligados neste kit.

Mantenha a janela de medição aberta até terminar as duas rodadas. Feche apenas a janela do jogo no fim de cada rodada. O executável e as bibliotecas anteriores são restaurados juntos.
