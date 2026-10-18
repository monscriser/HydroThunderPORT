# CPU — revisão 8.8 preview

A revisão 8.8 conserva a correção da espera ativa da 8.7 e acrescenta uma otimização pequena no envio de constantes aos shaders. As constantes consecutivas passam a ser copiadas juntas. O conteúdo, a ordem e o formato enviados à GPU continuam iguais.

## Resultado medido

Windows, RTX 5060 Ti, saída 1920×1080, modo Nativa (interno 2560×1440), geração de quadros desligada. Comparação com o mesmo executável e a mesma DLL, alternando apenas `ht_float_constant_runs`. A correção de espera da 8.7 permanece ligada em ambos.

| Medida na corrida a 60 FPS | Cópia original (67) | Cópia em blocos (68) |
|---|---:|---:|
| FPS | 60 | 60 |
| Preparação dos dados, ms/quadro | 1,138 | 0,838 |
| Desenhos/quadro | 2033,944 | 1903,722 |
| Preparação por desenho, microssegundos | 0,560 | 0,440 |
| CPU de comandos, ms/quadro | 5,476 | 4,860 |
| CPU do processo, núcleos equivalentes | 1,1778 | 1,2607 |
| Maior apresentação, ms | 35,1 | 17,4 |
| GPU, ms/quadro | 4,406 | 4,474 |

A preparação dos dados ficou aproximadamente **21% mais barata por desenho**. O custo total de CPU variou para cima nessa rodada; **não há comprovação de outra redução no consumo total de CPU**. Os percursos não são idênticos. A queda no maior tempo de quadro também não deve ser atribuída à otimização: há um pico isolado na base.

Sem limite, a base mediu **183,133 FPS desligada (70) e a compilação final 183,422 FPS ligada por padrão (71)**. Não houve queda nessa comparação. A preparação caiu de 1,247 para 0,919 ms/quadro, com 2040 e 2221 desenhos/quadro, respectivamente; por desenho, aproximadamente 0,611 para 0,414 microssegundo. A CPU do processo foi 2,9781 e 3,0090 núcleos equivalentes; a GPU ficou em 4,413 e 4,423 ms/quadro. A maior apresentação foi 15,0 e 14,3 ms. Uma rodada ligada anterior (69) deu 189,065 FPS, mostrando a variação entre percursos. Não anunciar aumento garantido de FPS.

No menu, a preparação caiu de aproximadamente 0,90 para 0,71 ms/quadro e a CPU de comandos de 4,29 para 4,07. A economia total do processo não foi consistente entre os testes.

## Método e provas

Os testes de corrida medem somente os segundos 20–55 depois do início da aceleração. O roteiro usa a entrada interna de teste, acelera e alterna breves comandos de direção. As imagens confirmam a pista e, quando capturado durante a aceleração, o movimento; o roteiro pode bater e não serve como prova de chegada. Os arquivos `measurement.json` contêm o recorte preciso. O campo `SemFoco=null` não é uma medição de foco.

As provas estão em `otimizacao-8.8/`, junto deste relatório:

- 60 e 61: aumentar a pausa de 200 para 1000 microssegundos não ajudou; padrão 200 mantido.
- 62: compilação inicial e teste de 108.323 mapas de constantes, incluindo mapas vazios, cheios, todos os intervalos e 100 mil mapas aleatórios. Conferidos bytes, tamanho e guardas de memória. O microbenchmark isolado não representa o ganho do jogo.
- 63: prova no menu, 720.896 uploads / 779.176.720 bytes, zero diferenças.
- 64: controle positivo detectou uma diferença provocada somente na referência; os dados reais da GPU não foram modificados.
- 65 e 66: comparação no menu, verificador desligado.
- 67 e 68: corrida a 60 FPS, desligada e ligada.
- 69 e 70: corrida sem limite, ligada e desligada.
- 71: confirmação sem limite com a compilação final e a nova opção ligada por padrão.
- 72: prova de bytes na corrida com a compilação final; não usar seu FPS/CPU como benchmark, pois a comparação de cada upload tem custo alto.
- `build-88/` e `linux-llvmpipe-88/`: compilação, empacotamento e conferência Linux.

## Validação e limites

Windows e Linux foram recompilados com a identificação `revision-8.8-cpu-preview`. A compilação final Windows passou pelo teste sem limite descrito acima. A sessão final Windows, incluindo a corrida, comparou **7.766.016 uploads / 8.776.302.464 bytes**, zero diferenças. No Linux, o título renderizou por 120 segundos em llvmpipe/Xvfb: **544.768 uploads / 588.207.680 bytes de constantes**, zero diferenças. O verificador de texturas também registrou **3.544.142.848 texels**, zero diferenças. A igualdade das constantes é verificada contra a cópia escalar original, sem aproximações de ponto flutuante.

O empacotador confere o CRC dos ZIPs, os arquivos de fonte e a igualdade de todos os binários Windows com a cópia testada. Os três binários Linux são comparados com os usados no smoke; uma conferência adicional extrai a AppImage pronta e compara novamente. Resultado em `otimizacao-8.8/build-88/artifact-validation.json` e `appimage-validation.txt`. Os hashes dos três pacotes ficam em `SHA256-revisao-8.8-preview.txt`.

O jogo original e seus saves não são usados como destino das alterações. Os roteiros restauram a configuração da cópia de teste; os hashes antes/depois são conferidos no empacotamento. A 8.7 e os pacotes anteriores ficam preservados.

Continuam pendentes a chegada e o som confirmados pelo jogador, testes no Ally/Deck e Linux com GPU real. Não foi medido ganho em watts nesses aparelhos nem revalidado Lossless Scaling nesta etapa. O teste Linux por llvmpipe comprova execução/renderização nesse ambiente, sem prever desempenho de GPU real.

## Desligar a nova otimização

No `hydrothunder.toml`:

    ht_float_constant_runs = false

O padrão da 8.8 é `true`. O verificador `ht_float_constant_runs_verify` e seu controle positivo ficam desligados no uso normal. A correção da espera ativa pode ser controlada separadamente por `ht_spin_backoff`.
