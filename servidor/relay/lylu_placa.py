#!/usr/bin/env python3
"""O Hermes mexendo na placa da Lylu: tarefas, recados, fala, humor e pomodoro.

Usa o mesmo token do relay (LYLU_BRIDGE_URL e LYLU_RELAY_TOKEN no ambiente, ou
num arquivo .env apontado por --env / LYLU_ENV_FILE). Tudo o que muda na placa
chega em até ~10 s, mesmo que ela não tenha perguntado nada.

Exemplos:
  lylu_placa.py estado
  lylu_placa.py listas
  lylu_placa.py historico --hoje
  lylu_placa.py historico --dias 7 --tipo foco_fim
  lylu_placa.py falar "Bora beber água?" --humor cuidadora
  lylu_placa.py tarefas --missao "Enviar orçamento" --missao "Tomar o remédio" --bonus "Lavar a louça"
  lylu_placa.py marcar "Tomar o remédio"
  lylu_placa.py recado "Dentista" --quando 2026-10-29T14:00 --detalhe "Levar o raio-x"
  lylu_placa.py pomodoro iniciar --tarefa "Escrever o relatório" --minutos 25
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lylu_bridge  # noqa: E402
import lylu_voz  # noqa: E402

TELAS = ("Foco", "Tarefas", "Recados", "Semana", "Relógio", "Cantinho", "Ajustes")
BRASILIA = timezone(timedelta(hours=-3))      # sem horário de verão desde 2019
LEITURAS = ("estado", "listas", "historico")  # só leem; não mandam nada para a placa


def carrega_env(caminho: str | None) -> None:
    caminho = caminho or os.getenv("LYLU_ENV_FILE")
    if not caminho or not Path(caminho).expanduser().exists():
        return
    for linha in Path(caminho).expanduser().read_text(encoding="utf-8").splitlines():
        linha = linha.strip()
        if not linha or linha.startswith("#") or "=" not in linha:
            continue
        nome, valor = linha.split("=", 1)
        os.environ.setdefault(nome.strip().removeprefix("export ").strip(), valor.strip().strip('"').strip("'"))


def tarefa(texto: str) -> dict[str, Any]:
    """'texto' ou 'id::texto' (para guardar o id do Todoist junto)."""
    if "::" in texto:
        ident, t = texto.split("::", 1)
        return {"id": ident.strip(), "t": t.strip()}
    return {"t": texto.strip()}


def monta(args: argparse.Namespace) -> tuple[str, dict[str, Any]]:
    c = args.comando
    if c == "falar":
        p: dict[str, Any] = {"text": args.texto}
        if args.forte:
            p["forte"] = args.forte
        if args.humor:
            p["humor"] = args.humor
        return "chat", p
    if c == "humor":
        return "estado", {"humor": {"nome": args.nome, "minutos": args.minutos}}
    if c == "tarefas":
        if args.json:
            fonte = Path(args.json)
            dados = json.loads(fonte.read_text(encoding="utf-8") if fonte.exists() else args.json)
            return "estado", {"tarefas": dados}
        return "estado", {"tarefas": {"missao": [tarefa(t) for t in args.missao or []],
                                      "bonus": [tarefa(t) for t in args.bonus or []]}}
    if c == "marcar":
        return "estado", {"marcar": {"alvo": args.alvo, "f": not args.desfazer}}
    if c == "recado":
        r: dict[str, Any] = {"titulo": args.titulo}
        for campo in ("id", "quando", "dia", "hora", "turno", "detalhe", "tipo", "fala", "forte"):
            valor = getattr(args, campo)
            if valor:
                r[campo] = valor
        return "estado", {"recado": r}
    if c == "recado-apagar":
        return "estado", {"recado_apagar": args.alvo}
    if c == "recados-limpar":
        return "estado", {"recados": []}
    if c == "pomodoro":
        p = {"acao": args.acao}
        if args.tarefa:
            p["tarefa"] = args.tarefa
        if args.minutos:
            p["minutos"] = args.minutos
        return "comando", {"pomodoro": p}
    if c == "pomodoro-config":
        p = {"acao": "config"}
        for campo in ("foco", "curta", "blocos", "longa"):
            if getattr(args, campo) is not None:
                p[campo] = getattr(args, campo)
        return "comando", {"pomodoro": p}
    if c == "tela":
        return "comando", {"tela": args.nome}
    if c == "dia-dificil":
        return "comando", {"dificil": args.ligado == "on"}
    raise SystemExit(f"comando desconhecido: {c}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Mexe na placa física da Lylu.")
    ap.add_argument("--env", help="arquivo .env com LYLU_BRIDGE_URL e LYLU_RELAY_TOKEN")
    ap.add_argument("--placa", help="device_id (padrão: a placa vista por último)")
    sub = ap.add_subparsers(dest="comando", required=True)

    sub.add_parser("estado", help="mostra o que está na placa agora (JSON)")
    sub.add_parser("listas", help="missão, bônus e recados guardados no Supabase (valem com a placa desligada)")

    s = sub.add_parser("historico", help="o que aconteceu: tarefas feitas, focos, recados, conversas (mais novo primeiro)")
    s.add_argument("--hoje", action="store_true", help="desde a meia-noite de hoje (Brasília)")
    s.add_argument("--dias", type=int, default=1, help="quantos dias para trás (padrão 1, até 90)")
    s.add_argument("--tipo", help="só um tipo: tarefa_feita, tarefa_desfeita, foco_fim, recado_entendi, "
                                  "recado_adiado, conversa, comentario, hermes_falou, hermes_estado, hermes_comando")
    s.add_argument("--limite", type=int, default=200, help="no máximo quantos (padrão 200, até 500)")

    s = sub.add_parser("falar", help="a Lylu fala na placa (balão + voz)")
    s.add_argument("texto")
    s.add_argument("--forte", help="segunda linha, em negrito")
    s.add_argument("--humor", choices=lylu_bridge.HUMORES)
    s.add_argument("--sem-voz", action="store_true", help="só o balão, sem áudio")

    s = sub.add_parser("humor", help="muda o humor que aparece na barra de cima")
    s.add_argument("nome", choices=lylu_bridge.HUMORES)
    s.add_argument("--minutos", type=int, default=60, help="por quanto tempo (padrão 60)")

    s = sub.add_parser("tarefas", help="troca a missão do dia e o bônus (substitui as listas)")
    s.add_argument("--missao", action="append", help="até 3; repita a opção. Aceita 'id::texto'")
    s.add_argument("--bonus", action="append", help="até 6; repita a opção. Aceita 'id::texto'")
    s.add_argument("--json", help='JSON ou arquivo: {"missao":[{"id","t","f"}],"bonus":[...]}')

    s = sub.add_parser("marcar", help="marca (ou desmarca) uma tarefa como feita")
    s.add_argument("alvo", help="o id ou o texto da tarefa")
    s.add_argument("--desfazer", action="store_true")

    s = sub.add_parser("recado", help="acrescenta (ou atualiza, pelo id) um recado")
    s.add_argument("titulo")
    s.add_argument("--id")
    s.add_argument("--quando", help="AAAA-MM-DD ou AAAA-MM-DDTHH:MM (a placa calcula hoje/amanhã)")
    s.add_argument("--dia", help="texto livre, se não houver data (ex.: 'semana que vem')")
    s.add_argument("--hora", help="HH:MM, se não vier no --quando")
    s.add_argument("--turno", choices=("manha", "tarde", "noite"),
                   help="para lembrete sem hora certa: a placa mostra 'HOJE · À NOITE'")
    s.add_argument("--detalhe")
    s.add_argument("--tipo", help="etiqueta: lembrete, compromisso, ideia... (padrão lembrete)")
    s.add_argument("--fala", help="o que a Lylu diz no balão sobre ele")
    s.add_argument("--forte", help="a segunda linha da fala, em negrito")

    s = sub.add_parser("recado-apagar", help="tira um recado (pelo id ou pelo título)")
    s.add_argument("alvo")
    sub.add_parser("recados-limpar", help="tira todos os recados")

    s = sub.add_parser("pomodoro", help="iniciar, pausar, continuar ou parar o foco")
    s.add_argument("acao", choices=("iniciar", "pausar", "continuar", "parar"))
    s.add_argument("--tarefa")
    s.add_argument("--minutos", type=int)

    s = sub.add_parser("pomodoro-config", help="muda os tempos guardados do pomodoro")
    for campo in ("foco", "curta", "blocos", "longa"):
        s.add_argument(f"--{campo}", type=int)

    s = sub.add_parser("tela", help="mostra uma das telas")
    s.add_argument("nome", choices=TELAS)

    s = sub.add_parser("dia-dificil", help="liga/desliga o modo dia difícil")
    s.add_argument("ligado", choices=("on", "off"))

    args = ap.parse_args()
    carrega_env(args.env)
    settings = lylu_bridge.Settings.from_env(precisa_hermes=False)

    if args.comando in LEITURAS:
        if args.comando == "estado":
            resp = lylu_bridge.bridge_call(settings, "state", device_id=args.placa).get("device")
        elif args.comando == "listas":
            resp = lylu_bridge.bridge_call(settings, "listas", device_id=args.placa)
        else:
            extra: dict[str, Any] = {"dias": args.dias, "limite": args.limite}
            if args.hoje:
                extra["desde"] = datetime.now(BRASILIA).replace(hour=0, minute=0, second=0, microsecond=0).isoformat()
            if args.tipo:
                extra["tipo"] = args.tipo
            resp = lylu_bridge.bridge_call(settings, "historico", device_id=args.placa, **extra)
        print(json.dumps(resp, ensure_ascii=False, indent=2))
        return

    kind, payload = monta(args)
    device_id = args.placa
    if args.comando == "falar" and not args.sem_voz:
        try:
            pcm = lylu_voz.sintetizar(args.texto + (". " + args.forte if args.forte else ""))
            if pcm:
                placa = lylu_bridge.bridge_call(settings, "state", device_id=device_id).get("device")
                adpcm = lylu_bridge.device_takes_adpcm(placa)
                up = lylu_bridge.bridge_audio_up(settings, pcm, device_id=device_id, adpcm=adpcm)
                payload["audio_id"] = up["audio_id"]
                payload["formato"] = lylu_voz.FORMATO_ADPCM if adpcm else lylu_voz.FORMATO_PCM
                device_id = up.get("device_id") or device_id
        except Exception as exc:  # sem voz, vai só o balão
            print(f"aviso: sem voz ({exc})", file=sys.stderr)
    resp = lylu_bridge.bridge_call(settings, "push", kind=kind, payload=payload, device_id=device_id)
    print(json.dumps({"ok": resp.get("ok"), "kind": kind, "payload": payload}, ensure_ascii=False))


if __name__ == "__main__":
    main()
