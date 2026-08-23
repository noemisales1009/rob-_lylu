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

### Perguntas em aberto (decisão da Noemi)

- **A Lylu vive dentro do Todoist ou tem opinião própria?** Hoje é leitora fiel.
  Poderia ter iniciativa: sugerir pausa depois de 3h direto, notar que uma tarefa
  foi adiada 5 vezes e perguntar se não é hora de largar, lembrar de água. Mais
  viva — e também mais intrusiva. Qual dose?
- **É objeto de mesa de trabalho ou de casa?** Se escrivaninha, o Foco é a alma
  dela. Se sala/cabeceira, o Relógio e a Casa importam mais e as tarefas viram
  coadjuvantes.

---

## 📱 As telas

Ordem de construção sugerida: **Casa → Relógio → Foco → Semana**
(Casa e Relógio são quase de graça na arquitetura atual; Foco é o que mais muda o dia.)

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
| Casa | anda solta, brinca, cochila, boceja |
| Relógio | brinca (tem tempo livre) |
| Foco | fica quietinha do lado, trabalhando junto |
| Tarefas | aponta a lista |
| Semana | comemora ou consola, conforme o resultado |
| Ajustes | curiosa, esperando a conexão |

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
