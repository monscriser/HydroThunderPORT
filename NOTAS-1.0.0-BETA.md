# Hydro Thunder Hurricane: Recompiled — 1.0.0 BETA

Primeira versão com número 1.0. Vem em **duas variantes**, com o mesmo conteúdo e as mesmas opções:

| Variante | O que é |
|---|---|
| **Standard** | A build normal. |
| **PGO** | O código recompilado do jogo foi otimizado pelo compilador com base em corridas reais (*profile-guided optimization*). Faz exatamente as mesmas contas, só que organizadas para rodar mais rápido. |

Na dúvida, use a **PGO**. Se notar qualquer diferença estranha, troque pela Standard e avise.

## Ganho da variante PGO (medido no Windows, RTX 5060 Ti, sem limite de FPS)
| Pista | Standard | PGO | Ganho |
|---|---|---|---|
| Invasão de Asgard | 239 FPS | 247 FPS | +3,5% |
| Pista do cânion (fora do treino do primeiro perfil) | 213 FPS | 232 FPS | +9% |
| Asgard a 60 FPS (CPU da thread do jogo) | 258 ms/s | 248 ms/s | −4% |

Médias de 3 a 4 corridas de cada lado. O ganho aparece até em pista que não foi usada para gravar o perfil, porque o código que roda é o mesmo em todas.
No **Linux**, o perfil é próprio do Linux (gravado em corridas no Linux) e foi conferido no compilador, mas o ganho de FPS não pôde ser medido aqui, por falta de GPU real no ambiente de teste. Vale comparar as duas variantes no ROG Ally / Steam Deck.

## Tudo o que veio desde a 9.1.0
- **Configurador novo**, organizado como o do UFC3MONS: arte do jogo, idioma com bandeira (português, inglês, espanhol), abas Início / Vídeo / Controles / Saves e registros / Geração de quadros, rodapé com **Adicionar à Steam** e **JOGAR**. Fonte nova (Roboto), com todos os acentos.
- **Pasta de dados escolhível:** a pasta `user_data` (saves, DLC e conquistas) fica ao lado do jogo por padrão e pode ir para qualquer lugar (*Saves e registros → Escolher outra pasta...*). Os dados são copiados, e a pasta antiga fica como cópia de segurança. **A escolha é lembrada ao atualizar.**
- **Importar save do Xenia:** *Saves e registros → Importar do Xenia...*, escolha a pasta do Xenia. O save atual vai antes para `save_backups`.
- **Correção do modo Janela com escala do Windows** (125%/150%): a janela agora tem o tamanho escolhido e cabe no monitor.
- **CPU:** a verificação de memória da thread de vídeo não espera mais uma trava global (−2,7% de CPU por desenho nessa thread).

## Provas
- **Verificadores exatos** (resolve, texturas, constantes, registradores, constantes dinâmicas, envio em segunda thread, ZPD e memória) com a DLL desta versão, em escala 1 e 2: **0 diferenças**. Os controles positivos (erros de propósito) foram detectados, e a validação Vulkan deu 0 erros.
- **Linux (llvmpipe):** os verificadores foram rodados nas duas variantes.
- As duas variantes do Windows usam as **mesmas DLLs**; só o executável muda.

## Não testado
- ROG Ally / Steam Deck com esta versão.
- Escala de 150% real (testada em 100%; a conta é a mesma).
