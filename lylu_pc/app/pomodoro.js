// O Pomodoro da Lylu no PC, com os passos e os tempos da tela Foco da placa
// (lylu_p4/main/main.c): escolher a tarefa → foco → pausa → foco… O relógio conta
// pela hora de término, então dormir o PC não atrasa nada.
import { criaAnimacao } from "./compartilhado/animacao.js";

const PADRAO = { foco: 25, curta: 5, blocos: 4, longa: 20, sozinha: true, som: true, telaCheia: true };
const AVISO_CHEIA_S = 10; // antes de cobrir a tela, dá tempo de terminar a frase
const SEGURA_MS = 3000;
// Sem ninguém no PC o ciclo não gira sozinho (ela reclamou de pausa atrás de pausa):
const AUSENTE_S = 60; // no fim da pausa, parado há mais que isso = ainda não voltou
const ATRASO_MAX_MS = 2 * 60 * 1000; // acabou há mais que isso = PC dormindo ou programa fechado
const AJUSTES = [
  { chave: "foco", nome: "Tempo de foco", un: " min", min: 5, max: 90, passo: 5 },
  { chave: "curta", nome: "Pausa curta", un: " min", min: 1, max: 30, passo: 1 },
  { chave: "blocos", nome: "Blocos até a longa", un: "", min: 1, max: 8, passo: 1 },
  { chave: "longa", nome: "Pausa longa", un: " min", min: 5, max: 60, passo: 5 },
];
// Os mesmos cuidados da pausa na placa.
const CUIDADO = [
  ["Água", "Um copo inteiro, tá? Eu espero."],
  ["Levantar", "Vai até a janela e volta. O corpo agradece."],
  ["Alongar", "Ombro longe da orelha. Devagarinho."],
];
// A voz da Lylu (Francisca, a mesma da placa; textos em sons/MANIFESTO.json).
// Só existe para os tempos padrão: com outra pausa, fica só o toque.
// Ficam carregadas desde o começo: carregar na hora atrasava a fala (ela reclamou).
const VOZES = Object.fromEntries(Object.entries({
  "pausa-5": "sons/pausa_5min.wav",
  "pausa-20": "sons/pausa_20min.wav",
  "pausa-fim": "sons/pausa_fim.wav",
}).map(([chave, arquivo]) => {
  const audio = new Audio(arquivo);
  audio.preload = "auto";
  return [chave, audio];
}));
const VOZ_FALA_MS = 5800; // o tempo da boca mexendo (as falas têm 4,4 a 5,5 s)
const TAMANHO = { compacta: [250, 452], aberta: [300, 540] };
const MAX_RECENTES = 5;
const FALA_MS = 4000;

const $ = (id) => document.getElementById(id);
const cartao = $("cartao");
const fala = $("fala");
const animacao = criaAnimacao($("lylu"), "compartilhado/lylu-conversa-v3.webp");

// ---------------------------------------------------------------- guardado
function le(chave, padrao) {
  try {
    const v = JSON.parse(localStorage.getItem(chave));
    return v ?? padrao;
  } catch {
    return padrao;
  }
}
function grava(chave, valor) {
  try {
    localStorage.setItem(chave, JSON.stringify(valor));
  } catch {
    // sem armazenamento: vale só até fechar
  }
}

const pomo = { ...PADRAO, ...le("lylu_foco_ajustes", {}) };
let recentes = le("lylu_foco_recentes", []);
const hoje = () => new Date().toLocaleDateString("sv"); // AAAA-MM-DD, no fuso dela
let feitos = le("lylu_foco_feitos", { dia: hoje(), n: 0 }); // blocos de foco
let concluidas = le("lylu_foco_concluidas", { dia: hoje(), lista: [] }); // tarefas

// ---------------------------------------------------------------- estado
let passo = "escolher"; // escolher | foco | pausa | ajustes
let antesDosAjustes = "escolher";
let tarefa = "";
let bloco = 1;
let pausaLonga = false;
let cuidado = -1;
const relogio = { total: 25 * 60, resto: 25 * 60, fim: 0 }; // fim > 0: correndo
let falaDaVez = null; // { texto, forte, ate }
let avisouCheia = false;
let janelaCheia = false;
let tarefaFeita = false; // concluída durante a pausa: depois dela, escolher a próxima
let esperandoVolta = false; // pausa acabou com ela longe: o foco começa quando ela mexer no PC

