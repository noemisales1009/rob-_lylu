# Ponte Lylu ↔ Hermes v2: o que instalar no servidor

Para: Hermes (você instalou a v1 da ponte e o relay em 05/10/2026).
Da: Noemi / Claude Code, que está fazendo o firmware novo da placa.

## O que muda

1. **Voz.** A placa grava a fala da Mica (PCM 16 kHz mono 16 bits) e sobe pela
   ponte. O relay baixa o áudio, transcreve, pergunta a você, transforma a
   resposta em voz e sobe de volta. A placa toca no alto-falante.
2. **Você controla a placa.** A ferramenta `lylu_placa.py` (skill `lylu-placa`)
   manda para a placa, a qualquer momento: a fala, o humor, a missão do dia e o
   bônus, os recados e os comandos do pomodoro.
3. **A placa conta o que está mostrando.** Ela manda um relatório (tarefas,
   recados, pomodoro, tela, humor) a cada mudança. O relay coloca esse relatório
   nas suas instruções, e `lylu_placa.py estado` lê na hora.
4. **Avisos da placa.** Quando a Mica marca uma tarefa, termina um foco ou toca
   num recado, chega para você um aviso na conversa `lylu-fisica-01`. Responda
   `SILENCIO` quando não valer comentar.
5. **Menos chamadas.** O relay agora espera até 20 s em cada `claim`, em vez de
   chamar a função a cada 2 s. Do jeito antigo eram ~1,3 milhão de invocações
   por mês, e o plano gratuito tem 500 mil. A placa nova pergunta a cada 10 s.

A placa continua só com o token estreito dela, e nenhuma chave nova vai para ela.
Áudio fica no bucket privado `lylu-audio` só até ser usado: é apagado na hora,
e o que sobrar some em 30 minutos.

## Arquivos (pasta `servidor/` do repositório)

```
supabase/migrations/20261028120000_ponte_v2.sql   colunas novas, tipos novos, bucket
supabase/functions/lylu-bridge/index.ts           Edge Function v2 (substitui a v1)
supabase/functions/lylu-bridge/deno.json
relay/lylu_bridge.py                              relay v2 (substitui o atual)
relay/lylu_voz.py                                 fala → texto e texto → fala
relay/lylu_placa.py                               a sua ferramenta para mexer na placa
relay/requirements.txt
hermes-skill/lylu-placa/SKILL.md                  a skill que explica a ferramenta
```

## Passo a passo (nesta ordem)

### 1. Banco (Supabase, projeto `ocsiyvpulcxzirzgojjv`)

Rode `supabase/migrations/20261028120000_ponte_v2.sql` no SQL Editor (ou com
`psql`). Ele pode rodar duas vezes sem problema. Ele faz três coisas:
- acrescenta `estado jsonb` e `estado_em timestamptz` em `lylu_devices`;
- libera os tipos `voz`, `estado` e `comando` em `lylu_bridge_messages.kind`.
  Ele troca o CHECK, ou acrescenta os valores se a coluna for ENUM;
- cria o bucket privado `lylu-audio`.

Confira: `select column_name from information_schema.columns where table_name = 'lylu_devices';`
deve listar `estado` e `estado_em`.

### 2. Edge Function

```
supabase functions deploy lylu-bridge --project-ref ocsiyvpulcxzirzgojjv --no-verify-jwt
```

**`--no-verify-jwt` é obrigatório**: a placa não manda `Authorization`, só
`x-lylu-token`. Sem isso ela recebe 401 e fica offline.

Confira: `curl https://ocsiyvpulcxzirzgojjv.supabase.co/functions/v1/lylu-bridge`
deve devolver `"version":2`.

A v2 aceita todas as chamadas da v1 (placa e relay), então nada quebra nesse
meio-tempo.

### 3. Relay

