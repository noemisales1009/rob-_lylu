// Texto vai pela ação "send" e áudio pela "voz" da lylu-bridge, as mesmas da placa
// (ponte.c). A resposta do Hermes vai para a placa de todo jeito; com a chave do
// app (pacote servidor/LEIA-APP-PARA-O-HERMES.md) ela aparece e toca aqui também.
import { criaAnimacao } from "./animacao.js";
import { liberaSom, paraSom, toca } from "./voz.js";

// No `npm run dev` o Vite repassa "/ponte" pondo a chave; no APK o app fala direto
// com a lylu-bridge, com a chave do app posta no build (vite.config.js).
const PONTE = import.meta.env.VITE_PONTE_URL || "/ponte";
const CHAVE = import.meta.env.VITE_LYLU_TOKEN_APP || "";
const JSON_CT = { "content-type": "application/json" };
const MAX_TEXTO = 4000;
// Igual à Conversa da placa: no máximo 30 s, e só vai quando ela toca em Enviar.
const MAX_GRAVACAO_MS = 30_000;
const MIN_GRAVACAO_MS = 300;
const TAXA = 16_000; // a ponte quer PCM 16 kHz mono 16 bits

// O que ela mandou fica guardado só neste aparelho, para ver depois.
const GUARDA = "lylu_app_mensagens";
const MAX_GUARDADAS = 100;
// A lista que ela está escrevendo não se perde se trocar de tela ou fechar o app.
const RASCUNHO = "lylu_app_rascunho_lista";
// O áudio em si não cabe no armazenamento: fica só na memória, para o "tentar de novo".
const audios = new Map();
// Depois de mandar, pergunta pela resposta até ela chegar. Sem a ação "resposta"
// (ponte antiga ou chave da placa), desiste na primeira e o app só manda.
const ESPERA_MS = 1500;
const DESISTE_MS = 120_000;
const VOZ_GUARDADA_MS = 29 * 60 * 1000; // a ponte apaga a voz da resposta com 30 min

const ICONE_MIC = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 14a3 3 0 0 0 3-3V5a3 3 0 0 0-6 0v6a3 3 0 0 0 3 3zm5-3a5 5 0 0 1-10 0H5a7 7 0 0 0 6 6.9V21h2v-3.1a7 7 0 0 0 6-6.9z"/></svg>';

const lista = document.getElementById("lista");
const vazio = document.getElementById("vazio");
const conversa = document.getElementById("conversa");
const aviso = document.getElementById("aviso");
const caixa = document.getElementById("caixa");
const texto = document.getElementById("texto");
const falar = document.getElementById("falar");
const enviar = document.getElementById("enviar");
const gravando = document.getElementById("gravando");
const tempo = document.getElementById("tempo");
const abaConversa = document.getElementById("aba-conversa");
const abaLista = document.getElementById("aba-lista");
const telaLista = document.getElementById("tela-lista");
const tipoTarefa = document.getElementById("tipo-tarefa");
const tipoLembrete = document.getElementById("tipo-lembrete");
const tipoHabito = document.getElementById("tipo-habito");
const verAntesBotao = document.getElementById("ver-antes");
const limpar = document.getElementById("limpar");
const conversaTopo = document.getElementById("conversa-topo");
const quandoBox = document.getElementById("quando");
const novoItem = document.getElementById("novo-item");
const itemTexto = document.getElementById("item-texto");
const itensLista = document.getElementById("itens-lista");
const itensVazio = document.getElementById("itens-vazio");
const mandarLista = document.getElementById("mandar-lista");
const personagem = document.getElementById("personagem");
const personagemEstado = document.getElementById("lylu-estado");
const animacao = criaAnimacao(document.getElementById("lylu-animacao"));
if (import.meta.hot) import.meta.hot.dispose(() => animacao.destruir());

// No celular o Enter pula linha; no computador ele envia (Shift+Enter pula).
const dedo = matchMedia("(pointer: coarse)").matches;

let mensagens = carrega();
let gravacao = null;
let avisoRelogio = 0;
let pontePergunta = true; // a ponte sabe a ação "resposta"
let esperaRelogio = 0;
let falandoVoz = false;

