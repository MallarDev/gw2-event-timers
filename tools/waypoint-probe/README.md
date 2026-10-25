# Waypoint Probe v0.6

Sonda separada do Event Timers. Observa duas rotinas do diálogo de confirmação encontrado no executável fornecido. Registra automaticamente a associação do destino (`DESTINATION`), o botão de cancelar (`CANCEL`) e a entrada/retorno do tratamento de confirmar (`ACCEPT_ENTER`, `ACCEPT_RETURN`). Os nomes dos eventos descrevem a interpretação da análise estática, a ser validada no jogo.

Esta versão não inicia viagens. O objetivo do teste é verificar se o diálogo observado é o de waypoint e se o ID lido corresponde ao destino escolhido. O retorno do tratamento de confirmar não comprova que o servidor aceitou a viagem. Anote se houve loading e chegada ao destino.

Não há hooks de rede, leitura de payload, busca de padrões em mensagens nem gravação de texto do jogo. As únicas leituras de dados do jogo são campos fixos: ponteiro para a definição do destino (usado internamente, não gravado), ID numérico do destino e dois campos numéricos do evento de botão. Também são gravados horário, thread e endereços de retorno relativos ao executável. Uma leitura inválida recebe `read_ok=0`.

## Instalação e teste

1. Feche o jogo, substitua a DLL anterior por `WaypointProbe.dll` e abra o jogo. Confira a janela **Waypoint Probe v0.6** e os estados **Destino: ativo | confirmação: ativa**. Se algum estiver indisponível, informe isso; não repita viagens para produzir um arquivo vazio.
2. Comece em um local diferente de A. Clique **Iniciar / recomeçar**. Há até 3 minutos para o teste; a janela pode ser fechada e reaberta pelas opções do addon sem parar a captura.
3. Abra a confirmação para A (`[&BDAEAAA=]`) e **cancele pelo botão do diálogo**.
4. Abra A novamente, **confirme a viagem** e aguarde chegar.
5. Abra B (`[&BA4EAAA=]`), **confirme a viagem** e aguarde chegar.
6. Clique **Parar e salvar**. Envie `waypoint_ui_v6_1.tsv` (ou o próximo número) e diga se as duas viagens aconteceram. O caminho aparece na janela; o diretório é o `WaypointProbe` dos addons, como nas versões anteriores.

Não é necessário usar Ctrl+Alt+M nem alinhar uma marca com o clique. Para repetir, use **Iniciar / recomeçar** na mesma sessão. A captura anterior é descartada; os arquivos já salvos são preservados e numerados. Só a troca de DLL exige reiniciar o jogo.

## O que o resultado responde

A sequência esperada é `DESTINATION 1072`, `CANCEL 1072`, `DESTINATION 1072`, `ACCEPT_ENTER 1072`, `ACCEPT_RETURN 1072`, `DESTINATION 1038`, `ACCEPT_ENTER 1038`, `ACCEPT_RETURN 1038`. Se aparecer, teremos identificado o caminho de confirmação e o campo do destino em execução. Isso permite concentrar a investigação na chamada de ação feita dentro desse tratamento, em vez de inferir viagens por tamanho de pacote.

Se não aparecer, a sonda informa se os hooks instalaram, se as leituras falharam ou se outro caminho foi usado. Esse resultado também pode descartar a hipótese do diálogo.

## TSV

Colunas: `event`, `tick_ms`, `thread`, `waypoint_id`, `read_ok`, `control_id`, `event_subtype`, `setup_active`, `event_active`, `dropped`, `gw2_rva_0` a `gw2_rva_5`.

A linha `STATUS` indica os hooks ativos e a quantidade descartada; os campos de destino e botão dela são zero. Os endereços têm prefixo `0x` para reduzir conversões indevidas por planilhas. A capacidade é de 1024 eventos. Nenhuma coluna de payload existe nesta versão.

## Compatibilidade e limites

A sonda exige timestamp PE `0x6ad7acf4`, imagem `0x02c49000` e os bytes iniciais exatos de cada rotina do executável fornecido. Atualizações podem desativar a captura. Usa `AF_DisableHotloading`. Os hooks são experimentais e precisam ser validados no jogo; os testes automatizados cobrem o carregamento da DLL, encaminhamento dos argumentos, leituras inválidas, reinício da captura, capacidade e formato do arquivo.

## Build

O workflow `.github/workflows/waypoint-probe.yml` compila com MSVC x64 no Windows e executa os testes. Os headers Nexus e o ImGui têm commits fixados no CMake. O addon principal não foi modificado.
