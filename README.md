# Integração de Dispositivo Embarcado com Backend Assíncrono

> **Link da simulação no Wokwi:**  [clique aqui](https://wokwi.com/projects/459149394234148865)
 
> **Link do vídeo da solução em funcionamento:** [clique aqui](https://drive.google.com/file/d/16Rxgcy1fvFU2Zn1rf5i8SRYUP4oo9Y8H/view?usp=sharing)

> **Link do repositório/back-end da Atividade 1:** [clique aqui](https://github.com/ceciliagalvaoo/Ponderada-Analise-Telemetria-Assincrona)

## 1. Visão Geral

Este projeto apresenta a integração de um **dispositivo embarcado baseado no Raspberry Pi Pico W** com um **backend distribuído e assíncrono**, construído anteriormente na Atividade 1. O objetivo central foi validar, em um cenário próximo de uma aplicação real de IoT, o envio de telemetria gerada por sensores embarcados até sua persistência em banco de dados.

O firmware foi desenvolvido em **C com Pico SDK**, e a validação foi realizada no **Wokwi**, ambiente de simulação que permitiu reproduzir o comportamento do microcontrolador, do botão e do potenciômetro sem a necessidade de hardware físico. Para permitir a comunicação entre a simulação e o backend local, foi utilizado o **ngrok**, expondo a API por meio de uma URL pública acessível externamente.

O projeto cobre, de ponta a ponta:

- leitura de sensor analógico via ADC
- leitura de sensor digital via GPIO com interrupção
- conexão Wi-Fi com o Pico W
- resolução DNS assíncrona
- envio HTTP com payload em JSON
- fila local para tolerância a falhas
- integração com backend, mensageria e persistência

## 2. Objetivo da Atividade

O propósito desta atividade foi implementar um firmware embarcado capaz de:

- capturar dados de sensores físicos ou simulados
- organizar essas leituras em um formato estruturado
- transmitir os dados para um backend HTTP
- manter robustez mínima diante de falhas de rede
- validar o fluxo completo até a persistência da informação

Mais do que apenas realizar leituras locais, o desafio consistiu em integrar o dispositivo a uma arquitetura distribuída já existente, respeitando restrições reais de conectividade, simulação e comunicação embarcada.

## 3. Contexto da Integração com a Atividade 1

Na atividade anterior, foi desenvolvido um backend desacoplado para ingestão e processamento de telemetria. Esse backend é composto por:

- uma API HTTP responsável por receber os dados
- uma fila com **RabbitMQ** para desacoplamento entre ingestão e processamento
- um middleware assíncrono para consumo das mensagens
- um banco **PostgreSQL** para persistência final

Com isso, a arquitetura validada nesta atividade passou a ser:

```text
Dispositivo (Pico W) -> HTTP -> Backend -> RabbitMQ -> Middleware -> PostgreSQL
```

Essa integração é relevante porque reproduz um padrão amplamente utilizado em soluções de IoT e sistemas industriais: dispositivos de borda gerando eventos e serviços centrais processando essas informações de forma escalável.

## 4. Ambiente de Desenvolvimento e Simulação

Devido à indisponibilidade de hardware físico no momento do desenvolvimento, a solução foi construída e validada no **Wokwi**, plataforma online de simulação de sistemas embarcados. A escolha foi adequada porque a ferramenta oferece:

- simulação do Raspberry Pi Pico W
- conexão de periféricos como botão e potenciômetro
- execução do firmware compilado com Pico SDK
- monitor serial para análise dos logs de execução

Essa abordagem foi importante não apenas para viabilizar o projeto, mas também para acelerar testes de comportamento, depuração de leitura dos sensores e validação das etapas de comunicação.

### 4.1 Limitações do ambiente

Durante o desenvolvimento, foi identificado um ponto crítico: o Wokwi não acessa `localhost` diretamente. Isso significa que, mesmo com o backend rodando na máquina local, o microcontrolador simulado não consegue se conectar a ele sem uma camada intermediária de exposição.

Além disso, recursos como `host.wokwi.internal` podem depender de funcionalidades não disponíveis no plano gratuito, o que exigiu uma solução alternativa.


## 5. Componentes do Circuito

O circuito implementado no projeto simula dois tipos distintos de entrada:

### 5.1 Potenciômetro

O potenciômetro representa um sensor analógico. Seu valor é lido pelo conversor analógico-digital do Pico W e transformado em um percentual entre `0` e `100`, permitindo simular grandezas contínuas, como nível, posição, intensidade ou temperatura normalizada.

### 5.2 Botão

O botão representa um sensor digital orientado a evento. Diferentemente do potenciômetro, ele não demanda leitura periódica constante. Seu estado é tratado por interrupção, permitindo capturar mudanças de forma imediata e eficiente.

### 5.3 Mapeamento de pinos

| Componente | Pino no Pico W | Finalidade |
| --- | --- | --- |
| Potenciômetro | `GP26` | entrada analógica (`ADC0`) |
| Botão | `GP14` | entrada digital com interrupção |

Esse mapeamento está refletido no circuito descrito em `diagram.json` e no firmware em `main.c`.


## 6. Arquitetura de Software do Firmware

O firmware foi projetado com separação clara entre aquisição de dados, conectividade e transmissão. Em vez de uma lógica monolítica, o código foi organizado em blocos funcionais:

- inicialização de periféricos
- leitura dos sensores
- fila local de telemetria
- conexão Wi-Fi
- resolução DNS
- envio HTTP
- máquina de estados principal

Essa divisão reduz acoplamento, melhora a legibilidade e facilita a depuração.

### 6.1 Estrutura de dados

Cada leitura é representada por uma estrutura `telemetry_t`, contendo:

- identificador do dispositivo
- timestamp
- tipo do sensor
- tipo de leitura
- valor numérico

Em seguida, as mensagens são armazenadas em uma fila circular `telemetry_queue_t`, permitindo retenção temporária antes do envio.

### 6.2 Máquina de estados

O fluxo principal do sistema utiliza os seguintes estados:

- `STATE_BOOT`
- `STATE_WIFI_CONNECTING`
- `STATE_WIFI_READY`
- `STATE_SEND_PENDING`
- `STATE_RETRY_WAIT`

Essa modelagem foi importante para evitar um firmware excessivamente bloqueante e para organizar a transição entre inicialização, conexão e tentativas de reenvio.

## 7. Estratégia de Coleta de Dados

Um ponto importante do projeto foi tratar sensores diferentes de forma diferente, respeitando sua natureza.

### 7.1 Leitura analógica periódica

O potenciômetro é lido periodicamente por meio de um **timer repetitivo** configurado para disparar a cada `5 segundos`. Quando o timer dispara, uma flag (`adc_read_flag`) é ativada e o valor do sensor é lido no laço principal.

Além disso, a leitura analógica utiliza média de múltiplas amostras para reduzir ruído:

- número de amostras: `8`
- conversão final: percentual de `0` a `100`

Essa abordagem produz leituras mais estáveis e simula um comportamento adequado para sensores contínuos.

### 7.2 Leitura digital por evento

O botão é tratado por interrupção em GPIO. Sempre que ocorre uma borda de subida ou descida, a ISR registra a mudança de estado e sinaliza que um evento ocorreu.

Para evitar leituras falsas causadas por bouncing mecânico, foi implementado **debounce por software** com janela de `200 ms`.

Essa decisão reduz tráfego desnecessário e melhora a precisão dos eventos enviados.

## 8. Fila Local e Estratégia de Confiabilidade

Em sistemas embarcados conectados, a comunicação de rede pode falhar por diversos motivos: perda temporária de conectividade, falhas de DNS, indisponibilidade do servidor ou timeout na conexão. Para reduzir o risco de perda de telemetria, foi implementada uma **fila circular local**.

### 8.1 Papel da fila

A fila local tem três objetivos principais:

- armazenar leituras antes do envio
- desacoplar coleta e transmissão
- permitir nova tentativa em caso de falha

### 8.2 Capacidade

O firmware define:

```c
#define QUEUE_SIZE 16
```

Isso significa que até `16` mensagens podem permanecer armazenadas localmente antes do descarte de novas leituras, o que oferece uma tolerância básica a falhas transitórias.

### 8.3 Política de retry

Quando o envio falha:

- a mensagem é reencolada
- o sistema entra em `STATE_RETRY_WAIT`
- uma nova tentativa é feita após um intervalo

Essa estratégia simples já demonstra um comportamento importante em sistemas reais: **não perder dados imediatamente diante de uma falha momentânea**.

## 9. Conectividade Wi-Fi

A conexão Wi-Fi foi implementada com a biblioteca `cyw43_arch`, padrão para o Pico W. O firmware utiliza a rede disponibilizada pelo Wokwi:

```c
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
```

O processo de conexão ocorre no início da execução, durante o estado `STATE_WIFI_CONNECTING`.

### 9.1 Decisão de inicialização

Um cuidado importante no projeto foi evitar reinicializações desnecessárias do módulo Wi-Fi. Em cenários embarcados, reinicializar a pilha de conectividade repetidamente pode causar comportamento instável, especialmente quando combinado com tentativas frequentes de reconexão.

Por isso, a arquitetura separa:

- inicialização da stack Wi-Fi
- tentativa de conexão
- reenvio de mensagens em caso de falha

Essa separação torna o firmware mais previsível.


## 10. Comunicação com o Backend

Uma vez conectado à rede, o dispositivo envia telemetria para o backend usando **HTTP POST**.

As definições principais do endpoint estão no firmware:

```c
#define API_HOST "interramal-marquita-heavenless.ngrok-free.dev"
#define API_PORT 80
#define API_PATH "/telemetry"
```

Cada leitura é convertida para JSON antes da transmissão.

### 10.1 Exemplo de payload

```json
{
  "device_id": "pico-w-01",
  "timestamp": "2026-03-21T12:00:00Z",
  "sensor_type": "potentiometer",
  "reading_type": "analog",
  "value": 73.42
}
```

### 10.2 Cabeçalhos enviados

O firmware monta manualmente a requisição HTTP, incluindo:

- método `POST`
- `Host`
- `Content-Type: application/json`
- `Content-Length`
- `Connection: close`

Isso mostra uma implementação de baixo nível, coerente com o contexto embarcado e com o uso direto da pilha TCP/IP.

## 11. Resolução DNS Assíncrona

Um dos desafios mais relevantes do projeto esteve na resolução DNS com `lwIP`.

### 11.1 Problema encontrado

Ao chamar `dns_gethostbyname`, a biblioteca pode retornar:

- `ERR_OK`, quando o endereço já está disponível imediatamente
- `ERR_INPROGRESS`, quando a resolução continuará de forma assíncrona

Se o segundo caso for tratado como falha, o firmware conclui incorretamente que houve erro, mesmo quando a resolução está apenas em andamento.

### 11.2 Solução adotada

Foi implementado um callback de DNS para:

- receber o endereço resolvido
- registrar sucesso ou falha
- sinalizar conclusão da operação

Enquanto o callback não conclui, o firmware continua chamando `cyw43_arch_poll()` e aguardando por um intervalo controlado.

Essa solução é importante porque demonstra um aspecto clássico de sistemas embarcados e redes: **operações aparentemente simples podem ter comportamento assíncrono e precisam ser tratadas explicitamente**.

## 12. Uso do ngrok para Exposição do Backend

Como o backend estava sendo executado localmente, foi necessário torná-lo acessível ao dispositivo simulado no Wokwi, que não possui acesso direto ao `localhost`. Para contornar essa limitação, foi utilizado o **ngrok**, uma ferramenta que permite expor serviços locais por meio de uma URL pública.

O ngrok cria um túnel entre a internet e a máquina local, redirecionando requisições externas para uma porta específica em execução local.

### 12.1 Instalação e configuração

O ngrok pode ser obtido diretamente pelo site oficial:

https://ngrok.com/download

Após o download, é necessário criar uma conta na plataforma e configurar o **authtoken**, que associa o cliente local ao usuário.

A configuração é realizada com o comando:

```bash
ngrok config add-authtoken <SEU_TOKEN>
```

Esse passo é necessário para habilitar o uso da ferramenta sem restrições básicas de sessão.

### 12.2 Comando utilizado

```bash
ngrok http --scheme=http 8080
```

Esse comando expõe o serviço em execução em `localhost:8080` por meio de uma URL pública utilizando HTTP.

### 12.3 Funcionamento

Após a execução, o ngrok gera uma URL pública do tipo:

```text
http://<id>.ngrok-free.dev
```

Essa URL atua como intermediária entre o dispositivo e o backend:

```text
Dispositivo → Internet → ngrok → localhost:8080
```

Dessa forma, as requisições enviadas pelo Pico W são encaminhadas corretamente para o backend local, permitindo a validação completa da integração.

### 12.4 Observações

* O terminal do ngrok deve permanecer ativo durante toda a execução
* A URL gerada é temporária e pode mudar a cada execução
* Foi utilizado HTTP (porta 80) para compatibilidade com o firmware embarcado

### 12.5 Motivo para uso de HTTP

A comunicação foi realizada via HTTP, pois o firmware desenvolvido com Pico SDK e lwIP não possui suporte nativo a TLS/HTTPS.

A utilização de HTTPS exigiria a integração de bibliotecas adicionais de criptografia (como mbedTLS), aumentando significativamente a complexidade do firmware e o consumo de recursos do microcontrolador.

Considerando que o objetivo da atividade é validar a integração ponta a ponta da telemetria, optou-se pelo uso de HTTP, garantindo simplicidade e viabilidade no ambiente proposto.

Embora essa abordagem não ofereça criptografia no transporte dos dados, ela é adequada para fins acadêmicos. Em um cenário de produção, o uso de HTTPS seria recomendado.

## 13. Validação End-to-End

A validação do projeto não se restringe ao envio da requisição pelo microcontrolador. O resultado esperado envolve o encadeamento completo do pipeline:

1. o Pico W coleta a leitura
2. o firmware gera o payload em JSON
3. a API recebe a telemetria
4. a mensagem é publicada no RabbitMQ
5. o middleware processa a fila
6. os dados são persistidos no PostgreSQL

Essa visão é importante porque demonstra que o valor do projeto está na integração entre camadas heterogêneas, e não apenas na leitura local do sensor.

### 13.1 Evidências esperadas

Durante a validação, devem ser observados:

- logs seriais do Pico W indicando leitura e envio
- resposta positiva do backend
- processamento das mensagens pelo middleware
- registros persistidos no banco

## 14. Testes de Carga com k6

Para complementar a validação funcional, foram executados testes de carga no backend da Atividade 1 com o k6, usando o endpoint `POST /telemetry`.

O objetivo foi avaliar dois cenários:

- comportamento em níveis progressivos de carga
- limite operacional sob estresse, com critério de parada por SLO

### 14.1 Cenário 1: teste por níveis

No primeiro script, a carga foi aplicada em níveis controlados (`5`, `20` e `50` VUs), com rampa curta e validações de qualidade de serviço.

Critérios utilizados:

- `http_req_failed < 5%`
- `http_req_duration p(95) < 1000 ms`
- status HTTP `202`

Resultado:

- o cenário cumpriu o objetivo de validar o comportamento inicial em baixa e média carga
- não foram observadas falhas relevantes
- o endpoint respondeu de forma consistente com enfileiramento bem-sucedido

Esse teste foi importante como baseline para confirmar estabilidade antes dos cenários de estresse.

### 14.2 Cenário 2: teste de estresse e refino do limite

No segundo script, a estratégia foi elevar progressivamente o número de VUs para identificar o ponto de saturação.

Critérios de parada adotados (SLO):

- `http_req_failed < 1%`
- `http_req_duration p(95) < 1000 ms`

Principais execuções e resultados:

1. Execução até `2000` VUs:
  - `http_req_failed = 0%`
  - `checks = 100%`
  - `p(95) = 889.47 ms`
  - conclusão: cenário ainda saudável para o SLO definido.

2. Execução extrema (`6000` a `9000` VUs):
  - falhas de conexão (`connect refused`) durante o teste
  - `http_req_failed = 2.48%`
  - `p(95) = 12.9 s`
  - conclusão: saturação severa, com violação de latência e disponibilidade.

3. Execução de refino (até `3900` VUs):
  - `http_req_failed = 0%`
  - `checks = 100%`
  - `p(95) = 3.57 s`
  - conclusão: sem queda de disponibilidade, porém com degradação forte de desempenho.

### 14.3 Interpretação dos resultados

Os resultados mostram dois limites diferentes:

- limite de disponibilidade: mais alto (o serviço ainda responde `202` em cargas elevadas)
- limite de desempenho (SLO de latência): mais baixo

Assim, considerando o critério de qualidade adotado (`p95 < 1s`), o backend deixa de ser considerado saudável em cargas acima da faixa validada com `2000` VUs.

Em outras palavras, o gargalo observado foi primeiro de tempo de resposta e só depois, em carga extrema, de disponibilidade.

### 14.4 Observação metodológica

Os testes foram executados com VUs em loop (sem `sleep` no cenário de estresse), portanto a vazão (`req/s`) variou conforme latência e capacidade do ambiente. Assim, o número de VUs não equivale diretamente ao mesmo número de requisições por segundo.

Para controle estrito de taxa de chegada, pode-se usar, em trabalhos futuros, o executor `constant-arrival-rate`.

### 14.5 Scripts e outputs dos testes

Todos os scripts de carga utilizados na Atividade 1 e seus respectivos outputs (summaries) estão organizados na pasta `test-load-atv1` deste repositório.

Essa organização facilita a reprodução dos experimentos e a rastreabilidade dos resultados apresentados nesta seção.

## 15. Como Executar o Projeto

### 15.1 Subir o backend

No projeto da Atividade 1:

```bash
docker compose up --build -d
```

### 15.2 Expor o backend

```bash
ngrok http --scheme=http 8080
```

Depois disso:

- copie a URL pública gerada
- extraia o domínio
- atualize `API_HOST` em `main.c` 

### 15.3 Executar a simulação

- abra o projeto no Wokwi
- inicie a simulação
- acompanhe o Serial Monitor

### 15.4 Verificar o processamento

Exemplos de comandos úteis no backend:

```bash
docker compose logs -f middleware
```

```bash
docker exec db psql -U postgres -d telemetrydb -c "SELECT * FROM telemetry ORDER BY id DESC;"
```

## 16. Principais Aprendizados

O projeto evidenciou diversos aprendizados práticos relevantes:

- sensores diferentes exigem estratégias de leitura diferentes
- interrupções são mais eficientes para eventos discretos
- conectividade em sistemas embarcados exige cuidado com estados e retry
- resolução DNS pode introduzir comportamento assíncrono não trivial
- ambientes de simulação impõem restrições que influenciam a arquitetura
- integrar firmware com backend distribuído exige validar todas as camadas

Esses pontos tornam a atividade particularmente valiosa do ponto de vista de engenharia, pois aproximam o desenvolvimento acadêmico de problemas encontrados em sistemas reais.

## 17. Trabalhos Futuros

Como possíveis extensões do projeto, destacam-se:

- suporte a HTTPS para comunicação segura
- uso de NTP para timestamp real
- expansão da fila local com política mais robusta de descarte
- confirmação explícita de entrega com análise de código de resposta HTTP
- inclusão de novos sensores
- execução em hardware físico para validação eletroeletrônica completa

## 18. Conclusão

Esta atividade demonstrou com sucesso a integração entre um sistema embarcado e um backend assíncrono distribuído. O dispositivo simulado foi capaz de coletar dados de sensores, organizar essas leituras em mensagens estruturadas, enviá-las para um endpoint HTTP e participar de um fluxo maior de processamento e persistência.

Além da implementação funcional, o projeto revelou desafios técnicos concretos, como limitação do ambiente de simulação, necessidade de exposição externa do backend, tratamento correto de DNS assíncrono e adoção de mecanismos mínimos de confiabilidade no firmware.

Como resultado, a solução final não apenas cumpre os requisitos da atividade, mas também representa uma base consistente para evolução futura em cenários mais próximos de aplicações reais de IoT e automação.