function carrega() {
  try {
    const m = JSON.parse(localStorage.getItem(GUARDA) || "[]");
    if (!Array.isArray(m)) return [];
    // Se o app fechou no meio do envio, não dá para saber se chegou.
    for (const x of m) {
      if (x.estado === "enviando") {
        x.estado = "erro";
        x.motivo = "Não sei se chegou.";
      }
      if (x.aguardando && Date.now() - x.quando > DESISTE_MS) {
        x.aguardando = false;
        x.semResposta = true;
      }
    }
    return m;
  } catch {
    return [];
  }
}

function guarda() {
  mensagens = mensagens.slice(-MAX_GUARDADAS);
  try {
    localStorage.setItem(GUARDA, JSON.stringify(mensagens));
  } catch {
    // sem armazenamento (aba anônima): só não guarda
  }
}

function avisa(t) {
  aviso.textContent = t;
  aviso.hidden = false;
  clearTimeout(avisoRelogio);
  avisoRelogio = setTimeout(() => (aviso.hidden = true), 5000);
}

function hora(quando) {
  const d = new Date(quando);
  const h = d.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
  if (d.toDateString() === new Date().toDateString()) return h;
  return `${d.toLocaleDateString("pt-BR", { day: "2-digit", month: "2-digit" })} ${h}`;
}

