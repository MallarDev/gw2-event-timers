# Waypoint Probe v0.8 — solicitação nativa e teste direto

A v0.7 confirmou a rotina simples recebendo os IDs 1072 e 1038 dentro das respectivas confirmações, com a conexão ativa. Os botões ficaram bloqueados porque o envio manual ocorreu na thread 480 e o painel na 1072. A v0.8 encaminha o clique pela fila de mensagens da janela e executa a rotina somente na thread que corresponda à observada no envio manual.

Esta versão observa essas chamadas e correlaciona o ID com a confirmação que as acionou. Também oferece **botões experimentais de viagem direta** para A e B. É a primeira versão da sonda que pode iniciar uma viagem, por um clique explícito nesses botões. A confirmação normal do mapa continua funcionando normalmente.

## Teste

1. Feche o jogo, substitua `WaypointProbe.dll` e abra novamente. Na janela v0.8, os quatro hooks (destino, confirmação, solicitação simples e solicitação com rota) e o callback da janela devem estar ativos. A thread da janela aparece após alguma interação com o jogo. Se houver algum indisponível, informe isso.
2. Comece fora de A e clique **Iniciar / recomeçar**. A captura dura até 5 minutos.
3. Viaje **manualmente pelo mapa** para A (`[&BDAEAAA=]`), aguarde chegar, depois para B (`[&BA4EAAA=]`) e aguarde chegar. Não precisa repetir o cancelamento.
4. Confira os botões **Viajar direto para A (teste)** e **Viajar direto para B (teste)**. Se estiverem liberados, feche o mapa e teste A, aguarde chegar, depois teste B. As viagens usam o mecanismo normal de solicitação do cliente e podem ter o custo normal de waypoint. O clique cria um pedido na fila da janela. O intervalo mínimo entre viagens diretas é de 5 segundos; aguarde o término de cada loading mesmo se esse prazo já passou.
5. Se os botões permanecerem desativados, salve o registro assim mesmo. A chamada pode usar a variante com rota, a thread da janela pode diferir da thread do envio manual, ou outra condição pode impedir o teste direto.
6. Clique **Parar e salvar**. Envie `waypoint_request_v8_1.tsv` (ou o próximo número) e diga se os botões liberaram e se cada teste direto realmente levou ao destino.

Não há marcações manuais. Você pode recomeçar a captura na mesma sessão; isso limpa as validações para o teste direto. Os arquivos salvos são numerados e preservados. Apenas trocar a DLL exige reiniciar o jogo.

## Como os botões são liberados

Cada destino é validado separadamente. O botão exige que a chamada simples com aquele ID tenha sido observada dentro do tratamento de uma confirmação manual cujo ID corresponda, na captura atual. Também exige uma janela do próprio processo, callback registrado, thread da janela igual à observada no envio manual, a mesma identidade da conexão ativa, os quatro hooks instalados, captura dentro do prazo e intervalo entre cliques. IDs fora de 1072 e 1038 são recusados.

O botão enfileira uma mensagem privada do addon usando PostMessageW. O callback de janela do Nexus recebe essa mensagem na thread da janela e revalida o destino, a thread e a conexão antes de chamar a rotina simples original do jogo com o ID. Só um pedido pode ficar pendente. Um pedido expira após 5 segundos e é cancelado se a captura for encerrada ou reiniciada. Mensagens duplicadas ou com ticket/sessão incorretos não executam a rotina. Não há reprodução de pacotes capturados. A variante com rota é apenas observada; seu argumento é encaminhado sem copiar nem registrar seu conteúdo. A thread do painel pode diferir da thread do jogo; a execução acontece na thread da janela e exige que ela corresponda à do envio manual.

O retorno da rotina informa apenas que a chamada terminou. É preciso observar loading e chegada para confirmar o resultado; um servidor pode rejeitar uma solicitação. A validação no jogo desta versão ainda está pendente.

## Dados registrados

Eventos: `DESTINATION`, `CANCEL`, `ACCEPT_ENTER`, `ACCEPT_RETURN`, `REQUEST_SIMPLE`, `REQUEST_ROUTED`, `QUEUED`, `DISPATCH`, `DIRECT_ENTER`, `DIRECT_RETURN`, `QUEUE_FAILED`, `QUEUE_TIMEOUT`, `DISPATCH_REJECT`, `DIRECT_REJECT`, `READ_FAILED`, além de `STATUS`.

Colunas: `event`, `tick_ms`, `thread`, `waypoint_id`, `read_ok`, `control_id`, `event_subtype`, `setup_active`, `event_active`, `dropped`, `confirmation_id`, `correlated`, `connection_active`, `render_thread`, `simple_active`, `routed_active`, `window_thread`, `window_callback_active`, `gw2_rva_0` a `gw2_rva_5`.

`correlated=1` significa que a rotina de solicitação foi chamada dentro da confirmação, com o mesmo ID. `render_thread` e `window_thread` permitem comparar as threads do painel e da janela com a do envio. Uma execução direta deverá ter `QUEUED` na thread do painel e `DISPATCH`/`DIRECT_ENTER`/`DIRECT_RETURN` na thread do envio manual. A identidade da conexão é usada apenas em memória para validar o botão, sem gravação de ponteiro. Não são registrados payload, texto do jogo, dados de rota ou UUID. O arquivo contém somente esses campos numéricos e nomes fixos dos eventos. A capacidade é de 1024 registros.

## Compatibilidade e build

Exige o executável fornecido (timestamp `0x6ad7acf4`, imagem `0x02c49000`) e os bytes iniciais exatos de cada rotina. Atualizações podem desativar a sonda. Usa `AF_DisableHotloading`. O workflow Windows compila a DLL e executa testes de exportação, encaminhamento, leituras inválidas, captura, correlação, bloqueios dos botões e TSV. Há também teste com uma janela real do Windows e uma thread separada que enfileira pedidos: verifica execução somente ao despachar a mensagem na thread da janela, rejeição de duplicatas, sessões antigas, conexão inativa e pedidos expirados. A viagem direta experimental ainda precisa de validação no jogo.
