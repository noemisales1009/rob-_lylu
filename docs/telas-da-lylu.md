# 🖥️ As telas da Lylu — conceito e plano

> Documento vivo. Nasceu em 22/08/2026, conversando sobre o que a Lylu deve ser
> antes de sair codificando. Para publicar na página "Robô Lylu" do Notion.

---

## 🧭 O conceito (o que decide tudo o resto)

**A Lylu é uma companheira que por acaso conhece suas tarefas — não uma lista de tarefas com carinha.**

Se ela fosse um app de produtividade fofo, o sarcasmo dela seria insuportável.
Como companheira, o mesmo sarcasmo vira intimidade.

### O coração: o modo dia difícil

Todo app de produtividade aperta mais quando você fica pra trás — notificação
vermelha, sequência perdida, gráfico despencando. **A Lylu recua.** O comando
`/diaruim` não é um detalhe, é a tese dela.

> **Filtro para qualquer decisão futura:** isso acolhe ou isso cobra?

O sarcasmo dela só funciona *porque* existe o contrapeso. É uma amiga que cutuca
porque sabe recuar quando precisa. Sem o recuo, cutucar vira julgar.

### 🎯 A missão do dia (o antídoto da culpa)

Pedido da Noemi: *"uma alternativa pra que eu não me sinta culpada por não cumprir,
tipo o mínimo aceitável."*

O problema que isso resolve: uma lista de 8 tarefas com 3 feitas é lida como
**5 fracassos**, não como 3 vitórias. A conta padrão de qualquer app de tarefas
produz culpa por construção.

**A mecânica:** todo dia tem **1 a 3 coisas** que definem o dia como ganho.
Fez a missão → **o dia foi cumprido.** Ponto final. O resto é bônus.

#### Três níveis de dia

| Nível | O que é | A Lylu |
|---|---|---|
| 🌟 **Dia cheio** | missão + bônus | comemora junto, sem exagero |
| ✅ **Dia mínimo** | só a missão | **comemora de verdade** — foi vitória, não consolo |
| 🛟 **Dia de sobrevivência** | nem a missão deu | acolhe; nenhuma cobrança; o dia não conta contra |

#### As regras que fazem isso funcionar

1. **A missão é curta de propósito.** 1 a 3 itens. Se virar 6, deixa de ser mínimo
   e volta a ser lista.
2. **O humor dela segue a MISSÃO, não o total.** Fez a missão e faltaram 5 bônus?
   Ela fica **comemorando**, não julgadora. Isso muda tudo: hoje o humor olha a
   contagem total, e por isso ela pode julgar um dia que na verdade foi bom.
3. **Comemorar sem tom de consolo.** *"Missão do dia batida! 🎉"* — nunca
   *"ah, pelo menos você fez o mínimo…"*. Prêmio de participação é fracasso
   disfarçado, e a pessoa sente.
4. **No dia difícil, o bônus some da tela.** Fica só a missão. Não dá pra sentir
   culpa do que não está à vista — e conecta direto com o `/diaruim` que já existe.
5. **A missão pode ser reduzida no meio do dia**, sem drama. Dia desandou? Ela
   pergunta: *"quer cortar a missão pra uma coisa só?"*
6. **Um dia de sobrevivência não vira dívida.** Não acumula, não aparece em
   vermelho amanhã, não conta sequência quebrada.

#### Como implementar (com o que já existe)

- **Quem marca a missão:** a **prioridade do Todoist**. P1 (a vermelha) = missão do
  dia. Sem infra nova, sem tela nova — você já usa isso.
- **No `lylu_diario`:** somar as colunas `missao_total` e `missao_feita`, para o
  histórico e o relatório semanal passarem a medir a missão, não o volume bruto.
- **Na tela de Tarefas:** a missão em cima, destacada; o bônus abaixo, discreto.
- **No n8n:** o prompt da Lylu passa a receber a missão separada do resto, e a
  regra de humor muda para olhar a missão.
