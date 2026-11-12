# Hydro Thunder Hurricane: Recompiled — FINAL BUILD (Windows e Linux, PGO)

**Esta é a versão final do projeto.** Obrigado a todos pelo carinho e pelo apoio!

Inclui tudo da 1.2.0 BETA e mais:

## Novidades
- **Shaders de todas as 11 pistas pré-compilados para a sua placa de vídeo, antes do menu.** O pacote leva só a **lista** de shaders e pipelines que o jogo usa em todas as pistas (8 base + 3 da DLC), gravada numa sessão completa de jogo. Na abertura, o jogo junta essa lista ao seu cache e compila tudo **com o driver do seu PC, para a sua GPU**, com barra de progresso ("Preparando shaders… N de M"). Nenhum shader compilado em outra máquina vai no pacote. A primeira abertura demora alguns segundos; depois fica guardado. Resultado: as corridas não têm mais as travadinhas de compilação de shader, inclusive na primeira vez numa pista. Pode ser desligado em *Vídeo* ("Pré-compilar os shaders de todas as pistas").
- **Fim da tela preta parada na abertura.** O jogo já compilava os shaders conhecidos antes do menu, mas travado na thread da janela; agora a janela continua viva e mostra o progresso.
- **Opção "Gravar registros (logs)"** em *Saves e registros*, **desligada por padrão**: o jogo não cria arquivos de log. Ligue se precisar relatar um problema.

## Testado no ROG Ally (todas as 11 pistas)
Na Área 51, a pista mais pesada, a mínima de FPS que antes chegava a **55** agora **não desce abaixo de 70** (+27%), e o ganho foi parecido em todas as outras pistas.

## Da 1.2.0 BETA (continua)
- Correção do texto "Capturing Vulkan / F12" no Steam Deck/SteamOS.
- Thread de áudio sem girar à toa no Linux (77% → ~1% de um núcleo).
- Cache dos conjuntos de texturas: ~96% menos escritas de descritores no driver por quadro.
- Launcher novo com menu lateral, aba Novidades e fontes que se ajustam à janela.

## A lista de shaders
- 1.444 pipelines e 586 shaders, da sessão completa no ROG Ally (só a lista; o cache compilado de lá não é usado), somados ao que já existia no PC de teste.
- Conferida registro por registro: o hash de cada pipeline bate; a conversão Linux ↔ Windows (os registros têm 66 e 68 bytes por diferença de alinhamento do compilador) foi validada — 454 dos 455 registros gravados no Windows são idênticos aos do Ally depois da conversão.

## Provas
| O quê | Resultado |
|---|---|
| Verificadores exatos (resolve, texturas, constantes, registradores, ZPD, envio, cache de descritores…) | **0 diferenças** no Linux e no Windows; validação Vulkan no Windows **0 erros** |
| Lista de shaders na primeira abertura | Linux: 586 shaders + 1.444 pipelines acrescentados, **1.444 de 1.444** pré-compilados antes do menu; Windows: idem |
| Abertura seguinte (Windows, RTX 5060 Ti) | 1.444 pipelines prontos em **1,5 s** (cache do driver); corrida a 60 FPS |
| Pré-compilação desligada | o jogo pula a etapa ("1.444 pipelines serão criados no primeiro uso") |
| Logs desligados (padrão) | nenhum arquivo de log criado |
| Pacotes | contêm exatamente os binários que passaram nas provas |
