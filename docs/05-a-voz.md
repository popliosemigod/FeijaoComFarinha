# A voz — o que já ouve, e o que ainda falta decidir

> **Desde 01/10/2026 o robô transcreve fala.** Henrique pediu fala para texto, e
> saiu a metade "ouvir" da opção 1 abaixo: a placa recorta as frases
> ([`src/voz_serial.h`](../src/voz_serial.h)) e o PC transcreve com Whisper
> ([`scripts/ouve.py`](../scripts/ouve.py)) — o mesmo motor da ponte do Jaspy,
> pelo cabo USB, porque o Wi-Fi ainda não tem credencial.
>
> ```powershell
> pio run -e cerebro -t upload --upload-port COM13
> python scripts/ouve.py --porta COM13
> ```
>
> | Medido na XIAO S3 Sense, voz sintética a ~1 m | |
> | --- | --- |
> | *"Feijão com farinha, ande para a frente."* | `Feijão com farinha, ande para a frente.` |
> | *"Pare agora."* | `Para e agora.` |
> | *"Vire para a esquerda e tire uma foto."* | `Vire para a esquerda e tira uma foto.` |
> | Tempo para transcrever (CPU, modelo `small`) | 2,5 a 3,6 s por frase; uma levou 8,9 s |
>
> **O texto fica no PC.** Transformar o que foi dito em ordem para o robô, e
> falar de volta, continuam em aberto — é o resto deste documento.

O prompt do projeto diz, com estas palavras: *"Decisão em aberto — PERGUNTE
antes de implementar a parte de voz: qual serviço de reconhecimento de fala /
IA / síntese de voz será usado."*

Então nenhuma das três opções abaixo está escrita. O que existe é a **forma do
contrato**, em [`src/voz.h`](../src/voz.h), e uma implementação que não finge:
`VozDeMentira` mostra o nível do microfone e registra no console o que teria
sido dito.

Isso mantém a orquestração inteira testável — o robô anda, ouve, "responde" e
para — sem nenhuma chave de API. E é melhor que uma frase enlatada: um robô que
responde sem ter entendido nada é pior que um robô calado, porque o calado não
mente sobre o que ele é.

## As três opções

### 1. A ponte do Jaspy, na LAN — **recomendada**

O laboratório já tem, em `assistente/ponte/`, um servidor que recebe áudio,
transcreve com Whisper na própria máquina, decide e devolve ordens. É o mesmo
que move o avatar em AR e o corpo de pinguim na bancada.

O robô entraria como **mais um corpo do mesmo agente** — que é exatamente o que
"baseado no corpo do Jaspy" quer dizer. O protocolo de encarnação já existe
(`assistente/docs/protocolo-alma.md`), já tem estado, prioridade entre corpos e
eleição de quem fala.

| A favor | Contra |
| --- | --- |
| Já existe, e já funciona | só funciona em casa, na mesma rede |
| Transcrição roda no PC: a placa só empurra 32 KB/s | depende de o PC estar ligado |
| Um agente, vários corpos — o robô herda o que o avatar já sabe | |
| Sem custo por frase | |

O que falta para plugar: um cliente WebSocket em claro na porta 8081 (o mesmo
que o `firmware/jaspy-corpo` usa) e a tradução de `Intencao` para os comandos
que o corpo entende. É trabalho de um dia, não de uma semana — o grosso já
está escrito dos dois lados.

### 2. API na nuvem, direto da placa

| A favor | Contra |
| --- | --- |
| Funciona em qualquer lugar com internet | TLS no ESP32 come RAM e faz handshake lento a cada reconexão |
| Qualidade de reconhecimento melhor | latência da rede externa, no meio de uma conversa |
| Nada para manter em casa | cada frase custa dinheiro |

### 3. Reconhecimento local (ESP-SR)

Só roda na S3 — não na C3 —, e só para um conjunto **fixo** de palavras:
"para", "vem", "anda", "vira".

Não substitui os outros dois, e convive bem com qualquer um deles como
**palavra de acordar**. É também o único que continua funcionando com a
internet caída e o PC desligado, o que o torna o candidato natural para uma
única palavra: `"para"`.

## A recomendação

**Opção 1 como caminho principal, opção 3 só para a palavra "para".**

O argumento não é técnico, é de projeto: o robô e o avatar são o mesmo agente
em corpos diferentes, e dois serviços de voz diferentes fariam deles dois
personagens que se parecem. A opção 3 entra por cima porque parar é a única
ordem que não pode depender de rede nenhuma.

## O que falta decidir

1. Qual das três — ou outra.
2. Se o robô fala com a **mesma voz** do avatar (mesma síntese) ou com voz
   própria.
3. Se ele deve funcionar **fora de casa**. A resposta muda tudo: se sim, a
   opção 1 sozinha não basta.