- **Na semana:** contar "dias em que a missão foi cumprida" em vez de porcentagem
  de tarefas. Uma semana com 5 missões batidas é uma semana ótima, mesmo com
  30 bônus intocados.

### 🎮 Juntar com o RPG "Próximo Nível" (proposta — falta decidir o prêmio)

A Noemi tinha criado o repo `proximo_nivel`: um RPG de produtividade em React
(missões com XP, moedas, loja de prêmios, hábitos, níveis). **O projeto não foi
pra frente e o banco nunca chegou a existir** — ou seja, nada pra migrar.

**Provável causa da morte:** era um site que exigia lembrar de abrir. Gamificação
que precisa de disciplina pra ser acessada morre — pede justamente o que deveria
estar ajudando. Com a Lylu na mesa, ela fica visível o dia inteiro de graça.

**Reframe:** não juntar os dois projetos — trazer as *mecânicas* pro sistema da
Lylu, que já tem Supabase, Todoist (captura), n8n (cérebro), Telegram (conversa)
e a presença física.

```
P1 no Todoist  →  missão do dia
completou      →  n8n dá XP + moedas  →  Lylu comemora na mesa 🎉
juntou moedas  →  troca por prêmio de verdade
```

Sem app novo: 2 tabelas no Supabase existente (`lylu_progresso` com xp/moedas,
`lylu_premios`), um trecho no n8n, uma tela na Lylu e comandos no Telegram
(`/loja`, `/resgatar`).

**Regras herdadas do conceito:**
- **Só ganha, nunca perde.** Cortar o "dano por mau hábito" do RPG antigo — se a
  moeda pode ser perdida, erro vira dívida, e aqui não existe dívida. Mau hábito
  só não dá moeda; não tira.
- **Nível fica pra depois.** Moeda + prêmio já é o motor; nível é enfeite.
- **O prêmio tem que ser real e escolhido por ela** (episódio sem culpa, doce,
  tarde livre, um livro) — é o que faz a moeda valer algo.

**⏳ Pendente:** definir quais prêmios. Se não houver prêmio que mova de verdade,
abandonar a moeda e investir só na presença e na comemoração da Lylu.

**Ordem:** terminar a Lylu primeiro (a missão do dia já está no plano); a
gamificação encaixa por cima depois, sem refazer nada.

### Três princípios que saem daí

1. **Ela precisa ter vida própria.** Se ela só existe quando há tarefa pendente,
   vira ansiedade encarnada em cima da mesa. Tendo tempo livre — brincando,
   cochilando, se distraindo — ela vira presença. Você olha e sorri.
2. **O poder dela é ser vista, não ser usada.** TDAH tem o "fora da vista, fora
   da cabeça": por isso ela é objeto físico e não mais um app. Ela funciona
   **passivamente**. Tocar nela é bônus, nunca obrigação. Se um dia for preciso
   navegar por 4 telas pra ela ser útil, erramos.
3. **A relação cresce com o tempo.** O diário, a memória do agente, o histórico
   da semana — é isso que a torna *dela* e não um gadget genérico.

### ✅ DECIDIDO: a Lylu tem opinião própria

Ela **não** é só uma leitora fiel do Todoist. Tem iniciativa: percebe padrões,
comenta, sugere. É o que a separa de um painel bonito.

> ⚠️ **DISTINÇÃO CRÍTICA — não confundir as duas coisas:**
>
> | | **Lembrar das atividades** | **Palpite / opinião** |
> |---|---|---|
> | O que é | A função principal dela | Iniciativa, comentário |
> | Frequência | **SEMPRE. Sem exceção.** | Uma vez, sem insistir |
> | Pode falhar? | **Nunca** | Sim, tudo bem |
> | Desliga no dia difícil? | Não (muda o *tom*, não some) | As cutucadas sim |
>
> Se ela às vezes esquecer de lembrar, ela deixa de ser confiável — e aí a pessoa
> volta a ter que guardar tudo na cabeça, que é exatamente o que ela veio resolver.
> **Lembrar não é opinião dela, é o trabalho dela.**

