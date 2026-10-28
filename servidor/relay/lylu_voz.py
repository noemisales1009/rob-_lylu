"""Ouvido e voz da Lylu no servidor do Hermes: fala → texto e texto → fala.

A placa manda e recebe PCM cru: 16 kHz, mono, 16 bits little-endian. É o
formato do codec dela, então ela não precisa decodificar nada.

Provedores (variáveis de ambiente):
  LYLU_STT = auto | local | groq | openai      (padrão: auto)
  LYLU_TTS = auto | edge | openai | nenhum     (padrão: auto)
  LYLU_STT_MODELO   modelo do faster-whisper local (padrão: small)
  LYLU_VOZ          voz do edge-tts (padrão: pt-BR-FranciscaNeural)
  LYLU_VOZ_RITMO    ritmo do edge-tts, ex. "+5%" (padrão: +0%)
  GROQ_API_KEY / OPENAI_API_KEY quando o provedor for groq/openai

"auto" escolhe o primeiro que estiver disponível nesta máquina, nesta ordem:
STT local (faster-whisper) → groq → openai; TTS edge-tts → openai.
"""

from __future__ import annotations

import array
import asyncio
import io
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import unicodedata
import urllib.error
import urllib.request
import uuid
import wave

LOG = logging.getLogger("lylu_voz")

TAXA = 16000


class VozError(RuntimeError):
    pass


# ---------------------------------------------------------------- IMA ADPCM
# A voz 4 vezes menor. A placa transfere poucos KB por segundo, e a voz crua
# (32 KB por segundo de fala) era a maior parte da espera numa conversa.
# O formato é o mesmo de lylu_p4/main/adpcm.c — os dois precisam bater bit a bit:
#   bytes 0-1 previsão inicial (int16 LE), byte 2 índice do passo, byte 3 zero,
#   depois 4 bits por amostra, o nibble de baixo é a amostra par.
FORMATO_PCM = "pcm_s16le_16000"
FORMATO_ADPCM = "ima_adpcm_16000"

_PASSO = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
    97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767,
)
_AJUSTE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def _aplica(previsao: int, indice: int, codigo: int) -> tuple[int, int]:
    passo = _PASSO[indice]
    delta = passo >> 3
    if codigo & 4:
        delta += passo
    if codigo & 2:
        delta += passo >> 1
    if codigo & 1:
        delta += passo >> 2
    previsao = previsao - delta if codigo & 8 else previsao + delta
    previsao = max(-32768, min(32767, previsao))
    indice = max(0, min(88, indice + _AJUSTE[codigo]))
    return previsao, indice