function duracao(ms) {
  const s = Math.round(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

function motivoDoErro(status) {
  if (status === 401) return "A ponte não aceitou a chave do app.";
  if (status === 413) return "Grande demais para a ponte.";
  if (status === 404 || status >= 500) return "A ponte não respondeu.";
  return `Deu erro (${status}).`;
}

// Cada dia começa limpo (pedido da Mica, 31/10/2026). O dia vira às 5h, como na
// placa. As conversas de antes continuam guardadas e aparecem no botão.
const VIRA_HORA = 5;
const DIA_MS = 86_400_000;
let verAntes = false;

function diaDe(ms) {
  const d = new Date(ms - VIRA_HORA * 3_600_000);
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

function rotuloDoDia(dia) {
  if (dia === diaDe(Date.now())) return "Hoje";
  if (dia === diaDe(Date.now() - DIA_MS)) return "Ontem";
  const [, mes, d] = dia.split("-");
  return `${d}/${mes}`;
}

// A que ainda espera resposta fica à vista, mesmo que seja de ontem.
const deHoje = (m) => diaDe(m.quando) === diaDe(Date.now()) || m.aguardando || m.estado === "enviando";
const visiveis = () => (verAntes ? mensagens : mensagens.filter(deHoje));

// Depois de mandar, a Lylu "fala" um pouquinho para avisar que foi.
const FALA_MS = 3000;
let falandoAte = 0;

function falaUmPouco() {
  falandoAte = Date.now() + FALA_MS;
  setTimeout(atualizaPersonagem, FALA_MS + 50);
}

function atualizaPersonagem() {
  const pensando = mensagens.some((m) => m.estado === "enviando" || m.aguardando);
  const ouvindo = !!gravacao;
  const avisando = Date.now() < falandoAte;
  // Ela só se mexe com a voz tocando (pedido da Mica, 31/10/2026). O que ela está
  // fazendo fora disso vai no texto embaixo.
  animacao.setEstado(falandoVoz ? "falando" : "parada");
  personagem.classList.toggle("compacta", visiveis().length > 0 || !telaLista.hidden);
  personagemEstado.textContent = ouvindo ? "Estou te ouvindo…" : falandoVoz ? "" : pensando ? "Pensando…"
    : avisando ? "Prontinho, mandei!" : "";
}

// O balão da Lylu com a resposta do Hermes, logo abaixo do que ela mandou.
function respostaDaLylu(m) {
  const li = document.createElement("li");
  li.className = "msg lylu";
  const balao = document.createElement("div");
  balao.className = "balao";
  balao.textContent = m.resposta.texto;
  const estado = document.createElement("div");
  estado.className = "estado";
  estado.textContent = `Lylu · ${hora(m.resposta.quando)}`;
  if (m.resposta.audio_id && Date.now() - m.resposta.quando < VOZ_GUARDADA_MS) {
    const ouvir = document.createElement("button");
    ouvir.type = "button";
    ouvir.className = "de-novo";
    ouvir.textContent = "ouvir";
    ouvir.addEventListener("click", () => {
      liberaSom();
      ouve(m);
    });
    estado.append(ouvir);
  }
  li.append(balao, estado);
  return li;
}

function desenha() {
  atualizaPersonagem();
  const aMostrar = visiveis();
  vazio.hidden = aMostrar.length > 0;
  conversaTopo.hidden = !mensagens.length;
  verAntesBotao.hidden = !mensagens.some((m) => !deHoje(m));
  verAntesBotao.textContent = verAntes ? "esconder as de antes" : "ver conversas de antes";
  let diaVisto = "";
  lista.replaceChildren(
    ...aMostrar.flatMap((m) => {
      const dia = diaDe(m.quando);
      const separador = [];
      if (verAntes && dia !== diaVisto) {
        const rotulo = document.createElement("li");
        rotulo.className = "dia";
        rotulo.textContent = rotuloDoDia(dia);
        separador.push(rotulo);
      }
      diaVisto = dia;
      const li = document.createElement("li");
      li.className = `msg ${m.estado}`;

      const balao = document.createElement("div");
      balao.className = "balao";
      if (m.tipo === "audio") {
        balao.classList.add("audio");
        balao.innerHTML = ICONE_MIC;
        balao.append(`Áudio · ${duracao(Math.max(m.ms, 1000))}`);
        if (m.transcricao) {
          const ouviu = document.createElement("q");
          ouviu.textContent = m.transcricao; // o que o Hermes entendeu
          balao.append(ouviu);
        }
      } else if (m.tipo === "lista") {
        balao.classList.add("balao-lista");
        const titulo = document.createElement("strong");
        titulo.textContent = m.lista_tipo === "lembrete" ? `Lembretes · ${m.quando_lembrete}`
          : m.lista_tipo === "tarefa" ? `Tarefas · ${quantos(m.itens.length, "tarefa")}`
          : m.lista_tipo === "habito" ? `Hábitos · ${quantos(m.itens.length, "hábito")}`
          : `Lista · ${quantos(m.itens.length)}`;
        const ul = document.createElement("ul");
        for (const i of m.itens) {
          const item = document.createElement("li");
          item.textContent = i;
          ul.append(item);
        }
        balao.append(titulo, ul);
      } else {
        balao.textContent = m.texto;
      }

      const estado = document.createElement("div");
      estado.className = "estado";
      if (m.estado === "enviando") {
        estado.textContent = "enviando…";
      } else if (m.estado === "enviada") {
        estado.textContent = m.resposta ? `✓ ${hora(m.quando)}`
          : m.aguardando ? `✓ ${hora(m.quando)} · esperando o Hermes…`
          : m.tipo === "lista" ? `✓ ${hora(m.quando)} · o Hermes vai pôr ${m.lista_tipo === "lembrete" ? "nos recados"
            : m.lista_tipo === "tarefa" ? "nas tarefas" : m.lista_tipo === "habito" ? "nos hábitos"
            : "nas tarefas e nos recados"}`
          : `✓ ${hora(m.quando)} · a resposta aparece na Lylu`;
      } else {
        estado.textContent = `${hora(m.quando)} · ${m.motivo}`;
        if (m.tipo !== "audio" || audios.has(m.id)) {
          const deNovo = document.createElement("button");
          deNovo.type = "button";
          deNovo.className = "de-novo";
          deNovo.textContent = "tentar de novo";
          deNovo.addEventListener("click", () => manda(m));
          estado.append(deNovo);
        }
      }

      li.append(balao, estado);
      return m.resposta ? [...separador, li, respostaDaLylu(m)] : [...separador, li];
    }),
  );
  conversa.scrollTop = conversa.scrollHeight;
}

function nova(dados) {
  const m = { id: `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`, quando: Date.now(), estado: "enviando", ...dados };
  mensagens.push(m);
  return m;
}

function ponte(corpo, cabecalhos = JSON_CT) {
  return fetch(PONTE, {
    method: "POST",
    headers: CHAVE ? { ...cabecalhos, "x-lylu-token": CHAVE } : cabecalhos,
    body: typeof corpo === "string" || corpo instanceof Uint8Array ? corpo : JSON.stringify(corpo),
  });
}

function pedido(m) {
  if (m.tipo === "audio") {
    return ponte(audios.get(m.id), {
      "content-type": "application/octet-stream",
      "x-lylu-action": "voz",
      "x-lylu-formato": "pcm_s16le_16000",
      "x-lylu-tela": "celular",
    });
  }
  return ponte({ action: "send", kind: "chat", payload: { text: m.texto } });
}

async function manda(m) {
  m.estado = "enviando";
  m.motivo = "";
  desenha();
  try {
    const r = await pedido(m);
    if (r.ok) {
      m.estado = "enviada";
      audios.delete(m.id);
      m.pedido = (await r.json().catch(() => ({}))).message_id;
      if (pontePergunta && m.pedido) {
        m.aguardando = true;
        agendaEspera();
      } else {
        falaUmPouco();
      }
    } else {
      m.estado = "erro";
      m.motivo = motivoDoErro(r.status);
    }
  } catch {
    m.estado = "erro";
    m.motivo = "Sem conexão com a ponte.";
  }
  guarda();
  desenha();
}

// ---------------------------------------------------------------- resposta

function agendaEspera() {
  clearTimeout(esperaRelogio);
  if (pontePergunta && mensagens.some((m) => m.aguardando)) esperaRelogio = setTimeout(buscaRespostas, ESPERA_MS);
}

async function buscaRespostas() {
  const esperando = mensagens.filter((m) => m.aguardando);
  if (!esperando.length) return;
  try {
    const r = await ponte({ action: "resposta", message_ids: esperando.map((m) => m.pedido) });
    if (r.status === 403) {
      // Ponte sem o pacote do app: a resposta só aparece na placa.
      pontePergunta = false;
      for (const m of esperando) m.aguardando = false;
      falaUmPouco();
    } else if (r.ok) {
      for (const x of (await r.json()).respostas || []) {
        const m = esperando.find((e) => e.pedido === x.message_id);
        if (!m) continue;
        if (x.resposta) recebe(m, x.resposta);
        else if (x.status === "completed" || x.status === "failed") {
          m.aguardando = false;
          m.semResposta = true;
        }
      }
    }
  } catch {
    // sem rede agora: tenta de novo na próxima
  }
  for (const m of esperando) {
    if (m.aguardando && Date.now() - m.quando > DESISTE_MS) {
      m.aguardando = false;
      m.semResposta = true;
    }
  }
  guarda();
  desenha();
  agendaEspera();
}

function recebe(m, resposta) {
  m.aguardando = false;
  m.resposta = { texto: resposta.text, quando: Date.now(), audio_id: resposta.audio_id, formato: resposta.formato };
  if (resposta.transcricao) m.transcricao = resposta.transcricao;
  if (resposta.audio_id && !document.hidden) ouve(m);
}

async function ouve(m) {
  const { audio_id, formato } = m.resposta;
  try {
    const r = await ponte({ action: "audio_get", audio_id });
    if (!r.ok) {
      avisa(r.status === 404 ? "Essa voz já foi apagada (ela fica 30 minutos)." : "Não consegui baixar a voz.");
      return;
    }
    const bytes = new Uint8Array(await r.arrayBuffer());
    falandoVoz = true;
    atualizaPersonagem();
    toca(bytes, formato, () => {
      falandoVoz = false;
      atualizaPersonagem();
    });
  } catch {
    falandoVoz = false;
    atualizaPersonagem();
    avisa("Não consegui tocar a voz.");
  }
}

// ---------------------------------------------------------------- texto

function ajustaCaixa() {
  texto.style.height = "auto";
  texto.style.height = `${texto.scrollHeight + 2}px`;
  // Caixa vazia mostra o microfone; com texto, o Enviar.
  const temTexto = !!texto.value.trim();
  enviar.hidden = !temTexto;
  falar.hidden = temTexto;
}

caixa.addEventListener("submit", (e) => {
  e.preventDefault();
  const t = texto.value.trim().slice(0, MAX_TEXTO);
  if (!t) return;
  liberaSom();
  const m = nova({ texto: t });
  texto.value = "";
  ajustaCaixa();
  manda(m);
});

texto.addEventListener("input", ajustaCaixa);
texto.addEventListener("keydown", (e) => {
  if (!dedo && e.key === "Enter" && !e.shiftKey && !e.isComposing) {
    e.preventDefault();
    caixa.requestSubmit();
  }
});

// ---------------------------------------------------------------- lista

// Vai como mensagem normal: quem põe no Todoist e na Lylu é o Hermes. A Mica
// escolhe aqui se é tarefa, lembrete ou hábito, e o quando do lembrete, em vez de o
// Hermes adivinhar pela frase (pedido dela, 28/10/2026: "não tá intuitivo").
// O `tarefas` do lylu_placa.py troca a lista inteira da placa, então o pedido
// diz com todas as letras para somar, nunca substituir.
const TURNOS = { manha: "de manhã", tarde: "à tarde", noite: "à noite" };

function turnoDeAgora() {
  const h = new Date().getHours();
  return h < 12 ? "manha" : h < 18 ? "tarde" : "noite";
}

let rascunho = { tipo: "tarefa", itens: [], dia: "hoje", turno: turnoDeAgora() };

// "AAAA-MM-DD" no fuso do celular, como o --quando do lylu_placa.py.
function dataDe(dia) {
  const d = new Date();
  if (dia === "amanha") d.setDate(d.getDate() + 1);
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

function quandoFalado(r) {
  return `${r.dia === "amanha" ? "amanhã" : "hoje"} ${TURNOS[r.turno]}`;
}

function textoDaLista(r) {
  const linhas = r.itens.map((i) => `- ${i}`).join("\n");
  if (r.tipo === "lembrete") {
    return `Cria estes LEMBRETES na Lylu, um recado para cada (lylu_placa.py recado), para ${quandoFalado(r)}: ` +
      `--quando ${dataDe(r.dia)} --turno ${r.turno}.\n${linhas}\n` +
      "São lembretes, não tarefas: nada na missão nem no bônus. No fim, me confirma em uma frase.";
  }
  if (r.tipo === "habito") {
    return `Acrescenta estes HÁBITOS na aba Hábitos da Lylu:\n${linhas}\n` +
      "Olha os hábitos que já estão no estado da placa e manda a lista inteira com lylu_placa.py habitos: o comando " +
      "substitui tudo (os que continuam guardam a sequência) e a placa mostra até 6; se passar disso, me pergunta " +
      "qual sai. São hábitos de todo dia, não tarefas nem lembretes. No fim, me confirma em uma frase.";
  }
  return "Adiciona estas TAREFAS no meu Todoist, sem apagar nem concluir nenhuma que já existe:\n" +
    `${linhas}\n` +
    "Depois atualiza a Lylu com a lista inteira: as que já estavam lá e não foram feitas, mais estas. " +
    "O comando tarefas substitui tudo, então não mande só as novas. São tarefas, não lembretes. " +
    "No fim, me confirma em uma frase.";
}

function quantos(n, coisa = "item") {
  if (coisa === "item") return n === 1 ? "1 item" : `${n} itens`;
  return n === 1 ? `1 ${coisa}` : `${n} ${coisa}s`;
}

// Aceita lista colada de outro lugar: uma por linha, sem o "-", "•", "1." do começo.
function separaItens(texto) {
  return texto
    .split("\n")
    .map((l) => l.replace(/^\s*(?:[-*•·]|\d+[.)])\s*/, "").trim())
    .filter(Boolean);
}

function guardaRascunho() {
  try {
    localStorage.setItem(RASCUNHO, JSON.stringify(rascunho));
  } catch {
    // sem armazenamento: o rascunho só vive enquanto a tela estiver aberta
  }
}

function desenhaLista() {
  const lembrete = rascunho.tipo === "lembrete";
  const habito = rascunho.tipo === "habito";
  tipoTarefa.setAttribute("aria-pressed", String(rascunho.tipo === "tarefa"));
  tipoLembrete.setAttribute("aria-pressed", String(lembrete));
  tipoHabito.setAttribute("aria-pressed", String(habito));
  quandoBox.hidden = !lembrete;
  for (const b of quandoBox.querySelectorAll(".chip")) {
    const vale = b.dataset.dia ? b.dataset.dia === rascunho.dia : b.dataset.turno === rascunho.turno;
    b.setAttribute("aria-pressed", String(vale));
  }
  itemTexto.placeholder = lembrete ? "ir na farmácia comprar remédio" : habito ? "beber água" : "escrever o relatório";
  itemTexto.setAttribute("aria-label", lembrete ? "Novo lembrete" : habito ? "Novo hábito" : "Nova tarefa");

  itensLista.replaceChildren(...rascunho.itens.map((texto, i) => {
    const li = document.createElement("li");
    const t = document.createElement("span");
    t.textContent = texto;
    const tira = document.createElement("button");
    tira.type = "button";
    tira.textContent = "✕";
    tira.setAttribute("aria-label", `Tirar “${texto}”`);
    tira.addEventListener("click", () => {
      rascunho.itens.splice(i, 1);
      desenhaLista();
    });
    li.append(t, tira);
    return li;
  }));
  const n = rascunho.itens.length;
  itensVazio.hidden = n > 0;
  itensVazio.textContent = lembrete ? "Escreve um lembrete e toca em +."
    : habito ? "Escreve um hábito e toca em +." : "Escreve uma tarefa e toca em +.";
  // O botão diz o que vai acontecer; o texto que ficou no campo também conta.
  const total = n + separaItens(itemTexto.value).length;
  mandarLista.disabled = !total;
  mandarLista.textContent = !total ? "Mandar pro Hermes"
    : lembrete ? `Lembrar ${total === 1 ? "disso" : `de ${total} coisas`} ${quandoFalado(rascunho)}`
    : habito ? `Criar ${quantos(total, "hábito")}`
    : `Mandar ${quantos(total, "tarefa")} pro Hermes`;
  guardaRascunho();
}

function poeNaLista() {
  const novos = separaItens(itemTexto.value);
  if (!novos.length) return;
  rascunho.itens.push(...novos);
  itemTexto.value = "";
  desenhaLista();
}

function mostraTela(qual) {
  if (gravacao) return; // não larga uma gravação no meio
  const naLista = qual === "lista";
  abaConversa.setAttribute("aria-pressed", String(!naLista));
  abaLista.setAttribute("aria-pressed", String(naLista));
  telaLista.hidden = !naLista;
  conversa.hidden = naLista;
  caixa.hidden = naLista;
  aviso.hidden = true;
  atualizaPersonagem();
  if (naLista) itemTexto.focus();
  else conversa.scrollTop = conversa.scrollHeight;
}

novoItem.addEventListener("submit", (e) => {
  e.preventDefault();
  poeNaLista();
  itemTexto.focus();
});
itemTexto.addEventListener("input", desenhaLista);

tipoTarefa.addEventListener("click", () => { rascunho.tipo = "tarefa"; desenhaLista(); });
tipoLembrete.addEventListener("click", () => { rascunho.tipo = "lembrete"; desenhaLista(); });
tipoHabito.addEventListener("click", () => { rascunho.tipo = "habito"; desenhaLista(); });
quandoBox.addEventListener("click", (e) => {
  const b = e.target.closest(".chip");
  if (!b) return;
  if (b.dataset.dia) rascunho.dia = b.dataset.dia;
  else rascunho.turno = b.dataset.turno;
  desenhaLista();
});

mandarLista.addEventListener("click", () => {
  poeNaLista();                                   // escreveu e foi direto no botão: vai junto
  if (!rascunho.itens.length) return;
  liberaSom();
  const lembrete = rascunho.tipo === "lembrete";
  const m = nova({
    tipo: "lista", lista_tipo: rascunho.tipo, itens: [...rascunho.itens],
    quando_lembrete: lembrete ? quandoFalado(rascunho) : null, texto: textoDaLista(rascunho),
  });
  rascunho = { tipo: rascunho.tipo, itens: [], dia: "hoje", turno: turnoDeAgora() };
  desenhaLista();
  mostraTela("conversa");
  manda(m);
});

verAntesBotao.addEventListener("click", () => {
  verAntes = !verAntes;
  desenha();
});
limpar.addEventListener("click", () => {
  if (!confirm("Apagar as conversas guardadas neste celular?")) return;
  mensagens = mensagens.filter((m) => m.aguardando || m.estado === "enviando");
  verAntes = false;
  guarda();
  desenha();
});
// O app fica aberto de um dia para o outro: ao voltar, a conversa de ontem sai.
document.addEventListener("visibilitychange", () => {
  if (!document.hidden) desenha();
});

abaConversa.addEventListener("click", () => mostraTela("conversa"));
abaLista.addEventListener("click", () => mostraTela("lista"));

try {
  const guardado = localStorage.getItem(RASCUNHO) || "";
  if (guardado.startsWith("{")) {
    const r = JSON.parse(guardado);
    rascunho = { ...rascunho, ...r, itens: Array.isArray(r.itens) ? r.itens.filter((i) => typeof i === "string") : [] };
    if (!TURNOS[rascunho.turno]) rascunho.turno = turnoDeAgora();
  } else {
    rascunho.itens = separaItens(guardado);       // rascunho da versão antiga (caixa de texto)
  }
} catch {
  // sem armazenamento (ou rascunho estragado): começa vazio
}
desenhaLista();

// ---------------------------------------------------------------- áudio

// O celular grava no formato dele (webm, mp4…); aqui vira PCM 16 kHz mono,
// o mesmo que a placa manda.
async function paraPcm(blob) {
  const ctx = new AudioContext();
  let som;
  try {
    som = await ctx.decodeAudioData(await blob.arrayBuffer());
  } finally {
    ctx.close();
  }
  const off = new OfflineAudioContext(1, Math.ceil(som.duration * TAXA), TAXA);
  const fonte = off.createBufferSource();
  fonte.buffer = som;
  fonte.connect(off.destination);
  fonte.start();
  const f = (await off.startRendering()).getChannelData(0);
  const pcm = new Int16Array(f.length); // little-endian em todo celular
  for (let i = 0; i < f.length; i++) {
    const s = Math.max(-1, Math.min(1, f[i]));
    pcm[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
  }
  return new Uint8Array(pcm.buffer);
}

async function comecaGravacao() {
  if (gravacao) return;
  paraSom(); // não grava a própria Lylu falando
  falandoVoz = false;
  if (!navigator.mediaDevices?.getUserMedia || !window.MediaRecorder) {
    avisa("O microfone só funciona abrindo o app pelo endereço https.");
    return;
  }
  let fluxo;
  try {
    fluxo = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true },
    });
  } catch (e) {
    avisa(e.name === "NotAllowedError" ? "Preciso da sua permissão para usar o microfone." : "Não consegui abrir o microfone.");
    return;
  }
  const rec = new MediaRecorder(fluxo);
  const partes = [];
  rec.ondataavailable = (e) => {
    if (e.data.size) partes.push(e.data);
  };
  rec.start();
  gravacao = { rec, fluxo, partes, inicio: Date.now(), relogio: setInterval(contaTempo, 250) };
  atualizaPersonagem();
  aviso.hidden = true;
  caixa.hidden = true;
  gravando.hidden = false;
  contaTempo();
}

function contaTempo() {
  const ms = Date.now() - gravacao.inicio;
  tempo.textContent = duracao(Math.min(ms, MAX_GRAVACAO_MS));
  if (ms >= MAX_GRAVACAO_MS) terminaGravacao(true);
}

function terminaGravacao(mandar) {
  const g = gravacao;
  if (!g) return;
  gravacao = null;
  atualizaPersonagem();
  clearInterval(g.relogio);
  gravando.hidden = true;
  caixa.hidden = false;
  g.rec.onstop = async () => {
    g.fluxo.getTracks().forEach((t) => t.stop());
    if (!mandar) return;
    let pcm;
    try {
      pcm = await paraPcm(new Blob(g.partes, { type: g.rec.mimeType }));
    } catch {
      avisa("Não consegui preparar o áudio.");
      return;
    }
    const ms = Math.round((pcm.length / 2 / TAXA) * 1000);
    if (ms < MIN_GRAVACAO_MS) {
      avisa("Ficou curtinho demais, grava de novo.");
      return;
    }
    const m = nova({ tipo: "audio", ms });
    audios.set(m.id, pcm);
    manda(m);
  };
  g.rec.stop();
}

falar.addEventListener("click", comecaGravacao);
document.getElementById("mandar-audio").addEventListener("click", () => {
  liberaSom();
  terminaGravacao(true);
});
document.getElementById("cancelar").addEventListener("click", () => terminaGravacao(false));

desenha();
ajustaCaixa();
agendaEspera(); // se fechou o app esperando uma resposta, continua esperando