#### Como lembrar sempre, sem virar chatice

A saída está no princípio 2 (*o poder dela é ser vista, não usada*): o lembrete é
**ambiente e permanente**, não uma interrupção repetida.

1. **A lista fica sempre visível** — não precisa cutucar o que já está à vista.
2. **A carinha dela é o lembrete que atravessa todas as telas.** O humor já reflete
   o estado das tarefas: *alertando* = tem coisa atrasada, *julgadora* = tem muita.
   Então, mesmo na tela de Foco ou do Relógio, olhar pra ela já te diz como está o
   dia — sem texto, sem alarme, sem interromper.
   → **Consequência de projeto:** o estado das tarefas não pode ficar preso na tela
   de Tarefas. Ele viaja com ela.
3. **Só o urgente interrompe de verdade** (compromisso com hora, prazo estourando):
   aí sim ela chama atenção ativamente, com a emoção *alertando*.
4. **No dia difícil ela continua lembrando** — muda o tom, não o fato.
   *"Tem duas coisas ali pra quando você puder. Sem pressa."*

#### As travas (valem para os PALPITES, não para os lembretes)

1. **Ela propõe, nunca insiste.** Falou uma vez, deixou quieto.
2. **Uma de cada vez.** Nunca empilhar dois palpites; escolhe o mais acolhedor.
3. **Ela pode estar errada, e tudo bem** — desde que fale como quem chuta
   ("acho que…", "posso tá enganada, mas…"). Assistente que erra afirmando é
   irritante; amiga que arrisca um palpite é gostoso.
4. **No modo dia difícil, só sobra o que acolhe.** As cutucadas desligam.

#### Iniciativas propostas (ordem de valor)

| O que ela nota | O que ela fala | Por quê |
|---|---|---|
| Tarefa parada há dias | *"Essa tá aí desde segunda. Quer deixar pra lá? Tá tudo bem."* | **A mais importante.** Nenhum app de produtividade dá permissão pra desistir sem culpa — e é exatamente disso que quem tem TDAH precisa |
| Dia com coisas feitas | *"Você fez 6 coisas hoje. Sei que não parece, mas fez."* | TDAH tende a não registrar o que foi concluído; ela devolve isso |
| Muito tempo sem pausa | *"Tá há 2h aí. Bebe uma água?"* | Cuidado básico, casa com a emoção *cuidadora* |
| Vários dias corridos seguidos | *"Terceiro dia puxado seguido. Tá tudo bem por aí?"* | Abre espaço pro `/diaruim` sem você ter que pedir |
| Lista grande demais | *"12 tarefas pra hoje. Ambiciosa, hein 👀"* | Cutucada leve — desliga no dia difícil |

#### O que ela NUNCA faz
- Contar sequência ("você quebrou 5 dias seguidos!") — é culpa disfarçada de jogo
- Comparar com outras pessoas
- Falar em horário fixo — vira ruído e ela deixa de ser notada

#### Onde a iniciativa mora
- **No n8n (o cérebro):** padrões ao longo de dias. Ele já tem o agente com
  memória e o `lylu_diario` desde julho/2026 — dá pra notar tendências hoje.
- **Na placa:** o imediato — tempo sem interação, tempo na tela de foco, hora do dia.
- **Dado que falta:** pra notar "tarefa adiada 5 vezes", precisa guardar histórico
  por tarefa (hoje o `lylu_diario` só guarda contagens do dia).

### ✅ DECIDIDO: ela é uma coisa da mesa de trabalho

Fica na escrivaninha, no campo de visão de quem trabalha. Consequências diretas:

- **A tela de Foco é a alma dela**, não um extra. Sobe pro topo da ordem de
  construção. É onde ela passa a maior parte do tempo útil.
- **A tela padrão é a de trabalho** (Foco ou Tarefas). Casa e Relógio são o
  respiro entre as coisas, não o estado principal.
- **A iniciativa dela é sobre a sessão de trabalho** — tempo sem pausa, tarefa
  travada, "você já fez bastante hoje" — e não sobre a casa/rotina doméstica.