def adpcm_codifica(pcm: bytes) -> bytes:
    amostras = array.array("h")
    amostras.frombytes(pcm[: len(pcm) // 2 * 2])
    if sys.byteorder == "big":
        amostras.byteswap()
    previsao = amostras[0] if amostras else 0
    indice = 0
    saida = bytearray(previsao.to_bytes(2, "little", signed=True) + bytes([indice, 0]))
    codigos = []
    for s in amostras:
        passo = _PASSO[indice]
        dif = s - previsao
        codigo = 0
        if dif < 0:
            codigo = 8
            dif = -dif
        if dif >= passo:
            codigo |= 4
            dif -= passo
        if dif >= passo >> 1:
            codigo |= 2
            dif -= passo >> 1
        if dif >= passo >> 2:
            codigo |= 1
        previsao, indice = _aplica(previsao, indice, codigo)
        codigos.append(codigo)
    if len(codigos) % 2:
        codigos.append(0)
    saida.extend(codigos[i] | codigos[i + 1] << 4 for i in range(0, len(codigos), 2))
    return bytes(saida)


def adpcm_decodifica(dados: bytes) -> bytes:
    if len(dados) <= 4:
        return b""
    previsao = int.from_bytes(dados[0:2], "little", signed=True)
    indice = min(88, dados[2])
    saida = array.array("h")
    for byte in dados[4:]:
        previsao, indice = _aplica(previsao, indice, byte & 0x0F)
        saida.append(previsao)
        previsao, indice = _aplica(previsao, indice, byte >> 4)
        saida.append(previsao)
    if sys.byteorder == "big":
        saida.byteswap()
    return saida.tobytes()


def para_pcm(dados: bytes, formato: str | None) -> bytes:
    return adpcm_decodifica(dados) if formato == FORMATO_ADPCM else dados


# ---------------------------------------------------------------- utilidades
def pcm_para_wav(pcm: bytes, taxa: int = TAXA) -> bytes:
    saida = io.BytesIO()
    with wave.open(saida, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(taxa)
        w.writeframes(pcm)
    return saida.getvalue()


def reamostra(pcm: bytes, de: int, para: int = TAXA) -> bytes:
    """Interpolação linear simples; basta para voz."""
    if de == para:
        return pcm
    if shutil.which("ffmpeg"):
        return _ffmpeg(pcm, ["-f", "s16le", "-ar", str(de), "-ac", "1"])
    entrada = array.array("h")
    entrada.frombytes(pcm[: len(pcm) // 2 * 2])
    if not entrada:
        return b""
    n = int(len(entrada) * para / de)
    saida = array.array("h", bytes(2 * n))
    passo = de / para
    ultimo = len(entrada) - 1
    for i in range(n):
        pos = i * passo
        k = int(pos)
        frac = pos - k
        a = entrada[min(k, ultimo)]
        b = entrada[min(k + 1, ultimo)]
        saida[i] = int(a + (b - a) * frac)
    return saida.tobytes()


def _ffmpeg(dados: bytes, formato_entrada: list[str]) -> bytes:
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", *formato_entrada, "-i", "pipe:0",
           "-f", "s16le", "-acodec", "pcm_s16le", "-ac", "1", "-ar", str(TAXA), "pipe:1"]
    proc = subprocess.run(cmd, input=dados, capture_output=True, timeout=60)
    if proc.returncode != 0:
        raise VozError("ffmpeg falhou: " + proc.stderr.decode(errors="replace")[:300])
    return proc.stdout


def mp3_para_pcm(mp3: bytes) -> bytes:
    if shutil.which("ffmpeg"):
        return _ffmpeg(mp3, [])
    try:
        import miniaudio  # type: ignore
    except ImportError as exc:
        raise VozError("para converter a voz instale o ffmpeg (ou: pip install miniaudio)") from exc
    som = miniaudio.decode(mp3, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=TAXA)
    return som.samples.tobytes()


def texto_para_falar(texto: str) -> str:
    """Tira o que não se lê em voz alta: emoji, markdown, links."""
    texto = re.sub(r"https?://\S+", "", texto)
    texto = re.sub(r"[*_`#>|~]+", "", texto)
    texto = "".join(c for c in texto if unicodedata.category(c) not in ("So", "Cs", "Co") and c not in "‍️")
    return re.sub(r"\s+", " ", texto).strip()


def _multipart(campos: dict[str, str], arquivo: tuple[str, str, bytes]) -> tuple[bytes, str]:
    fronteira = "lylu" + uuid.uuid4().hex
    partes: list[bytes] = []
    for nome, valor in campos.items():
        partes.append(f'--{fronteira}\r\nContent-Disposition: form-data; name="{nome}"\r\n\r\n{valor}\r\n'.encode())
    nome, nome_arquivo, dados = arquivo
    partes.append(
        f'--{fronteira}\r\nContent-Disposition: form-data; name="{nome}"; filename="{nome_arquivo}"\r\n'
        f"Content-Type: audio/wav\r\n\r\n".encode() + dados + b"\r\n"
    )
    partes.append(f"--{fronteira}--\r\n".encode())
    return b"".join(partes), f"multipart/form-data; boundary={fronteira}"


def _post(url: str, corpo: bytes, cabecalhos: dict[str, str], timeout: float = 120) -> bytes:
    req = urllib.request.Request(url, data=corpo, headers=cabecalhos, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read()
    except urllib.error.HTTPError as exc:
        raise VozError(f"HTTP {exc.code} em {url}: {exc.read().decode(errors='replace')[:300]}") from exc
    except (urllib.error.URLError, TimeoutError) as exc:
        raise VozError(f"falha em {url}: {exc}") from exc


# ---------------------------------------------------------------- fala → texto
_modelo_local = None


def _tem_faster_whisper() -> bool:
    try:
        import faster_whisper  # type: ignore  # noqa: F401
        return True
    except ImportError:
        return False


def _stt_local(pcm: bytes) -> str:
    global _modelo_local
    import numpy as np  # type: ignore
    from faster_whisper import WhisperModel  # type: ignore

    if _modelo_local is None:
        nome = os.getenv("LYLU_STT_MODELO", "small")
        LOG.info("carregando o faster-whisper %s (só na primeira vez)", nome)
        _modelo_local = WhisperModel(nome, device="cpu", compute_type="int8")
    som = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
    # Uma passada só. Por padrão, quando fica inseguro, o whisper refaz o trecho
    # com "temperatura" cada vez maior (até 5 vezes): a transcrição oscilava de
    # 1,2 s para 6,1 s e o final saía inventado ("virou futebol", 28/10/2026).
    trechos, _ = _modelo_local.transcribe(som, language="pt", beam_size=1, vad_filter=True,
                                          temperature=0.0, condition_on_previous_text=False)
    return " ".join(t.text.strip() for t in trechos).strip()


def _stt_api(pcm: bytes, url: str, chave: str, modelo: str) -> str:
    corpo, tipo = _multipart(
        {"model": modelo, "language": "pt", "response_format": "json"},
        ("file", "fala.wav", pcm_para_wav(pcm)),
    )
    resp = _post(url, corpo, {"authorization": f"Bearer {chave}", "content-type": tipo})
    return str(json.loads(resp.decode()).get("text", "")).strip()


def transcrever(pcm: bytes) -> str:
    provedor = os.getenv("LYLU_STT", "auto").lower()
    if provedor == "auto":
        if _tem_faster_whisper():
            provedor = "local"
        elif os.getenv("GROQ_API_KEY"):
            provedor = "groq"
        elif os.getenv("OPENAI_API_KEY"):
            provedor = "openai"
        else:
            raise VozError("nenhum STT disponível: pip install faster-whisper, ou defina GROQ_API_KEY/OPENAI_API_KEY")
    if provedor == "local":
        return _stt_local(pcm)
    if provedor == "groq":
        return _stt_api(pcm, "https://api.groq.com/openai/v1/audio/transcriptions",
                        os.environ["GROQ_API_KEY"], os.getenv("LYLU_STT_MODELO", "whisper-large-v3-turbo"))
    if provedor == "openai":
        return _stt_api(pcm, "https://api.openai.com/v1/audio/transcriptions",
                        os.environ["OPENAI_API_KEY"], os.getenv("LYLU_STT_MODELO", "gpt-4o-mini-transcribe"))
    raise VozError(f"LYLU_STT desconhecido: {provedor}")


# ---------------------------------------------------------------- texto → fala
def _tem_edge_tts() -> bool:
    try:
        import edge_tts  # type: ignore  # noqa: F401
        return True
    except ImportError:
        return False


def _tts_edge(texto: str) -> bytes:
    import edge_tts  # type: ignore

    async def junta() -> bytes:
        com = edge_tts.Communicate(texto, os.getenv("LYLU_VOZ", "pt-BR-FranciscaNeural"),
                                   rate=os.getenv("LYLU_VOZ_RITMO", "+0%"))
        partes = []
        async for pedaco in com.stream():
            if pedaco.get("type") == "audio":
                partes.append(pedaco["data"])
        return b"".join(partes)

    mp3 = asyncio.run(junta())
    if not mp3:
        raise VozError("edge-tts não devolveu áudio")
    return mp3_para_pcm(mp3)


def _tts_openai(texto: str) -> bytes:
    corpo = json.dumps({
        "model": os.getenv("LYLU_TTS_MODELO", "gpt-4o-mini-tts"),
        "voice": os.getenv("LYLU_VOZ", "coral"),
        "input": texto,
        "response_format": "pcm",          # 24 kHz, 16 bits, mono
    }).encode()
    pcm = _post("https://api.openai.com/v1/audio/speech", corpo, {
        "authorization": f"Bearer {os.environ['OPENAI_API_KEY']}",
        "content-type": "application/json",
    })
    return reamostra(pcm, 24000)


def sintetizar(texto: str) -> bytes | None:
    """PCM 16 kHz da fala, ou None se a voz estiver desligada."""
    texto = texto_para_falar(texto)
    if not texto:
        return None
    provedor = os.getenv("LYLU_TTS", "auto").lower()
    if provedor in ("nenhum", "none", "off"):
        return None
    if provedor == "auto":
        if _tem_edge_tts():
            provedor = "edge"
        elif os.getenv("OPENAI_API_KEY"):
            provedor = "openai"
        else:
            LOG.warning("sem TTS: pip install edge-tts (e ffmpeg), ou defina OPENAI_API_KEY")
            return None
    if provedor == "edge":
        pcm = _tts_edge(texto)
    elif provedor == "openai":
        pcm = _tts_openai(texto)
    else:
        raise VozError(f"LYLU_TTS desconhecido: {provedor}")
    # 40 s é o teto da ponte; resposta maior que isso nem cabe no balão.
    return pcm[: 40 * TAXA * 2]
