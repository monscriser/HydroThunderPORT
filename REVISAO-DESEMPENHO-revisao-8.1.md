# Revisão de desempenho e compatibilidade — 15/10/2026

## Escopo real

Revisão direcionada da aplicação, configuração, paths, importação de conteúdo, proteção de saves, Steam, integração LS, overlay e relógio/limitador. No SDK, foram examinados os caminhos de geração de vblank, apresentação Vulkan, compilação/cache de pipelines, escrita do cache e atualização de bindings. O diretório app/src tem 26 arquivos e aproximadamente 240 KB nesta etapa. Isso **não equivale a uma auditoria manual completa de todas as linhas do SDK e dos fontes PPC gerados**. Uma garantia global de ausência de bugs não seria sustentada por estes testes.

## Alterações aplicadas sem reduzir qualidade do jogo

1. Configuração: troca de dezenas de aberturas, leituras e gravações por um lote em memória e uma substituição atômica. Se o texto não muda, nada é regravado. Testado no Windows e Linux, incluindo preservação de comentários, valores desconhecidos e seções. Beneficia o configurador e a integridade das preferências; não foi apresentado como ganho de FPS de corrida.
2. Fontes das preferências: o painel F5 agora recebe os caminhos finais, depois da resolução da biblioteca persistente. Antes guardava os defaults anteriores à chamada de HtBaseApp::OnConfigurePaths. Corrige consistência entre launcher e painel, sem mudar a renderização.
3. Arte do launcher carregada uma vez e mantida somente durante o configurador. O jogo iniciado diretamente não carrega essas texturas do cabeçalho.
4. Desbloqueio real de trocas de quadros, mantendo a origem de tempo virtual. Captura mostra 187 FPS em corrida no PC NVIDIA, em 720p; logs registram pace=0.00. Não é otimização por si só: permite usar mais GPU/CPU e pode elevar consumo.
5. Modelo leve LSFG Linux não é mais forçado. O usuário escolhe explicitamente aceitar maior chance de artefatos em troca de menor custo.

## Verificação do estado existente

- Compilação já usa Release, -O3, NDEBUG, alvo x86-64-v3 e ajuste znver4. Não existe justificativa para acrescentar fast-math indiscriminadamente: o jogo depende de cálculos que podem mudar com essas opções.
- Compilação de pipelines já possui workers e cache nativo persistente. No teste de reinício, foram restaurados 9.116.948 bytes; o cache cresceu e foi salvo em background. Não se deve apagar o cache automaticamente em toda atualização.
- `SaveDriverCache` libera o lock antes da escrita em disco, mas a extração pelo driver ainda usa sincronização necessária. Retirar esse lock sem substituir o modelo de sincronização seria incorreto.
- `UpdateBindings` já mantém flags de validade e reúne escritas de descritores. Um cache mais amplo precisa considerar mudanças de texturas, buffers, layouts e tempo de vida dos recursos. Não basta suprimir chamadas `vkUpdateDescriptorSets`.
- Não foram encontrados usos de `vkDeviceWaitIdle`/`vkQueueWaitIdle` nos dois arquivos específicos verificados (apresentador Vulkan e command processor Vulkan); isso não prova ausência no SDK inteiro.
- A maior parte da apresentação é assíncrona em relação à simulação; quadros gerados por ferramenta externa não aumentam a taxa de resposta da simulação.

## Próxima otimização do renderizador, com critério de aceitação

Medir uma corrida repetível com o mesmo caminho, resolução, shaders aquecidos e número de jogadores, primeiro em CachyOS/ROG Ally real. Registrar frame time CPU/GPU, percentis e consumo; a contagem de FPS sozinha não identifica o gargalo.

Prioridades: custo de bindings/descritores, criação de pipelines novos, render targets/resolve, ciclos de GPU em FSR/pós-processamento e espera no compositor. Os logs do PC NVIDIA já separam várias dessas parcelas. Reutilização adicional de descritores ou pipeline libraries deve ser introduzida de forma isolada, comparada visualmente e mantida somente com evidência de melhora. Não foram implementadas novas políticas de descritores, shader math ou compressão do cache nesta prévia.

Não substituir qualidade por presets mais baixos e chamar isso de otimização equivalente. Não confundir frame generation, remoção de cap e aumento de FPS de renderização nativa.

## Pesquisa além do Xenia

- [Khronos — pipeline management](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html): persistência e aquecimento de pipelines antes de desenhar.
- [Khronos — descriptor management](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html): reutilização de descritores e organização de buffers; os percentuais do exemplo da Khronos não foram transferidos para este jogo.
- [Khronos — synchronization](https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html): sincronização por recursos, evitando esvaziar a fila sem necessidade.
- [AMD GPUOpen — Vulkan API overhead](https://gpuopen.com/learn/reducing-vulkan-api-call-overhead/): reduzir custo de chamadas e gestão de recursos com medições.
- [lsfg-vk — pacing](https://lsfg-vk.dev/docs/configuration/pacing-modes/): a versão documentada só oferece pacing vsync; desligar o override FIFO não cria automaticamente um pacing alternativo correto.
- [Decky experimental — configuration](https://github.com/eugeniosegala/decky-lsfg-vk-experimental/blob/main/docs/CONFIGURATION.md): existem mudanças de sincronização, FIFO ordenado e orçamento de entrega sob Gamescope. Isso oferece hipóteses para a experiência relatada pelo usuário; a versão que ele testou ainda é desconhecida.
- [Discussão upstream sobre V-Sync](https://github.com/PancakeTAS/lsfg-vk/discussions/488): forçar mailbox pode também comprometer/desativar FG. Por isso o modo acima dos Hz é experimental e não foi descrito como correção universal.
- [lsfg-vk — performance overlays](https://lsfg-vk.dev/docs/troubleshooting/performance-overlays/): a ordem das camadas pode impedir que um overlay conte os frames gerados. Não inventar medição multiplicando FPS reais pelo multiplicador selecionado.
