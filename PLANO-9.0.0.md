> Atualização 16/10 (noite): 9.1.0 EMPACOTADA = 9.0.0 + envio em segundo plano (`ht_async_submit`, seção 25 do CLAUDE.md).

> Atualização 16/10 (fim do dia): 9.0.0 EMPACOTADA (seção 24 do CLAUDE.md). Feitos: item 1 (ZPD em lote), item 4 (constantes dinâmicas + registradores rápidos `ht_cp_fast_registers`). Item 2 analisado (quebras vêm dos resolves; sem ganho seguro). Item 5 já era um shader único. Pendentes no Ally: item 0 (kit `outputs/kit-ally-9.0.0`) e item 3.

> Atualização 16/10: item1 e constantes dinâmicas implementados e testados na candidata. Provas finais e A/B da cena do cais concluídos; navegação/Ally pendentes. Nenhum pacote novo. Continuar na seção 23 e no documento de passagem.

# Plano da revisão 9.0.0 — salto de FPS (principalmente ROG Ally / AMD)

Escrito em 16/10/2026, depois da 8.9.2. Meta do Ian: mais FPS na tela no ROG Ally Z1E (hoje ~120 FPS a maior parte do tempo, 720p + FSR 1), **sem perder fidelidade** e sem instabilidade. Cada item segue o método aprovado: medir → mudar com cvar liga/desliga → prova exata + controle positivo → A/B na corrida (Asgard, save antigo do Ian copiado para game-test) → custo em FPS/ms. Se uma correção custar desempenho, otimizar esse custo antes de entregar.

**Atualização do Ian nesta retomada:** não consegue enviar os dados do Ally agora. Continuar medindo e otimizando no PC NVIDIA disponível, reduzindo custo de GPU E CPU e aumentando FPS com fidelidade. Ele testa o Ally com Linux depois. Ver seção 22 do AGENTS.md. A exigência anterior de dados do Ally como pré-requisito foi substituída por este pedido.

## Onde o tempo está hoje (RTX 5060 Ti, 1080p Qualidade, 60 FPS, Asgard) — 8.9.2

- GPU 2,0–2,4 ms/quadro: resolve 0,37 · transfers 0,14 · upload 0,15 · desenho do jogo ~1,4–1,7.
- Por quadro: ~2.000–2.500 desenhos, **~56–69 render passes**, ~56 barreiras (32 quebram render pass), 18 resolves, 10 transferências, 3 submissões (eram 9–11 antes da 8.9.2), ~22–32 eventos de oclusão (ZPD).
- CPU: thread de comandos ~5–6 ms/quadro (bindings ~0,9, prim ~0,7, tail ~0,4); duas threads do jogo ~340 ms/s cada (lógica real).
- Ally: **não medido por passada ainda**. GPU AMD sofre mais com quebras de render pass/barreiras em MSAA 4× e com banda de memória.

## Itens, em ordem de valor esperado

### 0. Medição no Ally (validação posterior; prioridade atual é o PC NVIDIA)
- Kit `outputs/kit-diagnostico-gpu-ally/` (montado em 16/10): DLL de diagnóstico (tempo por desenho + barreiras + vários quadros) e script que troca a DLL, roda uma corrida, restaura e junta o log.
- Analisar com `tools/test/draw_timing.py` (separar quadros) → dizer quanto custam no AMD: render passes, resolves, transferências, passadas de tela cheia, sombras.

### 1. Oclusão (ZPD) sem fechar a render pass — candidato principal
- Hoje `ExecutePacketType3_EVENT_WRITE_ZPD` chama `EndRenderPass()` a cada evento, porque `vkCmdResetQueryPool` não pode estar dentro de uma render pass. Em Asgard isso é ~25–30 das ~60 render passes/quadro.
- Ideia: zerar (reset) um bloco de consultas de uma vez fora da render pass (início do quadro ou quando o cursor do pool cruza um bloco); dentro da render pass só `vkCmdBeginQuery/EndQuery`. Regra Vulkan: consulta começada dentro de uma render pass termina na mesma subpass → se a render pass terminar com consulta ativa (troca de alvo, barreira), encerrar a consulta host e abrir outra que **soma no mesmo intervalo** (intervalo = lista de consultas host).
- Prova: o placar `[ht-zpd-stats]` (patch_zpd_trace.py) com leak/miss iguais ao da 8.9.2; amostras por intervalo idênticas no rastro; contador de render passes por quadro (`rp=`) deve cair ~25–30.
- Medir: GPU ms (NVIDIA) e, principalmente, FPS no Ally com o kit.

### 2. Barreiras que quebram render pass (rp_breaks_barrier ≈ 32/quadro)
- Rastrear com `patch_barrier_trace.py` quais barreiras são evitáveis (ex.: barreira de memória compartilhada em desenhos sem memexport, transições repetidas do mesmo alvo). Só remover com prova (sentinela/verify) — histórico: seção 14 diz "quase todas exigidas pelos resolves".

### 3. Passadas de tela cheia no AMD
- PS `9895C800754993AC` (oclusão tipo SSAO, 17 leituras com textureGrad): no NVIDIA a troca por LOD explícito deu ruído (descartado lá), **pode valer no AMD**. Precisa A/B no Ally (`HT_MEASURE_FETCH_LOD0=1` já existe como patch só-medição) e prova de imagem (não é exato: exige comparação SSIM com controle).

### 4. CPU para portáteis (se o Ally mostrar limite de CPU)
- `bindings` ~0,9 ms e `prim` ~0,7 ms/quadro na thread de comandos: medir por dentro antes (cronômetro só-medição como em patch_measure_reqrange.py).
- `ht_zpd_flush=false` já na 8.9.2 (−5% CPU/desenho, 3 submissões/quadro).

### 5. Apresentação no Ally
- Saída 720p em tela 1080p borderless = FSR 1 EASU 1080p + RCAS + dither por quadro. Medir o custo do apresentador no Ally (kit). Opções sem perder fidelidade: unir RCAS+dither numa passada; pular dither quando a saída é 8 bits sem banding visível (exige comparação de imagem).

## O que NÃO fazer (já descartado com prova — seção 14 do CLAUDE.md)
FIFO/mailbox forçado, vértices na GPU, sem dither, aniso 8×, fsi, escalas 2×1/1×2, gamma unorm16, pular gravação na memória escalada, reaproveitar descriptor sets de textura, pular transferências de leque sem prova, `force_additive_depth_test` (apaga brilhos visíveis). Não reduzir MSAA/escala para "ganhar FPS".

## Critério para chamar de 9.0.0
- Ganho real medido no hardware atual: redução de custo de GPU E CPU, aumento de FPS acima da variação, sem regressão visual (provas exatas + fotos de Asgard e da pista 1) e 0 diferenças nos verificadores. Ally/Linux é a validação posterior do Ian, conforme atualização acima.
- Se o ganho medido for pequeno, publicar como 8.9.x e dizer isso ao Ian; não inflar número de versão.
