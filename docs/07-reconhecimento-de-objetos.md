# Reconhecimento de objetos

> **Atualizado em 29/09/2026:** Henrique decidiu usar sempre o celular (um
> Galaxy A14) para estes testes. A seção
> [*"A foto no celular"*](#a-foto-no-celular) é sobre isso — inclui como a
> foto chega na galeria do aparelho.

A câmera já tirava foto e despejava em base64 pelo console (`p`, desde a
montagem do corpo). O que faltava era alguém do outro lado **ler** essa
imagem. Este documento é sobre isso: [`scripts/reconhece.py`](../scripts/reconhece.py),
que pede a foto, salva e diz o que a câmera está vendo.

## Por que não é um modelo treinado do zero

O pedido original era "capturar da internet uma database já rotulada para uma
IA que já pudesse reconhecer objetos" — e é exatamente isso: o script usa o
**MobileNetV2 pré-treinado em ImageNet**, que o `torchvision` baixa uma vez
(≈14 MB) e mantém em cache. Mil categorias, já rotuladas, prontas — nenhum
treino roda aqui.

A alternativa seria treinar um classificador com fotos do próprio robô, do
jeito que o FarmIO fez com o "tem planta na frente" (`farmio_visao`, regressão
logística em ponto fixo, dez características). Não fazia sentido para este
caso: o robô quer reconhecer **objeto qualquer**, não uma categoria fechada
com poucos exemplos — é o problema que uma rede treinada em milhões de fotos
já resolve melhor do que um modelo pequeno treinado em dezenas.

## Onde roda, e por que não é na placa

**No PC, nunca na ESP32-CAM.** MobileNetV2 tem ~3,5 milhões de parâmetros;
mesmo quantizado, não cabe no orçamento de RAM de uma placa que já reparte
PSRAM entre câmera e microfone (ver a seção *"O QUE ESTA PLACA NÃO FAZ"* em
[`include/config_cerebro_cam.h`](../include/config_cerebro_cam.h)). A câmera
continua fazendo só o que sempre fez — capturar e transmitir —, e a
inteligência pesada fica do lado que tem CPU e RAM de sobra.

## A foto no celular

O console de rede (`docs/06`) é texto puro — telnet não mostra imagem. Como
Henrique vai testar sempre pelo telefone,
[`src/web_cerebro.h`](../src/web_cerebro.h) abre uma segunda porta, a 80, só
para foto: `http://<ip-do-robô>/` no navegador do celular mostra um botão
**"Tirar foto"** e, depois da primeira captura, um botão **"Baixar"`**.

**Por que "Baixar" e não só mostrar a foto na tela.** Uma `<img>` sozinha
entrega a imagem para ver, não para guardar. O atributo `download` no link faz
o Android salvar o arquivo pelo caminho normal de downloads do navegador — e a
galeria de fábrica do Android (e a da Samsung, que o A14 traz) indexa a pasta
Download sozinha, sem app nem configuração. Não é integração com a galeria: é
oferecer o arquivo do jeito que a galeria já sabe achar. Tocar e segurar a
foto e escolher "salvar imagem" no menu do navegador funciona do mesmo jeito,
como alternativa.

O endereço aparece no log de boot (se houver `WIFI_SSID` em `secrets.h`) e no
comando `?` do console, na linha `foto no celular:`.

**Uma foto só por vez na placa** — cada `POST /foto` sobrescreve a anterior no
buffer da `Camera` (`captura()`, em `camera.h`); é o mesmo desenho que o
FarmIO já usa do lado da câmera dele.

## Uso

```powershell
python -m pip install --user pyserial requests pillow
python -m pip install --user --index-url https://download.pytorch.org/whl/cpu torch torchvision

python scripts/reconhece.py --porta COM7          # pelo cabo de gravação
python scripts/reconhece.py --rede 192.168.1.50   # pelo console de rede (telnet, porta 23)
python scripts/reconhece.py --http 192.168.1.50   # a mesma foto que o celular já viu
```

`--http` é o caminho recomendado quando o robô já está em uso pelo celular:
pede a MESMA foto que a página do telefone mostra, em JPEG binário — sem
base64 e sem o risco de corrida no console descrito abaixo.

Cada chamada:

1. manda `p` (o mesmo comando do console manual);
2. lê o base64 entre `---FOTO-INICIO---` e `---FOTO-FIM---`, decodifica e
   salva em `evidencias/reconhecimento/AAAAMMDD-HHMMSS.jpg` (gitignorado —
   ver a nota em `.gitignore` sobre evidência pesada e volátil);
3. classifica e imprime as `--top` categorias mais prováveis (padrão 5);
4. acrescenta uma linha a `evidencias/reconhecimento/catalogo.csv` — o começo
   do banco de imagens rotuladas, que cresce sozinho a cada objeto mostrado à
   câmera, sem precisar treinar nada para começar a existir.

## Limitações honestas

- **ImageNet não tem categoria "peça impressa em 3D" nem "Henrique"** — é um
  classificador generalista, treinado em fotografia do mundo em geral. Serve
  para objetos do dia a dia (caneca, cadeira, teclado, animal, fruta); para
  reconhecer algo específico do laboratório, o caminho seria retreinar em
  cima do catálogo que este script já está construindo — não feito ainda.
- **A primeira foto real (29/09/2026) deu confiança baixa em tudo** — a maior
  categoria ficou em ~2,6%, porque a imagem original saiu escura (ver
  [`docs/06-montagem-do-corpo.md`](06-montagem-do-corpo.md)). Um classificador
  incerto sobre uma foto ruim é o comportamento certo, não um defeito do
  script — testado com essa mesma foto no smoke test de 29/09/2026.
- **Risco de corrida no console, não corrigido aqui.** `camera.despeja()`
  escreve várias linhas seguidas no `Print` que também recebe log de
  `tarefaSentidos()` (o aviso "microfone sem sinal", a cada ~2 s depois do
  primeiro segundo). Se uma dessas linhas cair no meio do base64, a foto
  chega corrompida — o script detecta pelo tamanho decodificado não bater com
  o anunciado, mas não recupera sozinho. É um defeito de arquitetura do
  console (duas tarefas escrevendo no mesmo `Serial`), não deste script; fica
  registrado aqui porque é quem primeiro sente o efeito. Mitigação futura:
  seção crítica em `executaComando()`, ou uma fila só para a foto.

## Smoke test (29/09/2026)

Rodado sem hardware — contra a foto real já salva em
`evidencias/marcos/primeira-foto-20260929.jpg` — para provar que decodificação,
transformação e inferência funcionam antes de depender da placa estar ligada:

```
11860 bytes lidos
  2.6%  golfcart
  2.0%  harvester
  1.9%  forklift
  1.8%  carousel
  1.8%  lumbermill
```

Confiança baixa é esperada (a foto é escura e sem o objeto de teste
planejado). O que este teste prova é o **caminho de código**: baixar os
pesos, decodificar o JPEG, montar o tensor e classificar — não a qualidade do
reconhecimento em si, que só se mede mostrando objetos de verdade à câmera.

## O que ainda não está provado

- **A captura via `--porta` nunca rodou contra a placa real nesta sessão** —
  as duas placas foram desconectadas antes deste script existir (ver
  `CONTEXTO.md`). O caminho de rede (base64 → decode → classifica) está
  provado; o caminho serial (abrir porta, esperar o boot, mandar `p`, ler a
  resposta) só está dimensionado.
- **`web_cerebro.h` nunca rodou contra hardware.** `pio run -e cerebro_cam`
  compila limpo (29/09/2026), e a lógica de `captura()`/`serveFoto()` segue o
  mesmo padrão já provado em produção no `main_cam.cpp` do FarmIO — mas
  nenhum celular abriu a página ainda, e "compila" não é "testado".
- Nenhum objeto foi apresentado deliberadamente à câmera ainda — o catálogo
  em `evidencias/reconhecimento/catalogo.csv` está vazio até a primeira
  captura de verdade.