const correndo = () => relogio.fim > 0;
const restoAgora = () => (correndo() ? Math.max(0, Math.ceil((relogio.fim - Date.now()) / 1000)) : relogio.resto);
function arma(min) {
  relogio.total = relogio.resto = min * 60;
  relogio.fim = 0;
  avisouCheia = false;
}
function liga() {
  relogio.fim = Date.now() + relogio.resto * 1000;
}
function desliga() {
  relogio.resto = restoAgora();
  relogio.fim = 0;
}

// Se o programa fechou no meio de um foco, continua de onde estava.
function guardaSessao() {
  grava("lylu_foco_sessao", { passo: passo === "ajustes" ? antesDosAjustes : passo, tarefa, bloco, pausaLonga, tarefaFeita, relogio });
}
function retomaSessao() {
  const s = le("lylu_foco_sessao", null);
  if (!s || !["escolher", "foco", "pausa"].includes(s.passo)) return;
  ({ passo, tarefa, bloco, pausaLonga } = s);
  tarefaFeita = !!s.tarefaFeita;
  Object.assign(relogio, s.relogio);
}

function contaFeito() {
  if (feitos.dia !== hoje()) feitos = { dia: hoje(), n: 0 };
  feitos.n++;
  grava("lylu_foco_feitos", feitos);
}

// ---------------------------------------------------------------- avisos
function diz(texto, forte = false, ms = FALA_MS) {
  falaDaVez = { texto, forte, ate: Date.now() + ms };
  setTimeout(desenha, ms + 50);
}

// Dois toques suaves, nada de alarme.
function toca() {
  if (!pomo.som) return;
  const ctx = new AudioContext();
  [659.25, 880].forEach((freq, i) => {
    const o = ctx.createOscillator();
    const g = ctx.createGain();
    const t = ctx.currentTime + i * 0.18;
    o.frequency.value = freq;
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(0.12, t + 0.02);
    g.gain.exponentialRampToValueAtTime(0.0001, t + 0.9);
    o.connect(g).connect(ctx.destination);
    o.start(t);
    o.stop(t + 1);
  });
  setTimeout(() => ctx.close(), 1500);
}

// A fala do momento, na hora; sem fala para esse momento, o toque.
function avisaComSom(chave) {
  if (!pomo.som) return false;
  const audio = VOZES[chave];
  if (!audio) {
    toca();
    return false;
  }
  audio.currentTime = 0;
  audio.play().catch(() => toca());
  return true;
}

function notifica(titulo, corpo) {
  if (!document.hidden && document.hasFocus()) return; // ela já está olhando
  try {
    new Notification(titulo, { body: corpo, icon: "compartilhado/lylu-rosto.png", silent: true });
  } catch {
    // sem notificação: o balão e o som bastam
  }
}

// ---------------------------------------------------------------- passos
function comeca(t) {
  tarefa = t.trim().slice(0, 80);
  if (!tarefa) return;
  recentes = [tarefa, ...recentes.filter((r) => r.toLowerCase() !== tarefa.toLowerCase())].slice(0, MAX_RECENTES);
  grava("lylu_foco_recentes", recentes);
  arma(pomo.foco);
  liga();
  passo = "foco";
  diz("Bora! Tô aqui com você.");
  mudou();
}

function fimDoFoco() {
  desliga();
  contaFeito();
  pausaLonga = bloco >= pomo.blocos;
  const min = pausaLonga ? pomo.longa : pomo.curta;
  arma(min);
  passo = "pausa";
  cuidado = -1;
  if (pomo.sozinha) liga();
  const falou = avisaComSom(`pausa-${min}`);
  diz(`Bloco fechado! Pausa ${pausaLonga ? "longa" : "curta"} de ${min} min.`, true, falou ? VOZ_FALA_MS : FALA_MS);
  notifica("Bloco fechado!", `Pausa de ${min} min. ${tarefa}`);
  mudou();
}

function fimDaPausa(pulou = false) {
  desliga();
  bloco = pausaLonga ? 1 : bloco + 1;
  arma(pomo.foco);
  if (tarefaFeita) {
    // A tarefa acabou durante a pausa: agora é escolher a próxima.
    tarefaFeita = false;
    tarefa = "";
    passo = "escolher";
    if (!pulou) {
      diz("Pausa feita! Qual é a próxima?", true);
      toca();
      notifica("Fim da pausa", "Qual é a próxima tarefa?");
    }
    return mudou();
  }
  passo = "foco";
  if (pulou) {
    if (pomo.sozinha) liga(); // segurou o botão: está aqui
    return mudou();
  }
  const falou = avisaComSom("pausa-fim");
  notifica("Fim da pausa", `Bora voltar? ${tarefa}`);
  if (pomo.sozinha) comecaQuandoVoltar(falou);
  else diz("A pausa acabou. Volta quando estiver pronta.", true, falou ? VOZ_FALA_MS : FALA_MS);
  mudou();
}

