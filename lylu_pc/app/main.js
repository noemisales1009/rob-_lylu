// Lylu Foco: a Lylu num canto da tela, sempre por cima, com o relógio do Pomodoro.
// A janela não tem moldura (o cartão é desenhado no CSS). Ela fica na barra de
// tarefas (com o progresso do foco no botão) e também na bandeja, perto do relógio.
const { app, BrowserWindow, Tray, Menu, ipcMain, nativeImage, screen, shell, powerMonitor } = require("electron");
const path = require("node:path");
const fs = require("node:fs");

// .ico com vários tamanhos: é o que o Windows usa na barra de tarefas e na bandeja.
const ROSTO = path.join(__dirname, "compartilhado", "lylu.ico");
// Identidade do programa no Windows (agrupa a janela com o atalho fixado). O teste
// e o "npm start" usam outra: o electron.exe cru tem o ícone de átomo, e o Windows
// guardava esse ícone para a identidade do programa de verdade.
const AUMID = app.isPackaged ? "br.noemi.lylufoco" : "br.noemi.lylufoco.dev";
const INICIAL = { width: 250, height: 452 };
let janela = null;
let bandeja = null;

// No teste, os dados (posição, tarefas, tempos) ficam numa pasta à parte.
if (process.env.LYLU_TESTE) app.setPath("userData", path.join(process.env.LYLU_TESTE, "dados"));
if (!app.requestSingleInstanceLock()) app.quit();
app.setAppUserModelId(AUMID);
app.on("second-instance", () => mostra());

// ---- registro: o que aconteceu com a janela (para achar erro de posição/minimizar)
function anota(...coisas) {
  try {
    const arq = path.join(app.getPath("userData"), "registro.txt");
    if (fs.existsSync(arq) && fs.statSync(arq).size > 200_000) fs.renameSync(arq, `${arq}.antigo`);
    fs.appendFileSync(arq, `${new Date().toISOString()} ${coisas.map((c) => (typeof c === "string" ? c : JSON.stringify(c))).join(" ")}\n`);
  } catch {
    // sem registro: tudo bem
  }
}

// ---- onde ela fica: lembra a posição, e volta pro canto se a tela mudou
const arquivoPosicao = () => path.join(app.getPath("userData"), "posicao.json");

function areaDe(b) {
  return screen.getDisplayMatching(b).workArea;
}

function dentro(b) {
  const a = areaDe(b);
  const x = Math.min(Math.max(b.x, a.x), a.x + a.width - b.width);
  const y = Math.min(Math.max(b.y, a.y), a.y + a.height - b.height);
  return { ...b, x, y };
}

// O cartão cresce para cima e para a esquerda, com o canto de baixo à direita
// parado: ela costuma morar no canto de baixo da tela. O canto fica guardado, e
// não relido da janela, porque a escala do Windows arredonda e ela escorregaria
// um pixel a cada troca.
let canto = null; // { direita, baixo }
let tamanho = [INICIAL.width, INICIAL.height];
let ajustando = false;

function posicaoInicial() {
  try {
    canto = JSON.parse(fs.readFileSync(arquivoPosicao(), "utf8"));
    if (typeof canto.direita !== "number") throw new Error("formato antigo");
  } catch {
    const a = screen.getPrimaryDisplay().workArea;
    canto = { direita: a.x + a.width - 24, baixo: a.y + a.height - 24 };
  }
  const b = dentro({ ...INICIAL, x: canto.direita - INICIAL.width, y: canto.baixo - INICIAL.height });
  canto = { direita: b.x + b.width, baixo: b.y + b.height };
  return b;
}

// Ela arrastou: o canto novo é onde ela largou.
function arrastou() {
  if (ajustando) return;
  const b = janela.getBounds();
  canto = { direita: b.x + tamanho[0], baixo: b.y + tamanho[1] };
  fs.writeFileSync(arquivoPosicao(), JSON.stringify(canto));
}

// Mexer no tamanho com a janela minimizada estraga a volta: o Windows a restaura
// na posição antiga com o tamanho novo (a pausa em tela cheia saiu do monitor,
// 28/10). Minimizada, o tamanho fica guardado e vale quando ela voltar.
let pendente = null;
function aplica(b) {
  anota("aplica", b, { minimizada: janela.isMinimized(), visivel: janela.isVisible() });
  if (janela.isMinimized()) {
    pendente = b;
    return;
  }
  ajustando = true;
  janela.setBounds(b);
  setTimeout(() => { ajustando = false; }, 300);
}

function redimensiona(w, h) {
  if (tamanho[0] === w && tamanho[1] === h) return;
  tamanho = [w, h];
  if (cheia) return; // vale quando a pausa acabar
  const b = dentro({ x: canto.direita - w, y: canto.baixo - h, width: w, height: h });
  canto = { direita: b.x + w, baixo: b.y + h };
  aplica(b);
}