Na pasta onde está o `lylu_bridge.py` de hoje:
1. Guarde uma cópia do atual (`lylu_bridge.v1.py`).
2. Coloque `lylu_bridge.py`, `lylu_voz.py` e `lylu_placa.py` lá (os três
   juntos, porque um importa o outro).
3. Instale a voz:
   ```
   pip install -r requirements.txt     # faster-whisper e edge-tts
   sudo apt install ffmpeg             # converte a voz do edge-tts para o formato da placa
   ```
   Se a máquina não aguentar o faster-whisper (ele baixa o modelo `small`, uns
   500 MB, e usa CPU), use `LYLU_STT=groq` com `GROQ_API_KEY`, ou `LYLU_STT=openai`
   com `OPENAI_API_KEY`. Se você já tem STT/TTS configurado em outro lugar, pode
   trocar as funções `transcrever()` e `sintetizar()` do `lylu_voz.py`, desde que
   entrem e saiam em PCM 16 kHz mono 16 bits.
4. As variáveis do serviço continuam as mesmas (`LYLU_BRIDGE_URL`,
   `LYLU_RELAY_TOKEN`, `API_SERVER_KEY`, `HERMES_API_URL`). As novas são todas
   opcionais:
   ```
   LYLU_CLAIM_WAIT=20                  # segundos de espera em cada claim (0 = jeito antigo)
   LYLU_STT=auto                       # auto | local | groq | openai
   LYLU_STT_MODELO=small               # modelo do faster-whisper
   LYLU_TTS=auto                       # auto | edge | openai | nenhum
   LYLU_VOZ=pt-BR-FranciscaNeural      # voz da Lylu no edge-tts
   ```
5. Reinicie o serviço do relay e olhe o log. Deve aparecer
   `ponte Lylu v2 iniciada`.

### 4. Skill

Copie `hermes-skill/lylu-placa/` para a pasta de skills do perfil `lylu` (e do
perfil principal, se a Mica também falar com você pelo Telegram sobre a placa).
No `SKILL.md`, troque `<pasta-do-relay>` pelo caminho de verdade. Se as variáveis
do relay estão num arquivo de ambiente (do systemd, por exemplo), coloque
`--env <arquivo>` no exemplo da skill. O perfil `lylu` precisa poder rodar
comandos de terminal.

### 5. Testes

```
python3 lylu_placa.py estado
python3 lylu_placa.py falar "Oi, Mica! Agora eu também falo."
```

O `estado` responde com `"online": true` quando a placa está ligada. Até o
firmware novo ser gravado, o campo `estado` dentro dele vem vazio. Isso é normal.

**Enquanto a placa estiver com o firmware antigo**, teste só o `falar`. O
firmware antigo só confirma mensagens que têm `text`. Um `tarefas`, `recado` ou
`pomodoro` ficaria pendente na fila e acabaria tapando as respostas. Se isso
acontecer, limpe as pendências:
`update lylu_bridge_messages set status = 'failed' where direction = 'agent_to_device' and status = 'pending' and kind in ('estado', 'comando');`

Com o firmware novo gravado, teste tudo:
```
python3 lylu_placa.py tarefas --missao "Testar a Lylu nova" --bonus "Beber água"
python3 lylu_placa.py recado "Teste de recado" --quando <amanhã>T10:00
python3 lylu_placa.py humor brincalhona --minutos 10
```
Depois a Mica toca em **Falar**, na barra de cima da placa, e fala com você.

### Se algo der errado

- Voltar: reinstale o `lylu_bridge.v1.py` e a função v1. As colunas e o bucket
  novos não atrapalham a v1.
- A placa mostra "A ponte ainda não sabe ouvir": a função implantada ainda é a v1.
- A resposta chega sem voz: veja no log do relay a linha "sem voz para…" (falta
  ffmpeg, edge-tts ou chave).

## Para a Mica saber

Responda para ela, em poucas palavras: o que foi instalado, o resultado dos
testes e o que falhou, se falhou.
