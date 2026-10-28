#!/usr/bin/env python3
"""Ponte de saída entre a fila privada do Supabase e o perfil Hermes/Lylu.

v2: além do texto, a placa manda a voz (o relay transcreve) e recebe a resposta
falada (o relay sintetiza); e os eventos da placa — tarefa feita, foco
concluído, recado lido — chegam ao Hermes, que comenta só quando vale a pena.
Para mexer na placa (tarefas, recados, humor, pomodoro) o Hermes usa o
lylu_placa.py, descrito na skill lylu-placa.
"""

from __future__ import annotations

import json
import logging
import os
import re
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any

import lylu_voz

LOG = logging.getLogger("lylu_bridge")

HUMORES = ("comemorando", "animada", "cuidadora", "brincalhona", "alertando", "julgadora", "entediada", "descansando")
SILENCIO = {"SILENCIO", "SILÊNCIO", "(SILENCIO)", "(SILÊNCIO)", "[SILENCIO]", "[SILÊNCIO]"}

NAO_ENTENDI = "Não consegui te ouvir direito. Fala de novo, pertinho de mim?"
NAO_RESPONDI = "Não consegui responder agora. A mensagem não virou dívida; tenta de novo quando quiser."


class BridgeError(RuntimeError):
    pass


@dataclass(frozen=True)
class Settings:
    bridge_url: str
    relay_token: str
    hermes_url: str
    hermes_key: str
    conversation: str = "lylu-fisica-01"
    poll_seconds: float = 2.0
    claim_wait: int = 20

    @classmethod
    def from_env(cls, precisa_hermes: bool = True) -> "Settings":
        required = {
            "bridge_url": os.getenv("LYLU_BRIDGE_URL", ""),
            "relay_token": os.getenv("LYLU_RELAY_TOKEN", ""),
            "hermes_key": os.getenv("API_SERVER_KEY", ""),
        }
        missing = [name for name, value in required.items() if not value and (precisa_hermes or name != "hermes_key")]
        if missing:
            raise BridgeError("configuração ausente: " + ", ".join(missing))
        return cls(
            **required,
            hermes_url=os.getenv("HERMES_API_URL", "http://127.0.0.1:8642").rstrip("/"),
            conversation=os.getenv("LYLU_CONVERSATION", "lylu-fisica-01"),
            poll_seconds=float(os.getenv("LYLU_POLL_SECONDS", "2")),
            claim_wait=int(os.getenv("LYLU_CLAIM_WAIT", "20")),
        )


# ---------------------------------------------------------------- HTTP
def _open(request: urllib.request.Request, url: str, timeout: float) -> tuple[bytes, str]:
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.read(), response.headers.get("content-type", "")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")[:500]
        raise BridgeError(f"HTTP {exc.code} em {url}: {detail}") from exc
    except (urllib.error.URLError, TimeoutError) as exc:
        raise BridgeError(f"falha em {url}: {exc}") from exc


def post_json(url: str, body: dict[str, Any], headers: dict[str, str], timeout: float = 90) -> dict[str, Any]:
    request = urllib.request.Request(
        url,
        data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
        headers={"content-type": "application/json", **headers},
        method="POST",
    )
    raw, _ = _open(request, url, timeout)
    try:
        return json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise BridgeError(f"resposta inválida de {url}") from exc


def bridge_call(settings: Settings, action: str, timeout: float = 30, **payload: Any) -> dict[str, Any]:
    return post_json(
        settings.bridge_url,
        {"action": action, **payload},
        {"x-lylu-token": settings.relay_token},
        timeout=timeout,
    )


def bridge_audio_get(settings: Settings, request_id: str) -> bytes:
    request = urllib.request.Request(
        settings.bridge_url,
        data=json.dumps({"action": "audio_get", "request_id": request_id}).encode(),
        headers={"content-type": "application/json", "x-lylu-token": settings.relay_token},
        method="POST",
    )
    raw, tipo = _open(request, settings.bridge_url, 60)
    if "octet-stream" not in tipo:
        raise BridgeError("a ponte não devolveu áudio")
    return raw


def device_takes_adpcm(device: dict[str, Any] | None) -> bool:
    """A placa nova conta no relatório que sabe tocar ADPCM; a antiga, não."""
    formatos = ((device or {}).get("estado") or {}).get("audio_formatos") or []
    return lylu_voz.FORMATO_ADPCM in formatos