- **Nada de som alto nem movimento chamativo demais** enquanto você trabalha:
  ela divide a mesa com o seu foco, não disputa com ele. Quando o alto-falante
  chegar, sons discretos e a possibilidade de silenciar durante o Pomodoro.
- Reforça a decisão de **não** fazer clima/notícias: mesa de trabalho já tem
  monitor pra isso.

---

## 📱 As telas

Ordem de construção: **Foco → Tarefas (já existe) → Casa → Relógio → Semana → Ajustes**

Como ela é objeto de **mesa de trabalho**, o Foco virou a tela principal e subiu
pro topo — apesar de ser mais trabalhosa que Casa e Relógio, é onde ela passa o
tempo útil. Casa e Relógio continuam importando como respiro, e são baratas de
construir na arquitetura atual.

### 🏠 1. A casa da Lylu
Ela só **vive**: anda, boceja, brinca, cochila. Nenhuma tarefa, nenhuma cobrança.

Parece supérfluo, mas é o contrapeso do projeto: se toda tela pedir algo, o
aparelho vira fonte de culpa. Precisa existir um lugar onde ela é só companhia —
a tela pra quando você olha pra ela num dia ruim.

### 🕐 2. Relógio grande
**Cegueira temporal** é uma das coisas mais atrapalhadas do TDAH. Relógio grande
e visível no canto da mesa resolve isso passivamente, sem precisar perguntar nada.

- Custo baixíssimo: o ESP32 pega a hora certa da internet sozinho (NTP).
- **A Lylu brinca nesta tela** (ideia da Noemi). Faz sentido pra personagem: não
  tem trabalho, então ela se diverte.
- Na hora cheia, uma comemoradinha. Com o alto-falante, um sonzinho junto.

### 🎯 3. Foco / Pomodoro
A tela mais valiosa que ainda não existe. Toca em "começar", ela marca 25 min, e a
Lylu fica ali **trabalhando ao seu lado** — não te cobrando, só fazendo companhia.
No fim, comemora.

Ataca a parte mais difícil do TDAH: *começar*. E funciona sem internet.

### ✅ 4. Tarefas
Já existe (é a tela atual). Vira a tela "de trabalho". A Lylu aponta a lista aqui.

### 📊 5. A semana
Últimos 7 dias com a Lylu comentando. **Os dados já existem**: a tabela
`lylu_diario` no Supabase guarda isso desde julho/2026. É a versão visual do
relatório semanal que já estava planejado pro Telegram.

### ⚙️ 6. Ajustes
WiFi, status da conexão, brilho, e o modo dia difícil acessível por um toque
(hoje só pelo Telegram).

#### ✅ WiFi: decisão revista em 22/09/2026 — **teclado na tela**

A decisão anterior era portal cativo, e o motivo era o tamanho da tela: digitar
senha num teclado de 480x272 é sofrido (teclas de ~30px, com maiúscula, minúscula,
número e símbolo se revezando). Na ESP32-P4 a tela passou a ser **1024x600**, onde
a tecla sai com ~90px — maior que a de qualquer celular. O motivo caiu; a decisão
caiu junto. Sem portal cativo, sem modo ponto de acesso, sem celular no meio.

Como ficou (`lylu_p4/main/rede.c` + a seção WI-FI do `main.c`):

1. Ajustes → **Wi-Fi** abre uma tela por cima das outras (a barra de status
   continua à vista, porque a carinha dela é o lembrete que atravessa tudo)
2. A lista de redes aparece sozinha, ordenada por sinal, sem as repetidas de 5 GHz
3. Toca na rede → teclado em português embaixo, **a Lylu espiando por trás dele**
4. Conectou → a senha vai pra NVS e ela volta sozinha nos próximos boots 🎉

Duas coisas vieram junto:
- **As credenciais saíram do código de vez.** No sketch antigo estavam como
  `SUA_REDE_AQUI` justamente pra não vazar no GitHub.
