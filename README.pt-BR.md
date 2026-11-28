# Hydro Thunder Hurricane — Port para PC Windows e Linux | HydroThunderPORT

[English](README.md) · **Português (Brasil)** · [Español](README.es.md)

[Download](https://github.com/monscriser/HydroThunderPORT/releases) · [Site do projeto](https://monscriser.github.io/HydroThunderPORT/) · [Issues](https://github.com/monscriser/HydroThunderPORT/issues)

<a href="https://recomplauncher.com/game/hydrothunderport"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

## Estado do projeto — FINAL BUILD e o futuro do HydroThunderPORT

> [!IMPORTANT]
> A **FINAL BUILD** é a última versão da geração 1.x, baseada na arquitetura atual de recompilação estática e compatibilidade gráfica com o Xbox 360. Ela permanece como a versão estável recomendada para Windows e Linux. **Este é o encerramento do ciclo técnico atual, não o fim do HydroThunderPORT.**

A próxima geração, **HydroThunderPORT 2.0**, está planejada com um novo renderizador Vulkan nativo. Diferentemente do backend Vulkan atual, a arquitetura proposta traduzirá operações gráficas de alto nível do jogo diretamente para Vulkan sempre que viável, reduzindo a dependência da camada legada de compatibilidade da GPU Xenos. A recompilação estática da CPU continuará fazendo parte da abordagem.

O objetivo é melhorar a escalabilidade gráfica, a eficiência e o desempenho, preservando os efeitos visuais, a física das embarcações e a jogabilidade original. São objetivos de desenvolvimento, não funcionalidades ou resultados disponíveis atualmente.

O trabalho na 2.0 está previsto para depois da conclusão e validação do ciclo atual de desenvolvimento do [UFC3MONS](https://github.com/monscriser/UFC3MONS). **Não há data ou previsão de lançamento.** Não estão previstas novas versões rotineiras da linha 1.x. A FINAL BUILD continuará disponível como referência estável durante o desenvolvimento e os testes da nova arquitetura.

Obrigado a todos que jogaram, testaram, relataram problemas e apoiaram o projeto. A contribuição de vocês ajudou a tornar esta versão possível.

## Sobre o port

HydroThunderPORT é um port experimental e não oficial de Hydro Thunder Hurricane para Windows e Linux. Utiliza ReXGlue para recompilação estática e um backend Vulkan com componentes de compatibilidade Xbox 360 baseados no trabalho do Xenia. Tem como base o [htrecomp](https://github.com/visibou/htrecomp) 1.0 de visibou, incluindo os fundamentos do aplicativo, launcher/overlay e recompilação.

**Arquivos originais do jogo, DLCs e Title Updates não são incluídos.** Utilize suas próprias cópias obtidas legalmente.

| Recurso | FINAL BUILD atual |
| --- | --- |
| Windows | Executável nativo e launcher de configuração |
| Linux | AppImage com launcher |
| Gráficos | Vulkan (arquitetura legada atual) |
| Controles | Controle, teclado remapeável e quatro perfis locais nomeáveis |
| Imagem | FSR 1, FSR 3.1 experimental, FXAA, filtragem anisotrópica até 16x, resolução e exibição |
| Desempenho | Caches persistentes de shaders/pipelines e limites de FPS |
| Portáteis | ROG Ally testado; validação mais ampla em CachyOS/Steam Deck ainda limitada |

## Destaques das versões

### FINAL BUILD (PGO)

- Testes no ROG Ally cobriram as 11 pistas. Em Area 51, o mínimo relatado passou de 55 para 70 FPS ou mais (cerca de 27%); melhorias semelhantes foram relatadas nas demais pistas. Os resultados dependem das condições e do hardware.
- Antes do menu, o jogo prepara os shaders conhecidos das pistas para a GPU e o driver locais, mostrando o progresso. A lista contém 1.444 pipelines e 586 shaders. Inicializações posteriores reutilizam o cache; shaders não listados ainda podem causar uma travada no primeiro uso.
- Logs desativados por padrão; podem ser ativados em Saves e logs → Gravar arquivos de log.
- Inclui as melhorias da 1.2.0. A versão final é distribuída com PGO para ambas as plataformas.

### BETA 1.2 (1.2.0)

- Telemetria coletada no ROG Ally a 20 W em 8 pistas base e 3 de DLC, incluindo dados por quadro, threads e funções da CPU, traces de GPU, memória, clocks e potência.
- Reutilização de conjuntos de descritores de textura dentro do quadro (aproximadamente 96%). A etapa medida anteriormente custava 3,17 ms por quadro na thread limitante das pistas pesadas, como Area 51, Tsunami Cup e City; a verificação de equivalência registrou zero diferenças.
- No Linux, a espera do áudio deixou de consumir CPU continuamente: a thread passou de 77% de um núcleo para cerca de 1%, após corrigir um sleep arredondado para zero milissegundos.
- Corrigido o carregamento involuntário do RenderDoc e seu overlay de captura no Steam Deck/SteamOS.
- Launcher com navegação por ícones, página de novidades, tamanho de texto adaptável e navegação por controle/teclado. A partir desta versão, apenas pacotes PGO são publicados. A estimativa de FPS da 1.2.0 era baseada em telemetria, não em benchmark da release naquele momento.

### BETA 1.1 (1.1.0)

- Editor de saves para nomes dos jogadores 1–4 e créditos, com backup e recálculo do checksum. Jogadores 3 e 4 receberam saves independentes.
- Filtragem anisotrópica: padrão do jogo, 2x, 4x, 8x e 16x (padrão 16x); nenhum custo de GPU foi medido no teste relatado.
- Verificadores de equivalência CPU/GPU registraram zero diferenças com as bibliotecas da versão em Windows e Linux.

### BETA 1.0 (1.0.0)

- Builds Standard e PGO tinham os mesmos recursos. Testes Windows sem limite de FPS registraram ganhos PGO de 3,5% em Asgard Invasion e 9% em uma pista de cânion fora do perfil inicial. Linux utiliza perfil próprio.
- Launcher baseado na organização do UFC3MONS: seletor de idioma, Início, Vídeo, Controles, Saves e logs, Geração de quadros, Adicionar à Steam e Jogar.
- Local de `user_data` configurável, com cópia dos dados e manutenção da pasta antiga como backup; a seleção persiste entre atualizações. Importação do Xenia localiza `ht2save.dat` e faz backup do save atual.
- Corrigido tamanho excessivo da janela com escala de exibição do Windows em 125%/150%.
- Removido um lock global na consulta de páginas de memória pela thread de comandos de vídeo; custo de CPU por draw nessa thread reduzido em 2,7%, conforme medição relatada.

### Revisão 9.1.0

- Otimizações configuráveis em `hydrothunder.toml` buscam eficiência de CPU/GPU sem reduzir resolução, MSAA, efeitos ou precisão.
- Testes informais nas quatro primeiras pistas no ROG Ally Linux a 25 W registraram cerca de 120 FPS na maior parte do tempo, raramente abaixo de 100 FPS; não constituem benchmark controlado nem garantia.
- Corrigidos crash ao sair pelo menu e iluminação visível através de paredes; adicionados controles de teclado remapeáveis no launcher.

Consulte as [Releases](https://github.com/monscriser/HydroThunderPORT/releases) e as [notas da FINAL BUILD](NOTAS-FINAL-BUILD.md) para medições históricas, downloads e checksums.

## Recursos e configuração

- Instalação guiada de conteúdo Xbox 360 extraído e pacotes de conteúdo compatíveis (jogo, DLCs e Title Update), incluindo a configuração da atualização 0.0.2.1. Conteúdo e traduções compatíveis são tratados em execução sem alterar os arquivos-fonte originais do jogo.
- Pasta do jogo persistente e `user_data` configurável (saves, DLCs e conquistas); importação de saves do Xenia, backup/restauração, editor de nomes e créditos para quatro jogadores, overlay opcional de FPS e exportação de atalho Steam com arte.
- Launcher em inglês, português brasileiro e espanhol, com modo de exibição, resolução, idioma, escalonamento, FPS e instalação.
- Controle, teclado remapeável e até quatro perfis locais; tela dividida disponível, mas requer validação mais ampla.

| Ação | Tecla padrão |
| --- | --- |
| Iniciar / Pausar | Enter |
| Confirmar / A | Espaço |
| Acelerar / RT | E |
| Frear / LT | Q |
| Direção | A/D ou setas |

### Gráficos e desempenho

O backend atual oferece modos janela, sem bordas e tela cheia, escalonamento e nitidez FSR 1, reconstrução FSR 3.1 experimental, FXAA opcional, filtragem anisotrópica até 16x, compilação paralela de shaders, caches persistentes e gravação de cache em segundo plano. Limites de FPS: 30, 40, 60, 90 e 120; apresentação ilimitada experimental usa relógio virtual de 60 Hz.

O FSR 3.1 não possui dados completos de vetores de movimento do jogo. Upscaling não é geração de quadros; selecionar FSR 3.1 não gera quadros. A compatibilidade do cache depende do hardware, driver e build.

### Integração opcional com Lossless Scaling

O launcher pode detectar uma cópia Steam adquirida separadamente, configurar perfis Windows com backups e selecionar multiplicador de geração de quadros. No Linux, orienta a instalação da camada externa `lsfg-vk` e informa as configurações solicitadas no overlay. Lossless Scaling não está incluído; Linux exige uma camada Vulkan compatível além do aplicativo Steam. A validação de ponta a ponta ainda é incompleta.

## Instalação

### Windows

1. Extraia `Hydro-Thunder-FINAL-BUILD-Windows-PGO.zip` obtido nas [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. Abra `Launcher-Config.exe` e selecione seus arquivos de jogo, DLCs e atualização.
3. Configure vídeo e controles, depois selecione Jogar.

Após configurar, é possível abrir `HydroThunder.exe` diretamente. Use o launcher para configurações, saves e atalhos Steam.

### Linux / AppImage

1. Baixe o AppImage Linux nas [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. Se necessário, conceda permissão: `chmod +x HydroThunder*.AppImage`.
3. Abra, selecione os arquivos do jogo obtidos legalmente, configure e selecione Jogar.

Exporte um atalho Steam pelo launcher para iniciar diretamente pela Steam.

### Geração de quadros opcional

- **Windows:** instale e abra sua cópia Steam adquirida do Lossless Scaling, feche-a e ative a integração no launcher. Comece com 2x. Feche Lossless Scaling antes de alterar configurações do perfil.
- **Linux:** instale o aplicativo adquirido, escolha a branch Steam `lsfg-vk` adequada, instale camada Vulkan 2.x compatível com a distribuição, execute o diagnóstico da camada, depois faça nova busca e ative a integração. Siga a documentação da própria camada.

## Saves, atualizações e compatibilidade

A seleção da pasta do jogo é persistente. Faça backup dos saves antes de trocar de build, extraia em uma pasta separada, confira o caminho do jogo e mantenha a build anterior até concluir os testes. Nunca exclua a pasta real do jogo ao remover um launcher antigo.

O desempenho depende de hardware, drivers, configurações, pista e cache. Recompilação estática não garante vantagem sobre o Xenia. A geração de quadros não aumenta a taxa de simulação subjacente nem elimina latência de entrada.

## Limitações conhecidas

- A FINAL BUILD encerra o ciclo técnico 1.x; não estão previstas atualizações rotineiras. O futuro renderizador 2.0 não está incluído.
- Tela dividida com três ou quatro controles físicos ainda não foi testada, embora os quatro perfis tenham sido verificados. Feche o jogo antes de editar créditos no save.
- A validação mais ampla de Linux/CachyOS/Steam Deck e portáteis permanece limitada; observações no ROG Ally não são garantias universais.
- Lossless Scaling requer mais testes de ponta a ponta. O overlay do jogo não verifica quadros gerados de forma independente; contadores Linux dependem de overlays externos e da ordem das camadas.
- Apresentação acima da taxa de atualização ou ilimitada pode causar ritmo irregular, tearing ou regressões; modo ilimitado não passou por validação exaustiva de física/áudio.
- Shaders não listados ainda podem causar uma travada inicial; defeitos gráficos como luzes coloridas dos portais visíveis através do cenário podem permanecer.
- Exportação de atalhos Steam requer mais testes em instalações reais; algumas mensagens de erro estão somente em português.
- FSR 3.1 não possui entradas temporais completas.

## Compilação a partir do código-fonte

Gere o código recompilado a partir de sua própria cópia do jogo: `app/generated-updated/` não é distribuído aqui. Scripts: [`tools/`](tools/) e [`tools/wsl/`](tools/wsl/). PGO utiliza CMake `-DHT_PGO=USE -DHT_PGO_PROFILE=<file>` e perfis em [`pgo/`](pgo/), com `build_pgo.sh` / `build_pgo_linux.sh`; Linux também requer o runtime de perfil de `build_profile_runtime.sh`.

O projeto utiliza ReXGlue, componentes de compatibilidade derivados do Xenia, Vulkan e Dear ImGui. Consulte [`licenses/`](licenses/) e [`LICENSE-ReXGlue`](LICENSE-ReXGlue) para os termos de terceiros aplicáveis.

## Issues e contribuições

Ao relatar uma [issue](https://github.com/monscriser/HydroThunderPORT/issues), inclua versão, sistema, driver, CPU/GPU/RAM, configurações de exibição e FPS, estado da geração de quadros, pista/modo, passos para reproduzir, logs relevantes e capturas/vídeos quando úteis. Compare com Xenia em condições equivalentes. Relatos podem orientar a 2.0, mas não implicam uma correção agendada na 1.x.

Nunca envie conteúdo protegido do jogo, DLCs, atualizações ou dados pessoais de saves em issues públicas.

## Créditos e aviso legal

Agradecimentos especiais a visibou e ao [htrecomp](https://github.com/visibou/htrecomp), base deste port e documentação original do formato de checksum dos saves. Obrigado aos colaboradores de ReXGlue, Xenia, Dear ImGui, FidelityFX, lsfg-vk e à comunidade de gráficos/emulação. As artes Steam pertencem aos respectivos colaboradores do SteamGridDB, incluindo Monscriser.

HydroThunderPORT é um projeto comunitário não oficial, sem afiliação ou endosso da Microsoft, Xbox ou desenvolvedores/editoras originais. Nomes, artes e assets originais pertencem aos respectivos titulares. Não são distribuídos arquivos proprietários do jogo ou Lossless Scaling. Respeite as licenças de terceiros ao reutilizar ou redistribuir código.
