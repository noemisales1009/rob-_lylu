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

**WiFi: usar portal cativo, NÃO teclado na tela.** Digitar senha num teclado de
480x272 é sofrido (teclas de ~30px, maiúscula/minúscula/número/símbolo). O jeito
que funciona:
1. A tela mostra *"Me conecte! Rede: **Lylu-Setup**"*, com a Lylu curiosa do lado
2. Você conecta o celular nessa rede
3. Abre sozinho um formulário com a lista de redes que ela achou
4. Você digita no teclado do celular; ela salva (NVS/Preferences) e conecta 🎉

Bônus: tira as credenciais do código de vez — hoje estão como `SUA_REDE_AQUI`
justamente pra não vazar no GitHub.

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
8. **Segundo cartão, físico e diferente** — mesmo silêncio (prova definitiva:
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