- **O relógio passou a ser confiável.** Com internet ele pega a hora por NTP;
  antes mostrava a hora em que o programa foi compilado. Como a cegueira temporal
  é metade do motivo da tela de Relógio existir, isso não é detalhe.

O portal cativo volta da gaveta se algum dia a Lylu tiver uma tela pequena de novo.

**Teclado sem símbolo nenhum.** O teclado pronto da LVGL marca as teclas de
comando com ícones da fonte Montserrat, que as fontes TTF da Lylu não têm — sairia
quadradinho vazio. Então o mapa é nosso, com as teclas escritas: `Apagar`,
`Cancelar`, `Conectar`. Ficou mais na voz dela do que os ícones seriam.

### ⏸️ Adiados de propósito
**Clima e notícias.** Bonitinhos, mas exigem chave de API e manutenção, e o
celular já faz melhor. Não é isso que faz a Lylu especial.

---

## 🎭 A Lylu em cada tela

A mesma Lylu, com contexto. É o que dá alma ao sistema de telas.

| Tela | O que ela faz |
|---|---|
| Foco | fica quietinha do lado, trabalhando junto |
| Tarefas | aponta a lista |
| Casa | anda solta, brinca, cochila, boceja |
| Relógio | brinca (tem tempo livre) |
| Semana | comemora ou consola, conforme o resultado |
| Ajustes | curiosa, esperando a conexão |

**Mas a emoção dela nunca é só decorativa:** em qualquer tela, o humor reflete o
estado real das tarefas (*alertando* = tem atrasada). É assim que ela lembra das
atividades mesmo quando você não está na tela de Tarefas.

Na prática o código já sorteia entre andar e parar pra fazer algo — basta mudar
o **peso** das escolhas por tela.

### 🥹 O carinho volta
No protótipo antigo (OLED + sensor de toque) tocar nela deixava ela feliz e ela ria.
Trazer isso pra tela grande: tocar **nela** (não na lista) e ela reagir — rir,
brincar, fazer graça. O touch já funciona; a tela do Relógio/Casa é o lugar
perfeito, já que não tem tarefa pra atrapalhar.

---

## 🎨 Artes: o que temos e o que falta

**Prontas na placa (14 animações, 168 arquivos, 7,3 MB):**
- 8 emoções: animada, comemorando, brincalhona, cuidadora, descansando,
  entediada, alertando, julgadora
- 4 caminhadas: direita, esquerda, cima, baixo
- 2 ações: pensando, apontando-lista

**Guardadas mas não na placa:** variante "andando de cabelo preso", versões v1 dos gifs.

**Faltando (por prioridade):**

| Arte | Destrava | Urgência |
|---|---|---|
| 🎯 **concentrada / trabalhando** | a tela de Foco inteira | **Alta** — é a única que bloqueia uma tela |
| 🎈 brincando (com bolinha, pulando) | Relógio e Casa | Média — dá pra usar a *brincalhona* |
| 😄 rindo | reação ao carinho | Baixa — dá pra usar a *comemorando* |
| 🥱 bocejando | Casa, fim de tarde | Baixa |

**Espaço:** cada animação = 12 quadros = 540 KB.
Cabem ~8 novas na flash da 4848. Num cartão SD de 4 GB, ~7.400.

---

## 🔧 Notas técnicas para quando for construir

- **A arquitetura atual já favorece multi-telas:** o fundo/interface vive numa
  camada e a Lylu é composta por cima. Trocar de tela = trocar o fundo; ela
  continua andando igual.
- **Navegação:** arrastar o dedo é o mais simples. Ideia mais bonita: a Lylu anda
  até a beirada e *puxa* a próxima tela — ela vira a navegação viva do aparelho.
- **Decisão pendente:** construir na 4848 (480x480, funcionando, tela maior) ou na
  JC4827 (480x272, imune ao tremor/pontinho, porte pronto em `lylu_jc4827/`).
  Definir antes de começar, pra não construir as telas duas vezes.

---

## 🔌 Diagnóstico da Guition JC1060P470 (22/09/2026)

