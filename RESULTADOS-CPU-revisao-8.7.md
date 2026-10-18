# CPU — revisão 8.7 preview

Medição em 16/10/2026, Windows, NVIDIA RTX 5060 Ti, saída 1920×1080, Nativa (2560×1440 interno), sem geração de quadros. A correção foi comparada ligada/desligada no mesmo executável, acelerando e virando dentro da corrida.

| Limite | Correção | CPU do processo, em núcleos | FPS médio | Pior apresentação | Maior max_frame |
|---|---|---:|---:|---:|---:|
| 60 FPS | Desligada | 2,0325 | 60,0 | 17,7 ms | 17,2 ms |
| 60 FPS | Ligada | 1,1846 | 60,0 | 17,3 ms | 17,3 ms |
| Sem limite | Desligada | 3,4324 | 180,5 | 11,6 ms | 11,6 ms |
| Sem limite | Ligada | 2,9886 | 189,4 | 10,6 ms | 10,2 ms |

A 60 FPS, a carga do processo caiu **41,7%**, equivalente a cerca de 0,85 núcleo. As amostras não mostraram aumento dos piores tempos de quadro. Isso confirma o ganho na corrida deste PC; não garante ausência de engasgos em toda pista ou máquina.

Sem limite, não houve queda de FPS na comparação. As trajetórias não são perfeitamente idênticas; o aumento de 180,5 para 189,4 FPS não deve ser anunciado como ganho garantido. A carga de CPU caiu cerca de 12,9% nessa comparação. Não foi necessário alterar a pausa de 200 microssegundos.

## Como foi medido

Trecho de 20 a 55 segundos depois de iniciar a aceleração; dados do processo coletados aproximadamente a cada 400 ms. Um núcleo equivale a 1000 ms de CPU por segundo de tempo real. FPS, pior apresentação e max_frame vêm dos registros do próprio jogo. As capturas mostram a pista, o cronômetro, o movimento e o contador.

Na execução 53, o início foi obtido da primeira leitura de RT=255 no registro; nas demais, o roteiro gravou o horário de início. Carregamentos e menus ficam fora do trecho medido. O campo SemFoco dos primeiros roteiros não é uma medição válida; o acompanhamento visual foi feito separadamente. Os resultados válidos são os arquivos measurement.json, calculados novamente com limites inicial e final.

A primeira tentativa 52 não foi validada visualmente e está excluída dos resultados.

Provas nesta pasta de saída:
- provas-cpu/53-cpu-corrida-60-off/
- provas-cpu/54-cpu-corrida-60-on/
- provas-cpu/55-cpu-corrida-unlimited-off/
- provas-cpu/56-cpu-corrida-unlimited-on/
- provas-cpu/57-cpu-amostras-com-correcao/
- provas-cpu/58-revisao87-corrida-longa/
- provas-cpu/linux-llvmpipe-87/
- provas-cpu/build-87/

## Threads e amostragem

Na corrida a 60 FPS com a correção ligada, as duas threads mais ocupadas ficaram em 334 e 314 ms de CPU/s. Na repetição do profiler no menu, ficaram em 309 e 269 ms/s; as outras duas selecionadas, em 122 e 97 ms/s. Não apareceu outra thread consumindo um núcleo inteiro nessa amostra.

A thread da espera corrigida aparece principalmente em NanoSleep. A análise de pilha é heurística (endereços candidatos de retorno), não uma árvore de chamadas exata. Os endereços foram identificados antes de recompilar, usando os símbolos do executável que foi amostrado.

## Limites da validação

O teste comparativo ocorreu antes da recompilação com o rótulo 8.7; o código de CPU é o mesmo. O executável recompilado recebe uma verificação adicional. A corrida longa e a conferência final dos pacotes são registradas separadamente. Essa execução longa roda em parte junto ao llvmpipe e não é usada para calcular o ganho de CPU ou FPS.

Linux em llvmpipe verifica inicialização/renderização, sem representar uma GPU real. Ainda é preciso medir no ROG Ally, Steam Deck ou CachyOS com GPU real. O assistente não verificou a qualidade do som por escuta.
## Conferência adicional da compilação 8.7

O novo executável Windows permaneceu aberto durante o roteiro longo, com aceleração, curvas, recuperação do barco, pausa e retomada. O roteiro foi encerrado deliberadamente ao atingir o limite; não houve saída espontânea ou travamento registrado. **A chegada não foi comprovada**, pois o barco bateu e perdeu tempo. A qualidade do som também precisa de confirmação manual.

Parte desse teste coincidiu com o llvmpipe usando a maior parte da CPU: o trecho inicial registrou pior quadro de 24,9 ms e mínimo pontual de 59,4 FPS. Esses dados estão preservados, mas não substituem o A/B sem essa carga concorrente. O teste longo comprova execução e resposta aos comandos; não comprova uma corrida completa sem engasgos.

Linux: a compilação 8.7 renderizou a tela de título durante 120 segundos no llvmpipe/Xvfb. Foram comparados 5.799.251.968 texels com 0 diferenças. Capturas em linux-30.png, linux-60.png, linux-90.png e linux-120.png. É uma verificação de funcionamento, não um benchmark de GPU real.