def bridge_audio_up(settings: Settings, pcm: bytes, request_id: str | None = None, device_id: str | None = None,
                    adpcm: bool = False) -> dict[str, Any]:
    """Sobe a voz para a placa; com adpcm=True ela vai 4x menor."""
    headers = {"content-type": "application/octet-stream", "x-lylu-token": settings.relay_token, "x-lylu-action": "audio_up"}
    if adpcm:
        pcm = lylu_voz.adpcm_codifica(pcm)
        headers["x-lylu-formato"] = lylu_voz.FORMATO_ADPCM
    if request_id:
        headers["x-lylu-request"] = request_id
    elif device_id:
        headers["x-lylu-device"] = device_id
    request = urllib.request.Request(settings.bridge_url, data=pcm, headers=headers, method="POST")
    raw, _ = _open(request, settings.bridge_url, 60)
    return json.loads(raw.decode("utf-8"))


# ---------------------------------------------------------------- Hermes
def extract_output_text(response: dict[str, Any]) -> str:
    chunks: list[str] = []
    for item in response.get("output", []):
        if item.get("type") != "message" or item.get("role") != "assistant":
            continue
        for part in item.get("content", []):
            if part.get("type") in {"output_text", "text"} and isinstance(part.get("text"), str):
                chunks.append(part["text"])
    text = "\n".join(chunks).strip()
    if not text:
        raise BridgeError("Hermes respondeu sem texto final")
    return text[:4000]


COMO_FALAR = {
    "chat": "Ela escreveu. A resposta aparece no balão da tela e também é lida em voz alta: "
            "uma ou duas frases curtas, sem markdown, sem listas, no máximo 1 emoji.",
    "voz": "Ela FALOU com você (a fala virou texto, pode ter erro de transcrição). A resposta vai ser "
           "lida em voz alta e aparece no balão: no máximo 2 ou 3 frases curtas, jeito de conversa, "
           "sem emoji, sem markdown, sem listas.",
    "event": "Isto NÃO é uma mensagem dela: é um aviso automático da placa sobre o que ela acabou de fazer. "
             "Se valer um comentário curto (comemorar a missão, lembrar de uma pausa, cuidar), responda com "
             "uma frase curta, sem emoji. Se não houver nada que valha a pena dizer, responda exatamente: SILENCIO. "
             "Na dúvida, SILENCIO — falar toda hora vira ruído.",
}


def instructions_for(via: str, device: dict[str, Any] | None, origem: str | None = None) -> str:
    partes = [
        "Você é a Lylu falando com a Mica pelo seu corpo físico: a placa na mesa de trabalho dela. "
        "Responda em português do Brasil, com a personalidade da Lylu (acolhe, nunca cobra).",
        COMO_FALAR.get(via, COMO_FALAR["chat"]),
    ]
    if origem == "app":
        partes.append("Desta vez ela está no app do celular e pode estar longe da placa: a resposta aparece "
                      "no celular e também na placa. Não peça para ela olhar a tela da placa.")
    partes += [
        "Se quiser mudar o humor que aparece na tela, comece a resposta com [humor: X], X sendo um de: "
        + ", ".join(HUMORES) + ". No modo dia difícil, nunca julgadora nem alertando.",
        "Para mudar tarefas, recados, humor ou o pomodoro na placa, use a skill lylu-placa (lylu_placa.py). "
        "Faça a mudança com a ferramenta e diga em uma frase o que fez.",
    ]
    estado = (device or {}).get("estado")
    if estado:
        texto = json.dumps(estado, ensure_ascii=False)
        if len(texto) > 3500:
            texto = texto[:3500] + "…"
        partes.append(f"O que a placa está mostrando agora (JSON, atualizado em {device.get('estado_em')}): {texto}")
    return "\n\n".join(partes)


def ask_hermes(settings: Settings, text: str, via: str = "chat", device: dict[str, Any] | None = None,
               origem: str | None = None) -> str:
    response = post_json(
        f"{settings.hermes_url}/v1/responses",
        {
            "model": "lylu",
            "conversation": settings.conversation,
            "input": text,
            "instructions": instructions_for(via, device, origem),
            "store": True,
        },
        {"authorization": f"Bearer {settings.hermes_key}"},
        timeout=180,
    )
    return extract_output_text(response)


def split_humor(text: str) -> tuple[str | None, str]:
    m = re.match(r"^\s*\[humor:\s*([a-zà-ú]+)\s*\]\s*", text, re.IGNORECASE)
    if not m:
        return None, text.strip()
    humor = m.group(1).lower()
    return (humor if humor in HUMORES else None), text[m.end():].strip()


def is_silence(text: str) -> bool:
    return text.strip().strip(".!").upper() in SILENCIO or not text.strip()


