// A voz da resposta do Hermes, a mesma que a placa toca. Vem em PCM 16 kHz ou em
// IMA ADPCM, no formato de lylu_p4/main/adpcm.c e servidor/relay/lylu_voz.py:
// bytes 0-1 previsão inicial (int16 LE), byte 2 índice do passo, byte 3 zero,
// depois 4 bits por amostra, o nibble de baixo é a amostra par.
const ADPCM = "ima_adpcm_16000";
const TAXA = 16_000;

const PASSO = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
  97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
  724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
  4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
  18500, 20350, 22385, 24623, 27086, 29794, 32767,
];
const AJUSTE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

export function adpcmParaPcm(dados) {
  if (dados.length <= 4) return new Int16Array(0);
  let previsao = (dados[0] | (dados[1] << 8)) << 16 >> 16;
  let indice = Math.min(88, dados[2]);
  const saida = new Int16Array((dados.length - 4) * 2);
  let n = 0;
  const aplica = (codigo) => {
    const passo = PASSO[indice];
    let delta = passo >> 3;
    if (codigo & 4) delta += passo;
    if (codigo & 2) delta += passo >> 1;
    if (codigo & 1) delta += passo >> 2;
    previsao = Math.max(-32768, Math.min(32767, codigo & 8 ? previsao - delta : previsao + delta));
    indice = Math.max(0, Math.min(88, indice + AJUSTE[codigo]));
    saida[n++] = previsao;
  };
  for (let i = 4; i < dados.length; i++) {
    aplica(dados[i] & 0x0f);
    aplica(dados[i] >> 4);
  }
  return saida;
}

let ctx = null;
let tocando = null;

// Os navegadores só deixam tocar som depois de um toque dela: chamar no clique.
export function liberaSom() {
  ctx ??= new AudioContext();
  if (ctx.state === "suspended") ctx.resume();
}

export function paraSom() {
  const f = tocando;
  tocando = null;
  if (!f) return;
  f.onended = null;
  try {
    f.stop();
  } catch {
    // já tinha acabado
  }
}

export function toca(bytes, formato, aoAcabar) {
  liberaSom();
  const amostras = formato === ADPCM ? adpcmParaPcm(bytes) : new Int16Array(bytes.buffer, bytes.byteOffset, bytes.byteLength >> 1);
  const som = ctx.createBuffer(1, Math.max(1, amostras.length), TAXA);
  const canal = som.getChannelData(0);
  for (let i = 0; i < amostras.length; i++) canal[i] = amostras[i] / 32768;
  paraSom();
  const fonte = ctx.createBufferSource();
  fonte.buffer = som;
  fonte.connect(ctx.destination);
  fonte.onended = () => {
    if (tocando !== fonte) return;
    tocando = null;
    aoAcabar?.();
  };
  tocando = fonte;
  fonte.start();
}
