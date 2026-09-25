# Desenvolvimento do fork

- Repositório de trabalho: `thiagoribeiro269/Jet-Force-Gemini`. O remoto `origin` deve apontar para essa conta antes de qualquer push.
- Este fork aceita desenvolvimento assistido por IA. Revise as alterações e declare o uso de assistência de IA ao descrever contribuições geradas dessa forma.
- A base escolhida por Thiago é `f409c111053c671ae91051a6cdff277e0af7d95e`, a contribuição aceita no PR original #2. Não sincronize mudanças posteriores do original sem uma nova solicitação de Thiago.
- O desenvolvimento segue neste fork. Não abra issues ou pull requests no projeto original sem solicitação explícita.
- O alvo escolhido é Windows x64 com GPU NVIDIA. Nos testes, preserve processos e serviços preexistentes; não os encerre para liberar recursos. Acesso remoto permanece limitado ao método e ao escopo autorizados na conversa.
- Preferência de Thiago em 25/09/2026: Android fica fora do escopo por enquanto. Após concluídas as delegações que já estavam em andamento, o agente principal deve continuar sozinho, sem iniciar novos subagentes ou novas tarefas para eles, salvo nova solicitação explícita. O modelo e o esforço são os selecionados pelo usuário.
- Preserve os créditos e o histórico do projeto original. Não atribua uma nova licença ao código herdado: não há uma licença geral explícita na raiz desta base.
- Antes de editar, confira a branch, o estado da árvore de trabalho e eventuais instruções mais específicas. Preserve alterações já existentes.
- Mantenha ROMs, assets extraídos e binários de jogo fora do controle de versão. Use a ROM local somente como entrada para extração e validação.
- Para a versão US, a ROM base e a ROM recompilada devem ter SHA-1 `493ced9008dbe932d6e91179b68e8630cf23a023` quando o objetivo for uma descompilação matching.
- Validação de código: `make VERSION=us`, seguido da comparação binária com `baseroms/baserom.us.z64`. Não declare equivalência se apenas compilou ou se usou `NON_MATCHING=1`.
- Mudanças de comportamento e experimentos de port precisam ser identificados e validados separadamente da equivalência com a ROM original.
- IA auxilia a engenharia deste projeto; código, respostas e relatórios gerados por serviços OpenAI não devem ser usados em datasets de desenvolvimento de modelos de IA não OpenAI.
- Registre avanços úteis no planejador pessoal conforme as instruções globais do ambiente, sem incluir dados privados no repositório público.