# ---------------------------------------------------------------- eventos da placa
def describe_event(payload: dict[str, Any]) -> str:
    tipo = payload.get("tipo", "")
    t = payload.get("t") or payload.get("tarefa") or ""
    ident = f" (id {payload['id']})" if payload.get("id") else ""
    if tipo == "tarefa_feita":
        texto = f"Mica marcou como feita, na placa: “{t}”{ident}."
        if payload.get("missao"):
            texto += " Era da missão do dia."
        if payload.get("missao_completa"):
            texto += " Com isso a missão do dia ficou COMPLETA."
        return texto + " Se essa tarefa vier do Todoist, conclua lá também."
    if tipo == "tarefa_desfeita":
        return f"Mica desmarcou, na placa: “{t}”{ident}. Se você tinha concluído no Todoist, reabra."
    # O pomodoro: ela pediu para você saber quando está em foco e avisar a pausa
    # (e de quantos minutos ela é). Nesses dois avisos falar não é palpite, é função.
    blocos = f" de {payload['blocos']}" if payload.get("blocos") else ""
    if tipo == "foco_inicio":
        return (f"Mica começou um bloco de foco de {payload.get('minutos', '?')} min em “{t}” "
                f"(bloco {payload.get('bloco', '?')}{blocos}). Ela está em pomodoro agora: não puxe "
                "conversa nem mande falas até a pausa. Responda exatamente SILENCIO.")
    if tipo == "foco_fim":
        texto = (f"Mica terminou um bloco de foco de {payload.get('minutos', '?')} min em “{t}” "
                 f"(bloco {payload.get('bloco', '?')}{blocos}; {payload.get('feitos_hoje', '?')} concluídos hoje).")
        if not payload.get("pausa_min"):
            return texto + " Agora começa a pausa."
        longa = "a pausa longa, o ciclo inteiro acabou" if payload.get("pausa_longa") else "uma pausa curta"
        comeca = ("o relógio da pausa já está correndo" if payload.get("pausa_sozinha")
                  else "ela toca em “Começar pausa” quando quiser")
        return (texto + f" Agora é {longa}: {payload['pausa_min']} minutos ({comeca}). AVISE a Mica, em uma "
                "frase curta e acolhedora, que é hora da pausa e de quantos minutos ela é — por exemplo "
                f"“Hora da pausa: {payload['pausa_min']} minutinhos. Levanta e bebe uma água.”. "
                "Aqui não responda SILENCIO.")
    if tipo == "pausa_fim":
        proximo = f"bloco {payload.get('proximo_bloco', '?')}{blocos}, de {payload.get('foco_min', '?')} min"
        if payload.get("pulou"):
            return f"Mica pulou a pausa e voltou para o foco ({proximo}). Responda exatamente SILENCIO."
        volta = ("o foco já recomeçou sozinho" if payload.get("foco_sozinho")
                 else "ela toca em “Começar” quando estiver pronta")
        return (f"A pausa de {payload.get('minutos', '?')} minutos da Mica acabou; agora vem o {proximo} "
                f"em “{t}” ({volta}). AVISE em uma frase curta e gentil que a pausa acabou, sem cobrar. "
                "Aqui não responda SILENCIO.")
    # A virada do dia: às 5h a placa tira as feitas e marca o resto "de ontem"; às 9h
    # manda isto. As tarefas são do dia (pedido da Mica, 28/10/2026).
    if tipo == "dia_novo":
        pend = payload.get("pendentes") or []
        if not pend:
            return "Virou o dia na placa e não ficou nada de ontem. Responda exatamente SILENCIO."
        lista = "; ".join(f"“{p.get('t', '')}”" + (f" (id {p['id']})" if p.get("id") else "")
                          + (" [era da missão]" if p.get("missao") else "") for p in pend)
        return (f"Aviso do começo do dia (a placa manda das 9h em diante): ficaram {len(pend)} de ontem: {lista}. "
                "AVISE a Mica, em uma ou duas "
                "frases curtas e acolhedoras, quais ficaram (sem cobrar) e pergunte se ela quer manter hoje. No "
                "Todoist, passe essas para hoje. Depois mande a lista do dia com lylu_placa.py tarefas: as de ontem "
                "continuam com a etiqueta “de ontem” sozinhas. Aqui não responda SILENCIO.")
    if tipo == "recado_entendi":
        return f"Mica tocou em “Entendi” no recado “{payload.get('titulo', '')}”{ident}."
    if tipo == "recado_adiado":
        return f"Mica pediu para ser lembrada depois do recado “{payload.get('titulo', '')}”{ident}."
    return "Aviso da placa: " + json.dumps(payload, ensure_ascii=False)


