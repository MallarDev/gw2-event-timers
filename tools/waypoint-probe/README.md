# Waypoint Probe (diagnóstico separado)

Este addon de diagnóstico do Nexus examina chamadas `send` e `sendto` feitas pelo executável principal do GW2 durante duas capturas manuais. A versão 0.2 também observa um ponto interno imediatamente anterior ao repasse de um bloco à camada de envio e permite marcar o instante próximo da confirmação. Ele **não realiza viagem**, não envia comandos ao jogo e não registra bytes de pacotes, nomes de conta nem endereços de rede.

## Compatibilidade

A instrumentação somente é ativada se a assinatura PE (timestamp `0x6ad7acf4`, tamanho da imagem `0x02c49000`) corresponder à versão de `Gw2-64.exe` fornecida para análise em 24/10/2026. A assinatura é um controle de compatibilidade, não uma prova criptográfica de identidade. Qualquer atualização do jogo exige nova inspeção. A DLL pode ser instalada sem a instrumentação ativa; nesse caso a janela indica o motivo.

O hook interno só é ativado se os bytes da função, na posição examinada do executável fornecido, também corresponderem exatamente. Ele usa a interface de hooks do Nexus. Se a instrumentação interna falhar (inclusive sob Wine/Proton), `send`/`sendto` continuam disponíveis, e a janela mostra **preparação de envio: indisponível**. O addon usa `AF_DisableHotloading`: reinicie o jogo para instalar, atualizar ou remover a DLL. Faça primeiro um teste em uma sessão curta. Trata-se de instrumentação experimental do processo do jogo; pode causar instabilidade.

## Procedimento

1. Baixe o artefato `WaypointProbe-diagnostic-x64` da execução de Actions na branch `diagnostic/waypoint-send-trace` e extraia `WaypointProbe.dll` para a pasta de addons do Nexus. Reinicie o jogo.
2. Abra **Waypoint Probe** (a janela aparece inicialmente; se for fechada, use a seção do addon nas opções do Nexus).
3. Em um mapa, fique parado, clique **Capturar repouso (10 s)**, aguarde o estado mudar para *parado* e clique **Salvar repouso**.
4. Clique **Capturar viagem (30 s)** e abra o mapa. Imediatamente **antes de clicar em Viajar** na confirmação do jogo, pressione **Ctrl+Alt+M** (configurável nos binds do Nexus) ou clique **Marcar confirmação** na janela da sonda. Em seguida, confirme a viagem normalmente. A marca será registrada com o relógio monotônico usado para as chamadas de rede.
5. Depois da tela de loading, aguarde o estado *parado* e clique **Salvar viagem**. Confira se a janela indica ao menos uma marca.
6. Os arquivos `baseline_v2.tsv` e `waypoint_v2.tsv` ficam no diretório `WaypointProbe` devolvido pelo Nexus. Envie os dois arquivos para comparação. Se o contador de descartados for maior que zero, a captura atingiu o limite de 16.384 eventos e pode estar incompleta.

Os arquivos guardam fase, tempo monotônico em milissegundos, identificador de thread, tipo do evento, tamanho e até 12 endereços de retorno relativos ao executável (`gw2_rva_*`, hexadecimal). A coluna `api` usa `S` para `send`, `T` para `sendto`, `P` para o ponto anterior ao repasse interno e `M` para a marca manual. Campos vazios indicam quadros fora do executável ou ausência de mais quadros; na linha `M`, os endereços ficam vazios. Há uma captura de cada tipo por sessão; para repetir, reinicie o jogo.

**Limite da investigação:** o evento `P` está mais próximo da preparação para o envio que `S`, mas ainda pode estar após uma fila assíncrona. O hook interno cobre apenas um caminho observado no binário fornecido; o comando de viagem pode passar por outro módulo ou mecanismo. A marca manual é aproximada, pois a tecla ou botão é acionada antes do clique no jogo. Mesmo um padrão correlacionado com a confirmação não comprova uma função de viagem invocável com segurança. Os logs servem para decidir o próximo ponto de inspeção.

## Build

Na branch diagnóstica, o workflow `.github/workflows/waypoint-probe.yml` usa MSVC x64, CMake e versões fixadas dos headers Nexus e ImGui. Roda um smoke test de carregamento e exportação do addon antes de disponibilizar o artefato. Nenhum código do Event Timers principal é modificado.
