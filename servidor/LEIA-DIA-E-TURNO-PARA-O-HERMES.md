# Tarefas do dia, pendentes "de ontem" e turno nos lembretes

Para: Hermes. Da: Noemi / Claude Code. A placa já está com o firmware novo.

Como a versão que está no ar é a sua (deploy 10 e o que você mudou depois),
aqui vão **trechos para colar**, não um patch. A referência completa está em
`relay/lylu_bridge.py` e `relay/lylu_placa.py` do repositório.

## 1. A virada do dia (`dia_novo`)

A Mica: "as tarefas são do dia; se não forem feitas ficam pendentes pro dia
seguinte, e a Lylu tem que me avisar e ver quais são". A placa agora:

- **às 5h**, em silêncio, tira as tarefas feitas e marca as que ficaram com
  `"pendente": true`. Na tela aparece a etiqueta "de ontem";
- **das 9h em diante** (fora do foco e da Conversa; se a placa estava desligada,
  quando ligar), manda o aviso:

```json
{"tipo": "dia_novo", "dia": "2026-10-29",
 "pendentes": [{"t": "Enviar orçamento", "id": "8812345", "missao": true}]}
```

No `describe_event`, antes de `recado_entendi`:

```python
    if tipo == "dia_novo":
        pend = payload.get("pendentes") or []
        if not pend:
            return "Virou o dia na placa e não ficou nada de ontem. Responda exatamente SILENCIO."
        lista = "; ".join(f"“{p.get('t', '')}”" + (f" (id {p['id']})" if p.get("id") else "")
                          + (" [era da missão]" if p.get("missao") else "") for p in pend)
        return (f"Aviso do começo do dia (a placa manda das 9h em diante): ficaram {len(pend)} de ontem: {lista}. "
                "AVISE a Mica, em uma ou duas frases curtas e acolhedoras, quais ficaram (sem cobrar) e pergunte se "
                "ela quer manter hoje. No Todoist, passe essas para hoje. Depois mande a lista do dia com "
                "lylu_placa.py tarefas: as de ontem continuam com a etiqueta “de ontem” sozinhas. "
                "Aqui não responda SILENCIO.")
```

Quando você manda `tarefas` depois, a placa mantém o "de ontem" das que já
estavam lá (pelo id ou pelo texto). Não precisa mandar o campo.

## 2. Lembrete com turno

Na placa, o recado ganhou `turno`: `manha`, `tarde` ou `noite`. Sem hora certa,
ela mostra "HOJE · À NOITE" e diz "Tem uma coisa pra hoje à noite". A hora, se
vier, ganha do turno.

No `lylu_placa.py`, subcomando `recado`:

```python
    s.add_argument("--turno", choices=("manha", "tarde", "noite"),
                   help="para lembrete sem hora certa: a placa mostra 'HOJE · À NOITE'")
```

e incluir `"turno"` na tupla de campos copiados para o payload do recado
(`("id", "quando", "dia", "hora", "turno", "detalhe", ...)`).

Regra (já está na skill `lylu-placa` do repositório): **lembrete tem quando**.
Sempre `--quando AAAA-MM-DD` (sem dia dito, é hoje) e, sem hora certa,
`--turno`. Se a Mica não disse nem dia nem turno, pergunte numa frase curta
("pra quando: tarde ou noite?") em vez de inventar.

## 3. Já combinado antes (confira se está assim)

- `foco_fim` / `pausa_fim` com `"avisei": true` → **SILENCIO** (a placa já tocou
  o áudio dela no zero). Mesmo que você fale, a placa descarta a repetição.

## Teste

- `lylu_placa.py recado "Ir na farmácia" --quando <hoje> --turno noite --detalhe "Comprar o remédio"`
  → a tela de Recados mostra "HOJE · À NOITE".
- A virada só acontece de verdade às 5h. Para ver o aviso antes disso, espere
  amanhã às 9h (se ficar alguma tarefa sem marcar hoje).
