// A Lylu do app: o rosto (quadros 0–23) para conversar, ouvir e falar, e a
// mão no queixo (24–68) para pensar. "parada" é o rosto quieto, sem gastar quadro. Atlas montado por scripts/build-conversa-v3.py.
const COLUNAS = 9;
const QUEIXO = 24; // primeiro quadro do gesto de pensar
const FUNDE_MS = 90; // entre desenhos do rosto
const FUNDE_GESTO_MS = 220; // rosto ↔ mão no queixo: desenhos diferentes, fusão mais longa

// O gesto já vem com os intermediários (fluxo óptico): anda a 35 ms, sem fusão.
function gesto(de, ate) {
  const passo = de <= ate ? 1 : -1;
  const passos = [];
  for (let j = de; j !== ate + passo; j += passo) passos.push([QUEIXO + j, 35]);
  return passos;
}

// Cada passo é [quadro, ms]. Os quadros do rosto são desenhos soltos: ficam
// parados um tempo e trocam com uma fusão curta. A piscadela (3) é rara de propósito.
const ROTAS = {
  parada: { laco: [[0, 60000]] },
  conversando: {
    laco: [[0, 2200], [1, 160], [2, 1400], [1, 160], [0, 1800], [6, 180], [7, 180], [8, 1500], [7, 180],
      [6, 180], [0, 2000], [3, 300], [0, 1600], [4, 1200], [5, 900], [2, 160]],
  },
  ouvindo: {
    laco: [[0, 1600], [1, 160], [2, 1200], [1, 160], [0, 1400], [23, 1200], [22, 900]],
  },
  falando: {
    laco: [[12, 130], [13, 130], [14, 150], [15, 130], [13, 130], [17, 160], [18, 130], [20, 150],
      [21, 130], [22, 150], [14, 130], [15, 220]],
  },
  // Sobe a mão, fica um pouco no queixo e desce: o gesto sempre termina.
  pensando: {
    entrada: gesto(0, 20),
    laco: [[QUEIXO + 20, 900], ...gesto(21, 24), [QUEIXO + 24, 1200], ...gesto(23, 21)],
    saida: gesto(25, 44),
  },
};

const ROTULOS = {
  parada: "Lylu",
  conversando: "Lylu conversando",
  ouvindo: "Lylu prestando atenção",
  falando: "Lylu falando",
  pensando: "Lylu pensando, com a mão no queixo",
};

const ehGesto = (q) => q >= QUEIXO;

// `atlas`: endereço da folha de quadros; o Lylu Foco (lylu_pc) passa o dele.
export function criaAnimacao(canvas, atlas = "/lylu-conversa-v3.webp") {
  const ctx = canvas.getContext("2d");
  const imagem = new Image();
  const movimentoReduzido = matchMedia("(prefers-reduced-motion: reduce)");
  let estado = "conversando";
  let fase = "laco";
  let indice = 0;
  let proximo = estado;
  let inicio = 0; // quando o passo atual começou
  let anterior = 0; // quadro de onde a fusão parte
  let funde = 0;
  let pedido = 0;
  let pronta = false;
  let destruida = false;

  const quadroAtual = () => ROTAS[estado][fase][indice][0];

  function comecaPasso(agora, de) {
    const q = quadroAtual();
    anterior = de;
    funde = de === q ? 0 : ehGesto(de) !== ehGesto(q) ? FUNDE_GESTO_MS : ehGesto(q) ? 0 : FUNDE_MS;
    inicio = agora;
  }

  function entraEm(novo, agora) {
    const de = quadroAtual();
    estado = novo;
    fase = ROTAS[novo].entrada ? "entrada" : "laco";
    indice = 0;
    comecaPasso(agora, de);
  }

  function avanca(agora) {
    const de = quadroAtual();
    const rota = ROTAS[estado];
    const saindo = proximo !== estado;
    // No laço do gesto, sair não espera o laço acabar: desce a mão já.
    if (saindo && fase === "laco" && rota.saida) {
      fase = "saida";
      indice = 0;
    } else if (++indice >= rota[fase].length) {
      indice = 0;
      if (fase === "entrada") {
        fase = saindo && rota.saida ? "saida" : "laco";
      } else if (fase === "saida" || saindo) {
        entraEm(proximo, agora);
        return;
      }
    }
    comecaPasso(agora, de);
  }

  function pinta(quadro, alfa = 1) {
    const lado = imagem.naturalWidth / COLUNAS;
    ctx.globalAlpha = alfa;
    // 1 px para dentro de cada lado: ampliado, o quadro vizinho do atlas vazava
    // como uma linha fina em cima da cabeça (tela cheia do Lylu Foco).
    ctx.drawImage(imagem, (quadro % COLUNAS) * lado + 1, Math.floor(quadro / COLUNAS) * lado + 1, lado - 2, lado - 2,
      0, 0, canvas.width, canvas.height);
    ctx.globalAlpha = 1;
  }

  function desenha(agora) {
    pedido = 0;
    if (!pronta || destruida || document.hidden) return;
    if (movimentoReduzido.matches) {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      pinta(proximo === "pensando" ? QUEIXO + 24 : 0);
      return;
    }
    if (!inicio) inicio = agora;
    // O rosto pode mudar de estado na hora (ela tocou no microfone e a Lylu
    // já presta atenção); o gesto termina antes.
    if (proximo !== estado && !ROTAS[estado].saida) entraEm(proximo, agora);
    else if (agora - inicio >= Math.max(ROTAS[estado][fase][indice][1], funde)) avanca(agora);

    const p = funde ? Math.min(1, (agora - inicio) / funde) : 1;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (p < 1) pinta(anterior);
    pinta(quadroAtual(), p * p * (3 - 2 * p));
    // Parada e sem fusão por terminar: não há o que redesenhar até mudar de estado.
    if (estado === "parada" && proximo === "parada" && p >= 1) return;
    pedido = requestAnimationFrame(desenha);
  }

  function retoma() {
    cancelAnimationFrame(pedido);
    inicio = 0;
    if (!document.hidden) pedido = requestAnimationFrame(desenha);
  }

  imagem.onload = () => {
    if (destruida) return;
    pronta = true;
    canvas.classList.add("pronta");
    retoma();
  };
  // A imagem de reserva continua visível se o atlas não carregar.
  imagem.src = atlas;
  document.addEventListener("visibilitychange", retoma);
  movimentoReduzido.addEventListener("change", retoma);

  return {
    setEstado(novo) {
      if (!Object.hasOwn(ROTAS, novo) || novo === proximo) return;
      proximo = novo;
      canvas.setAttribute("aria-label", ROTULOS[novo]);
      if (movimentoReduzido.matches || !pedido) retoma();
    },
    destruir() {
      destruida = true;
      cancelAnimationFrame(pedido);
      document.removeEventListener("visibilitychange", retoma);
      movimentoReduzido.removeEventListener("change", retoma);
    },
  };
}
