# Hydro Thunder Hurricane: Recompiled — 1.1.0 BETA

Continua em **duas variantes**, com o mesmo conteúdo e as mesmas opções: **Standard** (build normal) e **PGO** (código do jogo otimizado pelo compilador com base em corridas reais; na dúvida, use a PGO).

## Novidades da 1.1.0
- **Editor de save** (*Saves e registros → Editor de save*):
  - **Nomes dos 4 jogadores** (tela dividida). O jogo mostra esses nomes no lugar do gamertag. Até 15 letras, sem acentos (a fonte do jogo não tem). Valem na próxima abertura do jogo.
  - **Créditos do save:** escolha o save do jogador e digite a quantidade. Os créditos liberam pistas e barcos novos. Antes de gravar, uma cópia do save vai para `save_backups`. Feche o jogo antes de mudar.
- **Jogadores 3 e 4 têm perfil próprio:** antes só existiam os jogadores 1 e 2. Cada jogador tem o seu save separado.
- **Filtro anisotrópico escolhível** (*Vídeo → Imagem → Filtro anisotrópico*): Do jogo, 2x, 4x, 8x ou **16x**. O padrão agora é **16x** (antes 8x): medido sem custo.

## Medições e provas
| O quê | Resultado |
|---|---|
| 16x × 8x, Nativa (escala 2), 60 FPS, 2 corridas de cada | GPU 4,22 ms × 4,23 ms por quadro (igual, dentro do ruído) |
| Créditos editados pelo configurador (777.777) | O jogo **aceitou** o save e manteve os créditos |
| Controle: mesmo save com o código de conferência (CRC) errado de propósito | O jogo **rejeitou** e criou um save novo (0 créditos): prova de que a conferência é feita e de que o editor grava o CRC certo |
| Conferência independente do save (programa separado, em Python) | CRC certo depois da edição |
| Nomes | O jogo recebeu os 4 nomes (registro `[ht-players]`); os 4 perfis têm identidade e pasta de save separadas (verificação automática do jogo) |
| Verificadores exatos com as DLLs desta versão (resolve, texturas, constantes, registradores, ZPD, memória) | **0 diferenças**, validação Vulkan com 0 erros |
| Linux (llvmpipe, as duas variantes) | Verificadores e perfis de jogador conferidos |

As DLLs mudaram nesta versão (perfis dos jogadores 3 e 4 no runtime), por isso todas as provas exatas foram refeitas. As duas variantes do Windows usam as mesmas DLLs; só o executável muda.

## Créditos
O formato da conferência do save (CRC sobre os valores) foi documentado primeiro pelo projeto [htrecomp](https://github.com/visibou/htrecomp) de visibou, base deste port.

## Não testado
- Tela dividida com 3 ou 4 controles de verdade (os perfis existem e foram conferidos; falta jogar com 4 controles).
- ROG Ally / Steam Deck com esta versão.
