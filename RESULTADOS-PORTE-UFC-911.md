# Hydro Thunder: melhorias do UFC aplicadas (04/11/2026)

Base: revisão 9.1.0 (SDK conferido: patch idêntico, sha b9a73fce). PC: RTX 5060 Ti, 1080p, FSR 1.
Provas e logs: `E:\PROJETOS ANTIGOS\UFC3MONS-trabalho\hydro-port\runs\` e `linux-llvmpipe-911\`.

## O que entrou

| Item do UFC | No Hydro | Liga/desliga | Resultado |
|---|---|---|---|
| **B56**: verificação de páginas sem a trava global | **Entrou, ligado** | `ht_request_range_lockless` | CPU da thread de comandos −2,7% por desenho (1,49 → 1,45 µs); checagem de vértices −11%. FPS no cais 236 → 244, dentro da variação (no PC o limite é a thread do jogo) |
| **B57**: constante de fetch reescrita igual não invalida | No código, **desligado** | `ht_fetch_unchanged_skip` | Exato, mas no Hydro só ~6,5% das reescritas são iguais (UFC: 38%). Ganho medido no ruído, então fica desligado |

## O que não entrou (medido, sem alvo no Hydro)

| Item | Motivo |
|---|---|
| **A1** (upload antes da passada) | O SDK do Hydro já manda para o início da submissão as páginas ainda não usadas. Uploads que fecham passada na corrida: 0,0–0,2 por quadro |
| **A15** (retomar thread rodando) | Contador `[ht-a15]` = 0 casos numa sessão inteira |
| **A3** (fim de loop XMA) | Contador `[ht-a3]` = 0 quadros de fim de loop numa sessão inteira |
| A16, A11, log de VRAM | A16 teve 0 casos no próprio UFC; A11 não aparece nos logs do Ally; log de VRAM é só diagnóstico |
| A5 (caminhos longos) | Fica para a etapa 3 (pasta de saves escolhida pelo usuário), onde caminhos longos podem aparecer |

## Provas (DLL final, mesmas 6 rodadas da 9.1.0, com validação Vulkan)

- Escala 1 e 2: resolve direto 9,7 bi + 28,3 bi palavras; texturas 9,2 bi + 26,9 bi texels; constantes 2,3 GB; registradores 3,3 M; RequestRange 1,8 M; constantes dinâmicas 8,2 M; envio em 2ª thread 2.043; ZPD 12.032 pares; B57 6,3 M checagens → **0 diferenças**. **0 erros Vulkan.**
- Controles positivos (erro de propósito): todos acusaram.
- B57 ligada à força (P1): 25 M checagens, 0 diferenças; controle positivo 100%; verificador sem B57 (P3) sem falso alarme.
- Linux llvmpipe 210 s: todos os verificadores 0 diferenças, tela título ok (não é teste em GPU AMD real).

## Arquivos

- Patches: `tools/wsl/patch_fetch_unchanged_lockless.py` → `patch_fetch_unchanged_verify.py` (sobre o SDK 9.1.0). `patch_runtime_a15_a3.py` = medição de A15/A3, **não aplicado**.
- Build: `tools/wsl/build_911_cand.sh [pasta]` (originais em `/root/ht/backups/ufc-ports-911`), Linux `tools/wsl/linux_911.sh`, patch `tools/wsl/export_patch_911.sh`.
- `rexglue-modifications.patch` = 9.1.0 + B56/B57 (sha c079abd8; anterior em `_to_delete/`).
- Binários candidatos Windows: `hydro-port\cand-911-final` (exe f53f1cb1, runtime 1a589be4, GPU 06fe0508). O runtime tem o mesmo código da 9.1.0 (hash difere: build não reprodutível).
- `game-test` foi recriado (a pasta antiga sumiu na limpeza): cópia de `E:\hydrothunder\HYDRA JOGO\JOGO BASE\game_data` + `user_data` + save `B13EBABEBABEBABE`. Originais intocados.

**Ainda não empacotado** (nome da versão depende do Ian).