// Avançar sozinha só com ela no PC. Se ela ainda está longe, o foco espera e
// começa quando ela mexer no mouse ou no teclado.
async function comecaQuandoVoltar(falou = false) {
  const parado = (await window.lylu?.paradoHa?.()) ?? 0;
  if (parado < AUSENTE_S) {
    liga();
    diz("A pausa acabou. Tô aqui com você.", true, falou ? VOZ_FALA_MS : FALA_MS);
    return mudou();
  }
  esperandoVolta = true;
  mudou();
  const vigia = setInterval(async () => {
    if (!esperandoVolta || passo !== "foco" || correndo()) {
      clearInterval(vigia);
      esperandoVolta = false;
      return desenha();
    }
    if ((await window.lylu.paradoHa()) < 3) {
      clearInterval(vigia);
      esperandoVolta = false;
      liga();
      diz("Voltou! Bora, tô aqui com você.", true);
      mudou();
    }
  }, 2000);
}

// O tempo acabou com o PC dormindo ou o programa fechado: ela não estava aqui.
// Nada de pausa pulando na tela nem som; só volta pra escolher, com um recado.
function acabouLonge() {
  desliga();
  arma(pomo.foco);
  tarefa = "";
  tarefaFeita = false;
  esperandoVolta = false;
  passo = "escolher";
  diz("O relógio acabou enquanto você estava fora. Bora de novo?", false, 8000);
  mudou();
}

// Concluir: comemora, guarda nas "concluídas hoje" e tira dos recentes. No foco,
// volta pra escolher a próxima; na pausa, a pausa continua (descansar também conta).
function concluir() {
  if (!tarefa || tarefaFeita) return;
  if (concluidas.dia !== hoje()) concluidas = { dia: hoje(), lista: [] };
  concluidas.lista.push(tarefa);
  grava("lylu_foco_concluidas", concluidas);
  recentes = recentes.filter((r) => r.toLowerCase() !== tarefa.toLowerCase());
  grava("lylu_foco_recentes", recentes);
  toca();
  if (passo === "pausa") {
    tarefaFeita = true;
    diz("Tarefa concluída! Aproveita a pausa.", true);
    return mudou();
  }
  diz(`Concluída! Mandou bem.`, true);
  desliga();
  arma(pomo.foco);
  tarefa = "";
  passo = "escolher";
  mudou();
}

// Cancelar (antes de começar) e Parar (depois) zeram e voltam pra lista, sem drama.
function volta() {
  esperandoVolta = false;
  desliga();
  arma(pomo.foco);
  passo = "escolher";
  bloco = 1;
  mudou();
}

function principal() {
  esperandoVolta = false;
  if (correndo()) desliga();
  else liga();
  mudou();
}

function abreAjustes() {
  if (passo === "ajustes") return fechaAjustes();
  antesDosAjustes = passo;
  passo = "ajustes";
  mudou();
}

function fechaAjustes() {
  grava("lylu_foco_ajustes", pomo);
  passo = antesDosAjustes;
  // Relógio parado e não começado: pega o tempo novo na hora.
  if (!correndo() && relogio.resto === relogio.total) {
    if (passo === "foco" || passo === "escolher") arma(pomo.foco);
    else if (passo === "pausa") arma(pausaLonga ? pomo.longa : pomo.curta);
  }
  mudou();
}

// ---------------------------------------------------------------- tela
const mmss = (s) => `${String(Math.floor(s / 60)).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`;

function falaPadrao() {
  if (passo === "escolher") return "No que a gente foca agora?";
  if (passo === "ajustes") return "Do jeito que for melhor pra você.";
  if (passo === "pausa") {
    if (cuidado >= 0) return CUIDADO[cuidado][1];
    return pomo.telaCheia ? "Hora de levantar! Eu seguro tudo aqui." : "Pausa! Quer um cuidado?";
  }
  if (correndo()) return "";
  if (esperandoVolta) return "Bora voltar? Começo quando você mexer no PC.";
  return relogio.resto === relogio.total ? "Quando você estiver pronta." : "Pausei. Tô aqui.";
}

function estadoDaLylu() {
  if (falaDaVez && Date.now() < falaDaVez.ate) return "falando";
  if (passo === "ajustes") return "pensando";
  if (passo === "foco" && correndo()) return "ouvindo";
  return "conversando";
}

