# Uso de GPU medido dentro da corrida — revisão 8.2 preview

**Data:** 15/10/2026
**PC:** NVIDIA RTX 5060 Ti, monitor 1920×1080 a 75 Hz, saída em tela cheia sem bordas 1080p.

**Como foi medido:**
- Cópia isolada do jogo, pista de Asgard, acelerando e virando com o controle virtual.
- Os números são a média do trecho do segundo 20 ao 55 da corrida (cronômetro ~1:00 a ~1:35).
- `nvidia-smi` lê a cada 0,5 s os watts da placa, o clock e o uso. O FPS vem do log do próprio jogo (`[ht-present]`).
- A geração de quadros ficou desligada em todos os testes.

**Onde estão as provas:** `outputs/_teste_gpu/<teste>/`, com `result.json`, `nvidia-smi.csv`, `game.log`, `config.txt` e as fotos `race-50.png` e `race-130.png`.

Repetindo a mesma configuração, a linha de base variou entre **37 e 40 W**. Por isso, diferenças menores que ~3 W são ruído.

| # | Teste | FPS | Watts | Clock GPU (MHz) | Uso GPU | Pior quadro (ms) | Resultado |
|---|---|---|---|---|---|---|---|
| 1 | **Base:** Qualidade + FSR 1 (escala 1), 60 FPS | 60 | **40** | 2022 | 26% | 17,3 | referência |
| 2 | Nativa (escala 2 = 2560×1440 interno), 60 FPS | 60 | **85** | 2767 | 69% | 17,6 | **+111%**; é a causa dos "99%" antigos |
| 3 | Sem limite (Qualidade) | 208 | **85** | 2762 | 77% | 27,4 | **+112%**; a tela de 75 Hz só mostra 75 desses 208 |
| 4 | FIFO no lugar do mailbox, 60 FPS | 60 | 38,5 | 2003 | 25% | 17,5 | igual (dentro do ruído) |
| 5 | Caminho de render `fsi`, 60 FPS | 60 | **53** | 2569 | 36% | **26,2** | **pior: +33% e mais engasgos** |
| 6 | Escala 2×1 (só na horizontal), 60 FPS | 60 | **60** | 2721 | 39% | 17,5 | meio-termo entre Qualidade e Nativa |
| 7 | Vértices na GPU (`execute_unclipped_draw_vs_on_cpu=false`) | 60 | 40 | 2046 | 26% | 17,3 | igual |
| 8 | Sem pontilhado na saída (`present_dither=false`) | 60 | 39,4 | 2012 | 26% | 17,4 | igual |
| 9 | Filtro anisotrópico 8× (seu valor pessoal) contra 4× | 60 | 40,4 | 2027 | 26% | 17,3 | igual: 8× não custa nada |

## Conclusões

1. **As configurações atuais já são as mais econômicas** nas opções testadas: mailbox, caminho de render padrão, vértices na CPU, dither e filtro anisotrópico. Nenhuma troca de opção baixou o consumo além do ruído. A opção `fsi` piorou, como a pesquisa previa.
2. **O uso alto de GPU vem de duas escolhas do usuário, não de defeito:**
   - **"Nativa"** desenha internamente em 2560×1440 e reduz para 1080p. É uma imagem mais limpa, mas custa **o dobro** (85 W contra 40 W).
   - **"Sem limite"** desenha 208 quadros por segundo numa tela de 75 Hz. Cerca de 2 em cada 3 quadros são jogados fora, também a **85 W**.
3. **Com 60 FPS e "Qualidade" + FSR 1, a placa usa ~40 W e ~26%.** Para comparar: parada, na área de trabalho, ela usa ~14 W.
4. A escala 2×1 fica no meio (60 W). A imagem não mostrou distorção nas fotos, mas ainda falta uma comparação lado a lado, sem perdas, com a "Nativa".
5. As correções do Xenia Edge listadas no relatório de pesquisa diminuem esperas e trabalho de CPU. Com 60 FPS travados, a GPU já fica ociosa a maior parte do tempo, então não há como elas baixarem os watts de forma mensurável aqui. Elas só fariam sentido para FPS máximo ou para o ROG Ally.

## O que não foi testado

- Monitor em 60 Hz contra 75 Hz (prioridade 4 da pesquisa). Isso muda uma configuração do Windows do Ian, então não foi feito sem pedir.
- ROG Ally / Steam Deck / CachyOS.
- Captura de quadro em RenderDoc/Nsight.
