---
name: lylu-placa
description: Mexer na placa física da Lylu (a tela na mesa da Mica) — ver o que ela está mostrando, mandar a Lylu falar, trocar a missão do dia e o bônus, marcar tarefas, criar e apagar recados, mudar o humor e controlar o pomodoro. Use sempre que a Mica pedir algo "na Lylu", "na placa", "na tela", ou quando um lembrete/tarefa dela precisar aparecer lá.
---

# Placa da Lylu

A Lylu tem um corpo físico: uma placa ESP32-P4 com tela de 7" na mesa da Mica,
com microfone e alto-falante. Ela conversa com você pela ponte do Supabase
(`lylu-bridge`) e pelo relay `lylu_bridge.py`. Esta skill é o caminho
contrário: **você** mandando coisas para a placa, mesmo sem ela ter perguntado.

Tudo passa pelo comando `lylu_placa.py` (na mesma pasta do relay). Ele usa o
token do relay — `LYLU_BRIDGE_URL` e `LYLU_RELAY_TOKEN` — do ambiente ou de
`--env <arquivo>`. A placa recebe em até ~10 segundos.

```
python3 <pasta-do-relay>/lylu_placa.py [--env <arquivo .env>] <comando> ...
```

## Antes de mudar: veja o que está lá

```
lylu_placa.py estado
```

Devolve o JSON que a placa mandou por último: tela aberta, humor, `missao` e
`bonus` (cada tarefa com `id`, `t` = texto, `f` = feita), `recados`, `pomodoro`
(passo, rodando, tarefa, tempo restante, blocos), `dificil` (modo dia difícil),
e `online` (se ela falou com a ponte nos últimos 2 minutos).

Quando a conversa vem **da própria placa**, o relay já coloca esse estado nas
suas instruções — não precisa chamar `estado` de novo.

## O que ficou guardado no Supabase

```
lylu_placa.py listas
lylu_placa.py historico --hoje
lylu_placa.py historico --dias 7 --tipo foco_fim
```

- **`listas`**: a missão, o bônus e os recados, uma linha por item, no jeito
  que estavam na tela da última vez (valem com a placa **desligada**). Cada
  tarefa tem `feita` e `feita_em` (quando a Mica marcou).
- **`historico`**: o que aconteceu, do mais novo para o mais velho, com `em`
  (hora em UTC — no Brasil é 3 h a menos), `tipo` e `dados`. `--hoje` começa na
  meia-noite de Brasília; `--dias N` (até 90); `--tipo` filtra; `--limite` (até 500).

| tipo | quando entra |
|---|---|
| `tarefa_feita` / `tarefa_desfeita` | ela marcou/desmarcou na placa (`t`, `id`, `missao`, `missao_completa`) |
| `foco_fim` | terminou um bloco de foco (`t`, `minutos`, `bloco`, `feitos_hoje`) |
| `recado_entendi` / `recado_adiado` | tocou em "Entendi" / "Lembrar depois" (`titulo`, `id`) |
| `conversa` | ela falou ou escreveu com você (`via`, `pergunta`, `resposta`, `humor`) |
| `comentario` | você comentou um aviso da placa (`sobre`, `resposta`) |
| `hermes_falou` / `hermes_estado` / `hermes_comando` | o que você mandou para a placa |

Use para responder "quantos pomodoros eu fiz hoje?", "o que eu terminei
ontem?", "o que a gente conversou de manhã?" — e para não repetir um palpite
que você já deu hoje (olhe `hermes_falou` antes de um `falar` espontâneo).

## Comandos

| O que | Comando |
|---|---|
| A Lylu falar (balão + voz) | `falar "Bora beber água?" [--forte "segunda linha"] [--humor cuidadora] [--sem-voz]` |
| Humor na barra de cima | `humor brincalhona [--minutos 60]` |
| Missão do dia e bônus (substitui as listas) | `tarefas --missao "A" --missao "B" --bonus "C"` |
| Guardar o id do Todoist junto | `tarefas --missao "8812345::Enviar orçamento"` |
| Marcar / desmarcar tarefa | `marcar "Tomar o remédio"` · `marcar 8812345 --desfazer` |
| Novo recado (ou atualizar pelo id) | `recado "Dentista" --quando 2026-10-29T14:00 --detalhe "Levar o raio-x" [--id X] [--tipo compromisso] [--fala "..." --forte "..."]` |
| Apagar recado | `recado-apagar "Dentista"` (id ou título) · `recados-limpar` |
| Pomodoro | `pomodoro iniciar --tarefa "Relatório" --minutos 25` · `pomodoro pausar` · `pomodoro continuar` · `pomodoro parar` |
| Tempos guardados do pomodoro | `pomodoro-config --foco 25 --curta 5 --blocos 4 --longa 20` |
| Mostrar uma tela | `tela Tarefas` (Foco, Tarefas, Recados, Semana, Relógio, Cantinho, Ajustes) |
| Modo dia difícil | `dia-dificil on` · `dia-dificil off` |

Humores válidos: comemorando, animada, cuidadora, brincalhona, alertando,
julgadora, entediada, descansando. No modo dia difícil a placa ignora
julgadora e alertando.

## Regras da Lylu (valem aqui também)

- **Missão do dia**: de 1 a 3 tarefas (as P1 do Todoist). O resto é bônus, até 6.
  A placa mostra só isso; não mande a lista inteira do Todoist.
- **Lembrete não é tarefa.** Tarefa é trabalho que ela senta para fazer (cabe num
  pomodoro: "escrever o relatório", "enviar o orçamento"). Ir a algum lugar,
  comprar, ligar, buscar, "lembrar de…" é **recado**: `recado "Ir à farmácia"
  --detalhe "Comprar o remédio" [--quando ...]`, nunca na missão nem no bônus.
  Ex.: "lembrar de ir na padaria" e "ir na farmácia comprar remédio" são recados.
- **Lembrete tem quando.** Sempre `--quando AAAA-MM-DD` (o dia; sem dia dito, é
  hoje) e, sem hora certa, `--turno manha|tarde|noite` ("à tarde", "de noite"...).
  A placa mostra "HOJE · À NOITE". Se ela não disse nem dia nem turno, pergunte
  numa frase curta ("pra quando: tarde ou noite?") em vez de inventar.
- **Recado com data**: use `--quando` (AAAA-MM-DD ou AAAA-MM-DDTHH:MM). A placa
  calcula sozinha "hoje", "amanhã", "sexta"… Texto livre em `--dia` só quando
  não houver data.
- **Lembrar ≠ opinar**: recado e tarefa são função, nunca falham. Fala espontânea
  (`falar`) é palpite: uma vez, curta, sem insistir, nunca em horário fixo.
- **Isso acolhe ou isso cobra?** Se cobra, não mande.
- O texto do balão tem espaço para ~280 caracteres; a voz lê tudo. Prefira uma
  ou duas frases.

## Avisos que chegam da placa

O relay repassa para você, como mensagem na conversa `lylu-fisica-01`, quando a
Mica: marca/desmarca uma tarefa (`tarefa_feita` / `tarefa_desfeita`, com o `id`
se houver — **conclua ou reabra no Todoist também**), termina um bloco de foco
(`foco_fim`), ou toca em "Entendi" / "Lembrar depois" num recado. Responda com
uma frase curta só se valer a pena; senão responda exatamente `SILENCIO`.