// A pausa em tela cheia: ela pediu para ser obrigada a levantar. Cobre o monitor
// onde a Lylu mora, por cima até da barra de tarefas, e volta pro canto no fim.
let cheia = false;
function telaCheia(sim) {
  if (sim === cheia) return;
  anota("telaCheia", sim, { minimizada: janela.isMinimized(), visivel: janela.isVisible() });
  cheia = sim;
  // Minimizada: primeiro volta, depois muda de tamanho (ver aplica). Na pausa ela
  // aparece; no fim da pausa, se tinha sido minimizada, a Lylu reaparece no canto.
  pendente = null; // o tamanho certo é o daqui, não um guardado de antes
  if (janela.isMinimized() && !process.env.LYLU_TESTE) janela.restore();
  if (sim) {
    janela.setAlwaysOnTop(true, "screen-saver");
    aplica(screen.getDisplayNearestPoint({ x: canto.direita - 1, y: canto.baixo - 1 }).bounds);
    if (!process.env.LYLU_TESTE) mostra();
  } else {
    janela.setAlwaysOnTop(true, "floating");
    const [w, h] = tamanho;
    aplica(dentro({ x: canto.direita - w, y: canto.baixo - h, width: w, height: h }));
  }
  if (bandeja) atualizaMenu();
}

function criaJanela() {
  janela = new BrowserWindow({
    ...posicaoInicial(),
    frame: false,
    transparent: true,
    resizable: false,
    maximizable: false,
    alwaysOnTop: true,
    minimizable: true,
    hasShadow: false,
    show: false,
    paintWhenInitiallyHidden: true,
    icon: ROSTO,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      autoplayPolicy: "no-user-gesture-required",
      // Minimizada, o Chromium só acorda os relógios uma vez por minuto: o fim do
      // foco (e a fala) chegava atrasado. É um relógio; tem que tocar na hora.
      backgroundThrottling: false,
    },
  });
  janela.setAlwaysOnTop(true, "floating");
  if (process.env.LYLU_TESTE) {
    // O teste roda escondido, mas som e notificação sairiam de verdade no PC dela.
    janela.webContents.setAudioMuted(true);
    const ses = janela.webContents.session;
    ses.setPermissionCheckHandler((_wc, permissao) => permissao !== "notifications");
    ses.setPermissionRequestHandler((_wc, permissao, responde) => responde(permissao !== "notifications"));
  }
  janela.loadFile(path.join(__dirname, "index.html"));
  if (!process.env.LYLU_TESTE) janela.once("ready-to-show", () => janela.show());
  janela.on("moved", arrastou);
  for (const ev of ["minimize", "restore", "show", "hide", "ready-to-show"]) {
    janela.on(ev, () => {
      anota("evento", ev, janela.getBounds());
      if (bandeja) atualizaMenu();
    });
  }
  janela.on("restore", () => {
    if (!pendente) return;
    const b = pendente;
    pendente = null;
    aplica(b);
  });
}

const aparecendo = () => janela.isVisible() && !janela.isMinimized();

function mostra() {
  if (!janela) return;
  if (janela.isMinimized()) janela.restore();
  janela.show();
  janela.focus();
}

// Esconder = minimizar: ela continua na barra de tarefas, e um clique traz de volta.
function esconde() {
  janela.minimize();
}

function atualizaMenu() {
  bandeja.setContextMenu(Menu.buildFromTemplate([
    aparecendo()
      ? { label: "Esconder a Lylu", click: esconde }
      : { label: "Mostrar a Lylu", click: mostra },
    { type: "separator" },
    { label: "Fechar o Lylu Foco", click: () => app.quit() },
  ]));
}

ipcMain.on("tamanho", (_e, w, h) => redimensiona(w, h));
ipcMain.on("tela-cheia", (_e, sim) => telaCheia(!!sim));
// O botão na barra de tarefas enche conforme o foco: verde correndo, amarelo pausado.
ipcMain.on("progresso", (_e, fracao, modo) => janela.setProgressBar(fracao, { mode: modo }));
ipcMain.on("esconder", () => {
  anota("ela tocou em minimizar");
  esconde();
});
ipcMain.on("fechar", () => app.quit());
// Há quantos segundos ninguém mexe no mouse/teclado: se ela não voltou da pausa,
// o próximo foco espera por ela (no teste, um valor fixo).
ipcMain.handle("parado-ha", () =>
  process.env.LYLU_TESTE_PARADO ? Number(process.env.LYLU_TESTE_PARADO) : powerMonitor.getSystemIdleTime());
ipcMain.handle("abre-com-windows", () => app.getLoginItemSettings().openAtLogin);
ipcMain.on("abre-com-windows", (_e, sim) => app.setLoginItemSettings({ openAtLogin: !!sim }));

app.whenReady().then(() => {
  anota("abriu", { versao: app.getVersion(), empacotado: app.isPackaged });
  criaJanela();
  bandeja = new Tray(nativeImage.createFromPath(ROSTO));
  bandeja.setToolTip("Lylu Foco");
  bandeja.on("click", () => (aparecendo() ? esconde() : mostra()));
  atualizaMenu();
  // O atalho da Área de Trabalho com o mesmo AppUserModelId: assim, fixado na
  // barra de tarefas, o ícone fixado e a janela aberta são um botão só.
  if (app.isPackaged) {
    try {
      shell.writeShortcutLink(path.join(app.getPath("desktop"), "Lylu Foco.lnk"), "update",
        { target: process.execPath, appUserModelId: AUMID, icon: process.execPath, iconIndex: 0 });
    } catch {
      // sem atalho (ela apagou): tudo bem
    }
  }
  if (process.env.LYLU_TESTE) require("./teste.js")(janela, process.env.LYLU_TESTE);
});
