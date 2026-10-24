# Waypoint Probe (diagnóstico separado)

Este addon de diagnóstico do Nexus examina chamadas `send` e `sendto` feitas pelo executável principal do GW2 durante duas capturas manuais. Ele **não realiza viagem**, não envia comandos ao jogo e não registra bytes de pacotes, nomes de conta nem endereços de rede.

## Compatibilidade

A instrumentação somente é ativada se a assinatura PE (timestamp `0x6ad7acf4`, tamanho da imagem `0x02c49000`) corresponder à versão de `Gw2-64.exe` fornecida para análise em 24/10/2026. A assinatura é um controle de compatibilidade, não uma prova criptográfica de identidade. Qualquer atualização do jogo exige nova inspeção. A DLL pode ser instalada sem a instrumentação ativa; nesse caso a janela indica o motivo.

O addon usa `AF_DisableHotloading`: reinicie o jogo para instalar, atualizar ou remover a DLL. Faça primeiro um teste em uma sessão curta. Trata-se de instrumentação experimental do processo do jogo; pode causar instabilidade.

## Procedimento

1. Baixe o artefato `WaypointProbe-diagnostic-x64` da execução de Actions na branch `diagnostic/waypoint-send-trace` e extraia `WaypointProbe.dll` para a pasta de addons do Nexus. Reinicie o jogo.
2. Abra **Waypoint Probe** (a janela aparece inicialmente; se for fechada, use a seção do addon nas opções do Nexus).
3. Em um mapa, fique parado, clique **Capturar repouso (10 s)**, aguarde o estado mudar para *parado* e clique **Salvar repouso**.
4. Clique **Capturar viagem (30 s)** e faça uma viagem por waypoint manualmente, incluindo a confirmação. Depois da tela de loading, se necessário aguarde o estado *parado* e clique **Salvar viagem**.
5. Os arquivos `baseline.tsv` e `waypoint.tsv` ficam no diretório `WaypointProbe` devolvido pelo Nexus. Envie os dois arquivos para comparação. Se o contador de descartados for maior que zero, a captura atingiu o limite de 16.384 chamadas e pode estar incompleta.

Os arquivos guardam fase, tempo monotônico em milissegundos, identificador de thread, API, número de bytes e até 12 endereços de retorno relativos ao executável (`gw2_rva_*`, hexadecimal). Campos vazios significam que a chamada estava em outro módulo ou que não havia mais quadros. Há uma captura de cada tipo por sessão; para repetir, reinicie o jogo.

**Limite da investigação:** essa sonda vê apenas chamadas que atravessam as entradas importadas pelo executável principal. Uma fila assíncrona pode fazer o stack mostrar apenas a thread de rede; o comando de viagem pode passar por outro módulo ou mecanismo. Mesmo um padrão correlacionado com o clique de confirmação não comprova uma função de viagem invocável com segurança. Os logs servem para decidir o próximo ponto de inspeção, não para implementar teleporte automaticamente.

## Build

Na branch diagnóstica, o workflow `.github/workflows/waypoint-probe.yml` usa MSVC x64, CMake e as versões fixadas dos headers Nexus e ImGui. Roda um smoke test de carregamento e exportação do addon antes de disponibilizar o artefato. Nenhum código do Event Timers principal é modificado.