Três coisas descobertas ao fazer a tela de Wi-Fi funcionar. Todas custaram horas e
nenhuma estava documentada em lugar nenhum — ficam aqui para o próximo.

### 1. O toque estava deslocado desde sempre

O `placa.c` convertia as coordenadas do GT911 de 800x480 para 1024x600, seguindo a
demonstração de fábrica. **O GT911 desta placa reporta direto em 1024x600.** A
conversão empurrava todo toque 28% para a direita e 25% para baixo.

Passou despercebido porque nada exigia precisão: arrastar entre telas é gesto, e os
alvos eram grandes. Perto do topo o erro é de ~25px e ainda dá pra acertar um botão;
embaixo passa de 90px. O teclado do Wi-Fi foi a primeira coisa a expor isso — as
teclas de baixo grudavam todas na borda.

**Como foi medido:** uma linha de log no `process_coordinates` imprimindo o valor cru
e o convertido. Apareceu `cru 98,540 -> tela 125,599`: um `y` cru de 540, acima dos
480 que a conversão supunha. Prova direta, sem teoria.

### 2. O DHCP não voltava — era uma otimização do SDIO

A placa associava na rede e o IP nunca chegava. O que confundia: varredura, conexão
e eventos funcionavam **perfeitamente**.

A explicação é que controle e dados são caminhos diferentes no ESP-Hosted. O C6 veio
de fábrica com ESP-Hosted **2.3.0**, e o componente do host é **2.12.x** — nove
versões à frente. O host liga por padrão o `STREAMING MODE`, uma otimização de
**recepção** que o C6 antigo não fala. Resultado: comando vai e volta (controle),
mas a oferta do DHCP, que chega de fora, se perde.

**A correção** (uma linha no `sdkconfig.defaults`, sem encostar no C6):

```
CONFIG_ESP_HOSTED_SDIO_OPTIMIZATION_RX_NONE=y
```

O log passa a dizer `SDIO Host operating in PACKET MODE` e o IP chega em 1–2 s.

> **A pista que resolveu:** o Wi-Fi tinha pegado IP duas vezes antes, por acaso.
> Incompatibilidade total não funcionaria *nunca* — o que funciona às vezes é
> otimização mal-entendida. Foi esse detalhe que descartou "trocar tudo de versão"
> e apontou para o alvo certo.

Fica pendente atualizar o firmware do C6 (o aviso de versão continua aparecendo).
Enquanto a rede funcionar, não é urgente — e regravar o segundo chip tem risco.

### 3. A placa tem microfone e alto-falante

Uma varredura do barramento I2C no boot (que ficou no `placa.c`, é barata e útil):

```
I2C responde em 0x14  <- toque GT911
I2C responde em 0x18  <- codec de audio ES8311
I2C responde em 0x32
I2C responde em 0x36
I2C responde em 0x5D  <- toque GT911
```

**O ES8311 respondeu.** É codec de entrada *e* saída: microfone e alto-falante. Ou
seja, a Lylu pode ouvir e falar sem hardware novo — falta só ligar o I2S. `0x32` e
`0x36` ainda não foram identificados (`0x36` tem cara de medidor de bateria).

### 4. O microfone funciona — e o pino não é o da placa de referência

Resolvido em 23/09/2026. **`audio.c`** liga o ES8311 e a tela de Ajustes →
Microfone mostra o nível com a Lylu reagindo.

O que custou caro: a placa segue o projeto de referência da Espressif em quase
tudo (I2C 7/8, MCLK 13, BCLK 12, LRCK 10, DOUT 9), **menos em dois pinos**:

| | placa de referência | esta placa |
|---|---|---|
| Dado do microfone (I2S DIN) | GPIO11 | **GPIO48** |
| Liga o amplificador (PA) | GPIO53 / 20 | **GPIO11** |

Lendo o GPIO11 como dado, a amostra vinha **zero perfeito** — e isso enganou por
horas, porque parecia "microfone mudo". Não era: zero exato, sem nem ruído de
fundo, quer dizer **pino errado**. Microfone silencioso ainda entrega ruído de
uns 300 a 500 de RMS. Fica a regra: `rms == 0` é fiação, `rms` pequeno é volume.