# ---------------------------------------------------------------- uma mensagem
def answer(settings: Settings, request_id: str, text: str, transcricao: str | None = None, adpcm: bool = False) -> None:
    humor, text = split_humor(text)
    audio_id = None
    try:
        t0 = time.monotonic()
        pcm = lylu_voz.sintetizar(text)
        if pcm:
            audio_id = bridge_audio_up(settings, pcm, request_id=request_id, adpcm=adpcm).get("audio_id")
        LOG.info("tempo %s: voz da resposta %.1f s (%d ms de fala, %s)", request_id, time.monotonic() - t0,
                 len(pcm or b"") // 32, "adpcm" if adpcm else "pcm")
    except Exception:
        LOG.exception("sem voz para %s; vai só o texto", request_id)
    extra: dict[str, Any] = {}
    if audio_id:
        extra["audio_id"] = audio_id
        extra["formato"] = lylu_voz.FORMATO_ADPCM if adpcm else lylu_voz.FORMATO_PCM
    if humor:
        extra["humor"] = humor
    if transcricao:
        extra["transcricao"] = transcricao
    bridge_call(settings, "complete", request_id=request_id, text=text[:4000], **extra)


def process_once(settings: Settings) -> bool:
    claimed = bridge_call(settings, "claim", timeout=settings.claim_wait + 20, wait_s=settings.claim_wait)
    message = claimed.get("message")
    if not message:
        return False
    device = claimed.get("device") or {}
    request_id = message["message_id"]
    kind = message.get("kind")
    payload = message.get("payload") or {}
    # Quem fala em ADPCM ouve em ADPCM; e a placa nova avisa no relatório que entende.
    adpcm = payload.get("formato") == lylu_voz.FORMATO_ADPCM or device_takes_adpcm(device)

    def hermes(text: str, via: str) -> str:
        t0 = time.monotonic()
        reply = ask_hermes(settings, text, via, device, payload.get("origem"))
        LOG.info("tempo %s: Hermes %.1f s", request_id, time.monotonic() - t0)
        return reply

    try:
        if kind == "chat":
            text = payload.get("text")
            if not isinstance(text, str) or not text.strip():
                raise BridgeError("mensagem sem texto conversável")
            answer(settings, request_id, hermes(text.strip(), "chat"), adpcm=adpcm)
        elif kind == "voz":
            t0 = time.monotonic()
            pcm = lylu_voz.para_pcm(bridge_audio_get(settings, request_id), payload.get("formato"))
            t1 = time.monotonic()
            heard = lylu_voz.transcrever(pcm)
            LOG.info("voz %s: %d ms (%s), baixar %.1f s, transcrever %.1f s, ouvi %r", request_id, len(pcm) // 32,
                     payload.get("formato") or lylu_voz.FORMATO_PCM, t1 - t0, time.monotonic() - t1, heard[:80])
            if len(heard) < 2:
                answer(settings, request_id, NAO_ENTENDI, adpcm=adpcm)
            else:
                answer(settings, request_id, hermes(heard, "voz"), transcricao=heard, adpcm=adpcm)
        elif kind == "event":
            reply = hermes(describe_event(payload), "event")
            if is_silence(split_humor(reply)[1]):
                bridge_call(settings, "complete", request_id=request_id, silent=True)
            else:
                answer(settings, request_id, reply, adpcm=adpcm)
        else:
            raise BridgeError(f"tipo de mensagem desconhecido: {kind}")
        LOG.info("mensagem %s (%s) concluída", request_id, kind)
    except Exception:
        LOG.exception("falha ao processar %s", request_id)
        try:
            if kind == "event":
                # Aviso automático que falhou não vira recado de erro na tela dela.
                bridge_call(settings, "fail", request_id=request_id)
            else:
                answer(settings, request_id, NAO_RESPONDI, adpcm=adpcm)
        except Exception:
            LOG.exception("falha ao marcar %s como erro", request_id)
    return True


def main() -> None:
    logging.basicConfig(level=os.getenv("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
    settings = Settings.from_env()
    LOG.info("ponte Lylu v3 iniciada (voz em PCM ou ADPCM); o áudio só passa por aqui e é apagado em seguida")
    while True:
        try:
            worked = process_once(settings)
            if not worked and settings.claim_wait <= 0:
                time.sleep(settings.poll_seconds)
        except KeyboardInterrupt:
            return
        except Exception:
            LOG.exception("ciclo da ponte falhou")
            time.sleep(max(settings.poll_seconds, 2.0))


if __name__ == "__main__":
    main()
