# Waypoint Probe (diagnóstico separado)

Este addon de diagnóstico do Nexus examina chamadas `send` e `sendto` feitas pelo executável principal do GW2 durante duas capturas manuais. Também observa um ponto interno imediatamente anterior ao repasse de um bloco à camada de envio e permite marcar o instante próximo da confirmação. A versão 0.3 registra localmente até 2048 bytes de cada bloco `P`, `S` e `T`. Ele **não realiza viagem** nem envia comandos ao jogo.

**Privacidade:** os arquivos TSV agora podem conter dados de sessão, autenticação, conta ou outros dados sensíveis. O addon não transmite os arquivos. Revise-os localmente e envie somente as linhas que quiser compartilhar, depois de remover dados sensíveis. `P` e `S` podem conter os mesmos bytes ou conteúdos diferentes; o formato também pode ser binário, comprimido ou cifrado. Uma visualização ASCII legível não é garantida.

## Compatibilidade

A instrumentação somente é ativada se a assinatura PE (timestamp `0x6ad7acf4`, tamanho da imagem `0x02c49000`) corresponder à versão de `Gw2-64.exe` fornecida para análise em 24/10/2026. A assinatura é um controle de compatibilidade, não uma prova criptográfica de identidade. Qualquer atualização do jogo exige nova inspeção. A DLL pode ser instalada sem a instrumentação ativa; nesse caso a janela indica o motivo.

O hook interno só é ativado se os bytes da função, na posição examinada do executável fornecido, também corresponderem exatamente. Ele usa a interface de hooks do Nexus. Se a instrumentação interna falhar (inclusive sob Wine/Proton), `send`/`sendto` continuam disponíveis, e a janela mostra **preparação de envio: indisponível**. O addon usa `AF_DisableHotloading`: reinicie o jogo para instalar, atualizar ou remover a DLL. Faça primeiro um teste em uma sessão curta. Trata-se de instrumentação experimental do processo do jogo; pode causar instabilidade.

## Procedimento

1. Baixe o artefato `WaypointProbe-diagnostic-x64` da execução de Actions na branch `diagnostic/waypoint-send-trace` e extraia `WaypointProbe.dll` para a pasta de addons do Nexus. Reinicie o jogo.
2. Abra **Waypoint Probe** (a janela aparece inicialmente; se for fechada, use a seção do addon nas opções do Nexus).
3. Em um mapa, fique parado, clique **Capturar repouso (10 s)**, aguarde o estado mudar para *parado* e clique **Salvar repouso**.
4. Clique **Capturar viagem (30 s)** e abra o mapa. Imediatamente **antes de clicar em Viajar** na confirmação do jogo, pressione **Ctrl+Alt+M** (configurável nos binds do Nexus) ou clique **Marcar confirmação** na janela da sonda. Em seguida, confirme a viagem normalmente. A marca será registrada com o relógio monotônico usado para as chamadas de rede.
5. Depois da tela de loading, aguarde o estado *parado* e clique **Salvar viagem**. Confira se a janela indica ao menos uma marca.
6. Os arquivos `baseline_v3.tsv` e `waypoint_v3.tsv` ficam no diretório `WaypointProbe` devolvido pelo Nexus. **Não envie os arquivos completos antes de revisar o conteúdo.** Se o contador de descartados for maior que zero, a captura atingiu o limite de 16.384 eventos e pode estar incompleta.

Os arquivos guardam fase, tempo monotônico em milissegundos, identificador de thread, tipo do evento, tamanho, `captured_bytes`, `payload_hex`, `payload_ascii` e até 12 endereços de retorno relativos ao executável (`gw2_rva_*`, hexadecimal). `bytes` é o tamanho original; se for maior que 2048, `captured_bytes` será menor e o conteúdo foi truncado. `payload_hex` contém todos os bytes capturados e `payload_ascii` troca bytes não imprimíveis por `.`. A coluna `api` usa `S` para `send`, `T` para `sendto`, `P` para o ponto anterior ao repasse interno e `M` para a marca manual. Os eventos `M` têm payload e endereços vazios. Há uma captura de cada tipo por sessão; para repetir, reinicie o jogo.

Para começar a análise local, procure as linhas `M` e compare os eventos imediatamente seguintes. Nas duas viagens de referência, os envios de 38 e 45 bytes ocorreram cerca de 0,6 segundo após as marcas. Os IDs dos waypoints eram 1072 (`30 04 00 00` em little endian) e 1038 (`0E 04 00 00`); essas sequências podem não aparecer literalmente no payload. Examine `P` e `S` separadamente. Uma linha de texto legível pode conter valores de sessão: não a compartilhe sem revisão.

**Limite da investigação:** o evento `P` está mais próximo da preparação para o envio que `S`, mas ainda pode estar após uma fila assíncrona. O hook interno cobre apenas um caminho observado no binário fornecido; o comando de viagem pode passar por outro módulo ou mecanismo. A marca manual é aproximada, pois a tecla ou botão é acionada antes do clique no jogo. Mesmo um padrão correlacionado com a confirmação não comprova uma função de viagem invocável com segurança. Os logs servem para decidir o próximo ponto de inspeção.

## Build

Na branch diagnóstica, o workflow `.github/workflows/waypoint-probe.yml` usa MSVC x64, CMake e versões fixadas dos headers Nexus e ImGui. Roda um smoke test de carregamento e exportação do addon antes de disponibilizar o artefato. Nenhum código do Event Timers principal é modificado.