Para conferir depois: silêncio dá ~400, voz normal a um palmo passa de 4000.

O microfone é o **MSM381A3729H9CP**, MEMS analógico, ligado nas entradas
diferenciais do ES8311 (e alimentado pelo MICBIAS dele). O alto-falante sai por
um **NS4150** no conector CN3 — ainda não testado.

### ⚠️ Pendência conhecida: o cão de guarda no boot

`Task watchdog got triggered ... CPU 0: main`, uns 5 s depois da tela ficar pronta.
É **aviso, não travamento** — ela segue e liga normal. O `app_main` monta as seis
telas de uma vez sem ceder a vez, e a tarefa da LVGL fica girando em falso esperando
a trava. Anterior à tela de Wi-Fi, mas ela engordou o trecho.

---

## 🔌 Diagnóstico da placa JC4827W543C_I (23/08/2026)

**Veredito: o slot de cartão SD desta unidade está com defeito.** A placa foi
devolvida/trocada e o projeto seguiu na ESP32-4848S040C.

### O que funciona nela
- ✅ Display NV3041A via QSPI (CS=45, SCK=47, D0=21, D1=48, D2=40, D3=39)
- ✅ Luz de fundo no GPIO 1
- ⚠️ **Cores invertidas**, igual à 4848 → usar a mesma chave `TELA_INVERTE_CORES`
- 🔊 **Amplificador de áudio embutido** (AX98357A): BCLK=42, LRCLK=2, DIN=41
  → não precisa comprar o módulo MAX98357A, só um alto-falante 8Ω
- 🔋 Conector de bateria e botão liga/desliga

### O que NÃO funciona
- ❌ **Slot microSD (TF)** — não responde nem ao comando CMD0 do protocolo cru

### Como foi diagnosticado (para referência futura)
Pinagem oficial confirmada em duas fontes (planilha do fabricante em
`profi-max/JC4827W543_4.3inch_ESP32S3_board/Docs` e `lsdlsd88/JC4827W543`):
**TF_CS=10, TF_MISO=11, TF_CLK=12, TF_MOSI=13** (compartilhados com o touch
resistivo RTP, que não é usado na variante C/capacitiva).

Testes feitos, todos com resultado 0xFF (silêncio):
1. Biblioteca SD em 5 velocidades (400 kHz a 20 MHz)
2. Ambos os controladores SPI do ESP32-S3 (HSPI e FSPI)
3. Com e sem resistor de pull-up interno no MISO
4. Com o display ligado e desligado (havia suspeita de conflito de barramento)
5. Protocolo cru: CMD0 direto, sem biblioteca — resposta esperada 0x01, veio 0xFF
6. Varredura de CS em ~26 GPIOs, nas duas ordens de MISO/MOSI
7. Cartão reformatado em FAT32 (SDHC 3,74 GB) e verificado funcionando no PC
8. **Segundo cartão, físico e diferente** — mesmo silêncio
9. **Fonte externa** (carregador Samsung 5V/2A, fora da porta USB do PC) —
   mesmo silêncio. Descarta queda de tensão, a última hipótese em aberto
   (prova definitiva:
   o defeito é do slot, não do cartão)

### Consequência
A JC4827 tem só **4 MB de flash** (confirmado por esptool). Sem cartão sobra
espaço para ~4 animações; o projeto tem 22. **Sem SD, esta placa não serve.**

### Armadilhas encontradas no caminho (úteis para qualquer porte futuro)
- O `Serial` só aparece no USB se a opção **CDCOnBoot=cdc** estiver ligada
  (a placa usa USB nativo do S3, não tem chip CH340 como a 4848).
- Ler a serial exige `DtrEnable = $true` (diferente da 4848).
- **Nunca usar GPIO 35, 36, 37 para nada** — são da PSRAM; tocá-los trava a placa
  e ela some do PC (recupera com BOOT+RESET).
