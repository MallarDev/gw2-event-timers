# Waypoint Probe v0.5 (diagnóstico separado)

Esta DLL observa chamadas `send`/`sendto`, a preparação do envio (`P`) e a entrada de uma transformação de bytes (`E`) no executável do GW2 fornecido. **Não inicia viagens nem envia comandos.**

O hook `E` verifica se a entrada, antes da transformação, contém os valores de quatro bytes `30 04 00 00` (waypoint 1072, A) ou `0E 04 00 00` (waypoint 1038, B). Examina até 2048 bytes de cada chamada. Grava somente o deslocamento do primeiro achado, o tamanho examinado, horário, thread e até 12 endereços de retorno relativos ao executável. Os hooks `P`, `S` e `T` gravam somente metadados. **Nenhum payload, hash ou trecho de conteúdo é salvo pela v0.5.** Os arquivos anteriores permanecem no disco até você os apagar.

Um achado desses valores pode ser coincidência; a ausência deles não exclui que o destino seja codificado em outro formato ou passe por outro caminho. O ponto `E` é parte do processamento de saída, não a função que escolhe o waypoint. A rotina usa operações semelhantes ao gerador de bytes do RC4, mas isso não identifica o protocolo nem o comando de viagem.

## Compatibilidade

A instrumentação somente é ativada se o PE tiver timestamp `0x6ad7acf4` e tamanho de imagem `0x02c49000`, correspondentes ao `Gw2-64.exe` fornecido em 24/10/2026. Cada hook interno também confere os bytes iniciais da função antes de instalar. Uma atualização do jogo exige nova análise. A DLL usa `AF_DisableHotloading`: reinicie o jogo ao instalar, atualizar ou remover. A instrumentação experimental pode causar instabilidade; faça uma sessão curta primeiro.

## Captura

1. Substitua `WaypointProbe.dll` na pasta de addons do Nexus pela v0.5 e reinicie o jogo. Na janela do probe, confira **verificação anterior à transformação: ativa**.
2. Opcional: capture e salve 10 segundos em repouso, para comparação.
3. Clique **Capturar viagem (30 s)**. Viaje manualmente para A (`[&BDAEAAA=]`), B (`[&BA4EAAA=]`) e A novamente. Pouco antes de cada confirmação pressione `Ctrl+Alt+M` ou clique **Marcar confirmação**. Faça as três viagens dentro da janela de captura, se possível.
4. Se a marca falhar ou quiser repetir, clique **Recomeçar viagem (descartar atual)**. O contador de marcas e os registros da tentativa atual são zerados assim que as chamadas em curso terminarem; não precisa reiniciar o jogo.
5. Depois do carregamento, espere a captura parar e clique **Salvar viagem**. Você pode iniciar outra captura na mesma sessão. Cada arquivo salvo recebe um nome numerado; iniciar outra captura sem salvar descarta os registros anteriores.

Os arquivos `baseline_v5_1.tsv`, `waypoint_v5_1.tsv`, `waypoint_v5_2.tsv` etc. ficam no diretório `WaypointProbe` fornecido pelo Nexus. Colunas: `phase`, `tick_ms`, `thread`, `api` (`E`, `P`, `S`, `T`, `M`), `bytes`, `scanned_bytes`, `id_1072_offset`, `id_1038_offset`, `gw2_rva_0` a `gw2_rva_11`. Offset vazio significa que a sequência não apareceu nos bytes examinados. `scanned_bytes < bytes` indica truncamento da busca. Se houver eventos descartados, a capacidade de 16.384 registros foi atingida.

Abra ou remova colunas com um editor de texto que preserve identificadores hexadecimais. Planilhas podem converter endereços contendo `E` em notação científica.

## Build

O workflow `.github/workflows/waypoint-probe.yml` compila com MSVC x64, CMake e versões fixadas dos headers Nexus e ImGui. Executa um smoke test de carregamento e exportação da DLL. Nenhum código do Event Timers principal foi modificado.