function tique() {
  if (correndo() && restoAgora() === 0) {
    if (Date.now() - relogio.fim > ATRASO_MAX_MS) return acabouLonge();
    return passo === "foco" ? fimDoFoco() : fimDaPausa();
  }
  if (passo !== "foco" && passo !== "pausa") return;
  const resto = restoAgora();
  if (passo === "foco" && correndo() && pomo.telaCheia && !avisouCheia && resto <= AVISO_CHEIA_S) {
    avisouCheia = true;
    diz(`Daqui a ${resto} s eu cubro a tela. Termina a frase!`, true, resto * 1000);
    desenha();
  }
  const fracao = (relogio.total - resto) / relogio.total;
  $("tempo").textContent = mmss(resto);
  $("progresso").style.width = `${fracao * 100}%`;
  // O título aparece ao passar o mouse no botão da barra de tarefas.
  document.title = `${mmss(resto)} · ${passo === "foco" ? tarefa : "pausa"}`;
  mandaProgresso(fracao, correndo() ? "normal" : "paused");
}

let progressoEnviado = "";
function mandaProgresso(fracao, modo) {
  const chave = `${fracao.toFixed(3)}|${modo}`;
  if (chave === progressoEnviado) return;
  progressoEnviado = chave;
  window.lylu?.progresso(fracao, modo);
}

function desenha() {
  const aberta = passo === "escolher" || passo === "ajustes";
  const cheia = passo === "pausa" && pomo.telaCheia;
  cartao.classList.toggle("aberta", aberta);
  cartao.classList.toggle("pausa", passo === "pausa");
  document.body.classList.toggle("cheia", cheia);
  window.lylu?.tamanho(...TAMANHO[aberta ? "aberta" : "compacta"]);
  if (cheia !== janelaCheia) {
    janelaCheia = cheia;
    window.lylu?.telaCheia(cheia);
  }

  const vez = falaDaVez && Date.now() < falaDaVez.ate ? falaDaVez : null;
  fala.textContent = vez ? vez.texto : falaPadrao();
  fala.classList.toggle("forte", !!vez?.forte);
  animacao.setEstado(estadoDaLylu());

  if (passo !== "foco" && passo !== "pausa") {
    document.title = "Lylu Foco";
    mandaProgresso(-1, "none");
  }
  $("t-escolher").hidden = passo !== "escolher";
  $("t-relogio").hidden = passo !== "foco" && passo !== "pausa";
  $("t-ajustes").hidden = passo !== "ajustes";

  if (concluidas.dia !== hoje()) concluidas = { dia: hoje(), lista: [] };
  if (passo === "escolher") {
    $("rot-feitas").hidden = !concluidas.lista.length;
    // Concluída por engano: o × desfaz (e desconta do dia).
    const inicio = Math.max(0, concluidas.lista.length - 4);
    $("feitas").replaceChildren(...concluidas.lista.slice(inicio).map((t, i) =>
      itemComApagar(t, "Desfazer: não concluí", () => {
        concluidas.lista.splice(inicio + i, 1);
        grava("lylu_foco_concluidas", concluidas);
        desenha();
      })));
    $("rot-recentes").hidden = !recentes.length;
    $("recentes").replaceChildren(...recentes.map((r) => {
      const li = itemComApagar(r, "Tirar das recentes", () => {
        recentes = recentes.filter((x) => x !== r);
        grava("lylu_foco_recentes", recentes);
        desenha();
      });
      li.addEventListener("click", () => comeca(r));
      return li;
    }));
  }

  if (passo === "foco" || passo === "pausa") {
    const comecado = relogio.resto < relogio.total || correndo();
    $("nome-fase").textContent = passo === "foco" ? "foco" : pausaLonga ? "pausa longa" : "pausa curta";
    $("tarefa-atual").textContent = tarefaFeita ? `✓ ${tarefa}` : tarefa;
    $("tarefa-atual").title = tarefa;
    $("b-concluir").hidden = !tarefa || tarefaFeita;
    $("cuidados").hidden = passo !== "pausa";
    $("cuidados").replaceChildren(...CUIDADO.map(([nome], i) => {
      const li = document.createElement("li");
      li.textContent = nome;
      li.classList.toggle("escolhido", i === cuidado);
      li.addEventListener("click", () => { cuidado = i; desenha(); });
      return li;
    }));
    $("b-principal").textContent = correndo() ? "Pausar" : comecado ? "Continuar" : passo === "pausa" ? "Começar pausa" : "Começar";
    $("b-secundario").textContent = passo === "pausa" ? "Pular" : comecado ? "Parar" : "Cancelar";
    // Na tela cheia a pausa não se pula com um clique (só segurando), mas dá para
    // pausar e minimizar: preso demais não ajudava (ela pediu os dois de volta).
    $("b-principal").hidden = false;
    $("b-secundario").hidden = cheia;
    $("b-segura").hidden = !cheia;
  }

  if (passo === "ajustes") {
    $("lista-ajustes").replaceChildren(...AJUSTES.map((a) => {
      const li = document.createElement("li");
      const nome = document.createElement("span");
      nome.className = "nome";
      nome.textContent = a.nome;
      const valor = document.createElement("span");
      valor.className = "valor";
      valor.textContent = `${pomo[a.chave]}${a.un}`;
      const muda = (sinal) => {
        pomo[a.chave] = Math.min(a.max, Math.max(a.min, pomo[a.chave] + sinal * a.passo));
        desenha();
      };
      li.append(nome, botaoIcone("−", "Menos", () => muda(-1)), valor, botaoIcone("+", "Mais", () => muda(1)));
      return li;
    }));
    $("a-sozinha").checked = pomo.sozinha;
    $("a-som").checked = pomo.som;
    $("a-cheia").checked = pomo.telaCheia;
  }

  if (feitos.dia !== hoje()) feitos = { dia: hoje(), n: 0 };
  const nb = feitos.n;
  const nt = concluidas.lista.length;
  const hojeTxt = !nb && !nt ? "começando o dia"
    : `hoje: ${nb} ${nb === 1 ? "bloco" : "blocos"} · ${nt} ${nt === 1 ? "tarefa" : "tarefas"} ✓`;
  $("blocos").textContent = passo === "escolher" ? hojeTxt : `Bloco ${bloco} de ${pomo.blocos} · ${hojeTxt}`;
  tique();
}

