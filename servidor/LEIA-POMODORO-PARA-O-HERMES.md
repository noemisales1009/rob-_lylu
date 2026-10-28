# Pomodoro: você fica sabendo do foco e avisa a pausa

Para: Hermes. Da: Noemi / Claude Code.

A Mica pediu: "quero que o Hermes saiba que estou em pomodoro e me alerte que
tenho que dar pausa, e de quantos minutos". A placa nova manda três avisos
(`kind: event`):

| aviso | quando | o que você faz |
|---|---|---|
| `foco_inicio` | ela tocou em "Começar" (`t`, `minutos`, `bloco`, `blocos`) | fica sabendo; responde `SILENCIO` e não puxa conversa até a pausa |
| `foco_fim` | o relógio do foco chegou a zero; agora vem `pausa_min`, `pausa_longa` e `pausa_sozinha` | **avisa a pausa e de quantos minutos ela é**, em uma frase |
| `pausa_fim` | a pausa acabou (`minutos`, `proximo_bloco`, `foco_min`, `foco_sozinho`, `pulou`) | avisa que acabou, sem cobrar; se `pulou`, `SILENCIO` |

A placa busca a sua resposta ~8 s depois de `foco_fim` e de `pausa_fim`, sem
esperar a volta normal de 10 s. Os três avisos também entram no histórico
(`historico --tipo foco_inicio`...).

## O que instalar

Só o relay. `mudancas-pomodoro-relay.patch` mexe **apenas** em `describe_event()`
do `lylu_bridge.py`. Foi feito contra o arquivo que você publicou (deploy 10, o
que você mandou para a sessão do app) e aplica limpo nele:

```
cd <pasta do relay> && git apply mudancas-pomodoro-relay.patch   # ou patch -p1
```

Depois, reinicie o relay. A Edge Function não muda.

## Bom saber

- Se um `foco_fim` ou `pausa_fim` falhar e voltar pelo lease de 15 minutos, o
  aviso já perdeu a hora. Vale responder `SILENCIO` quando o aviso for velho.
- Placa antiga (sem `pausa_min`) continua com a frase de antes ("Agora começa a
  pausa").
