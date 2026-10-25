# Waypoint Probe v0.7 — solicitação nativa e teste direto

A v0.6 confirmou no jogo a associação do destino e os eventos de cancelar/confirmar para A (1072) e B (1038), com leituras válidas e sem descartes. A investigação seguinte localizou duas rotinas de solicitação de viagem: a simples recebe o ID; a outra recebe o ID e uma rota.

Esta versão observa essas chamadas e correlaciona o ID com a confirmação que as acionou. Também oferece **botões experimentais de viagem direta** para A e B. É a primeira versão da sonda que pode iniciar uma viagem, por um clique explícito nesses botões. A confirmação normal do mapa continua funcionando normalmente.

## Teste

1. Feche o jogo, substitua `WaypointProbe.dll` e abra novamente. Na janela v0.7, os quatro estados (destino, confirmação, solicitação simples e solicitação com rota) devem estar ativos. Se houver algum indisponível, informe isso.
2. Comece fora de A e clique **Iniciar / recomeçar**. A captura dura até 5 minutos.
3. Viaje **manualmente pelo mapa** para A (`[&BDAEAAA=]`), aguarde chegar, depois para B (`[&BA4EAAA=]`) e aguarde chegar. Não precisa repetir o cancelamento.
4. Confira os botões **Viajar direto para A (teste)** e **Viajar direto para B (teste)**. Se estiverem liberados, feche o mapa e teste A, aguarde chegar, depois teste B. As viagens usam o mecanismo normal de solicitação do cliente e podem ter o custo normal de waypoint. O intervalo mínimo entre cliques é de 5 segundos; aguarde o término de cada loading mesmo se esse prazo já passou.
5. Se os botões permanecerem desativados, salve o registro assim mesmo. A chamada pode usar a variante com rota, a thread pode ser diferente, ou outra condição pode impedir o teste direto.
6. Clique **Parar e salvar**. Envie `waypoint_request_v7_1.tsv` (ou o próximo número) e diga se os botões liberaram e se cada teste direto realmente levou ao destino.

Não há marcações manuais. Você pode recomeçar a captura na mesma sessão; isso limpa as validações para o teste direto. Os arquivos salvos são numerados e preservados. Apenas trocar a DLL exige reiniciar o jogo.

## Como os botões são liberados

Cada destino é validado separadamente. O botão exige que a chamada simples com aquele ID tenha sido observada dentro do tratamento de uma confirmação manual cujo ID corresponda, na captura atual. Também exige o mesmo thread ID do envio manual, a mesma identidade da conexão ativa, os quatro hooks instalados, captura dentro do prazo e intervalo entre cliques. IDs fora de 1072 e 1038 são recusados.

O botão chama a rotina simples original do jogo com o ID. Não há reprodução de pacotes capturados. A variante com rota é apenas observada; seu argumento é encaminhado sem copiar nem registrar seu conteúdo. Se a thread do painel diferir da thread da chamada manual, o botão fica bloqueado.

O retorno da rotina informa apenas que a chamada terminou. É preciso observar loading e chegada para confirmar o resultado; um servidor pode rejeitar uma solicitação. A validação no jogo desta versão ainda está pendente.

## Dados registrados

Eventos: `DESTINATION`, `CANCEL`, `ACCEPT_ENTER`, `ACCEPT_RETURN`, `REQUEST_SIMPLE`, `REQUEST_ROUTED`, `DIRECT_ENTER`, `DIRECT_RETURN`, `READ_FAILED`, além de `STATUS`.

Colunas: `event`, `tick_ms`, `thread`, `waypoint_id`, `read_ok`, `control_id`, `event_subtype`, `setup_active`, `event_active`, `dropped`, `confirmation_id`, `correlated`, `connection_active`, `render_thread`, `simple_active`, `routed_active`, `gw2_rva_0` a `gw2_rva_5`.

`correlated=1` significa que a rotina de solicitação foi chamada dentro da confirmação, com o mesmo ID. `render_thread` permite comparar a thread do painel com a do envio. A identidade da conexão é usada apenas em memória para validar o botão, sem gravação de ponteiro. Não são registrados payload, texto do jogo, dados de rota ou UUID. O arquivo contém somente esses campos numéricos e nomes fixos dos eventos. A capacidade é de 1024 registros.

## Compatibilidade e build

Exige o executável fornecido (timestamp `0x6ad7acf4`, imagem `0x02c49000`) e os bytes iniciais exatos de cada rotina. Atualizações podem desativar a sonda. Usa `AF_DisableHotloading`. O workflow Windows compila a DLL e executa testes de exportação, encaminhamento, leituras inválidas, captura, correlação, bloqueios dos botões e TSV. Os hooks e a viagem direta experimental precisam de validação no jogo.