function itemComApagar(texto, rotulo, apaga) {
  const li = document.createElement("li");
  const nome = document.createElement("span");
  nome.textContent = texto;
  nome.title = texto;
  const x = botaoIcone("×", rotulo, (e) => {
    e.stopPropagation(); // na recente, o clique no item começaria o foco
    apaga();
  });
  x.classList.add("apaga");
  x.title = rotulo;
  li.append(nome, x);
  return li;
}

function botaoIcone(texto, rotulo, acao) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "icone";
  b.textContent = texto;
  b.setAttribute("aria-label", rotulo);
  b.addEventListener("click", acao);
  return b;
}

function mudou() {
  guardaSessao();
  desenha();
}

// ---------------------------------------------------------------- botões
$("f-tarefa").addEventListener("submit", (e) => {
  e.preventDefault();
  comeca($("tarefa").value);
  $("tarefa").value = "";
});
$("b-principal").addEventListener("click", principal);
$("b-secundario").addEventListener("click", () => (passo === "pausa" ? fimDaPausa(true) : volta()));
$("b-concluir").addEventListener("click", concluir);
$("b-ajustes").addEventListener("click", abreAjustes);
$("b-pronto").addEventListener("click", fechaAjustes);
$("b-esconder").addEventListener("click", () => window.lylu?.esconder());
// A sessão já está guardada (guardaSessao): ao abrir de novo, continua de onde parou.
$("b-fechar").addEventListener("click", () => window.lylu?.fechar());
$("a-sozinha").addEventListener("change", (e) => { pomo.sozinha = e.target.checked; });
$("a-som").addEventListener("change", (e) => { pomo.som = e.target.checked; if (pomo.som) toca(); });
$("a-cheia").addEventListener("change", (e) => { pomo.telaCheia = e.target.checked; });

const segura = $("b-segura");
let seguraRelogio = 0;
let seguraDesde = 0;
segura.addEventListener("pointerdown", () => {
  segura.classList.add("segurando");
  seguraDesde = Date.now();
  seguraRelogio = setTimeout(() => {
    seguraDesde = 0;
    segura.classList.remove("segurando");
    fimDaPausa(true);
  }, SEGURA_MS);
});
for (const ev of ["pointerup", "pointerleave", "pointercancel"]) {
  segura.addEventListener(ev, () => {
    // Soltou antes da hora: um toque rápido parecia botão quebrado (28/10).
    if (ev === "pointerup" && seguraDesde) {
      diz("Segure o botão por 3 segundos para voltar.", false, 3500);
      desenha();
    }
    seguraDesde = 0;
    segura.classList.remove("segurando");
    clearTimeout(seguraRelogio);
  });
}
$("a-windows").addEventListener("change", (e) => window.lylu?.defineAbreComWindows(e.target.checked));
window.lylu?.abreComWindows().then((sim) => { $("a-windows").checked = sim; });

retomaSessao();
desenha();
setInterval(tique, 250);
