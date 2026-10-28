# Ponte Lylu: o app do celular

Para: Hermes. Da: Noemi / Claude Code.

## Por que

A Mica tem agora um app no celular para falar com você, por texto, áudio ou
mandando uma lista de tarefas. Hoje o app só manda: a sua resposta vai para a
placa. Ela pediu a resposta **nos dois**, a placa e o celular, porque pode
estar longe da placa.

## O que muda

- **Role nova `app`** em `lylu_bridge_credentials`, com o mesmo `device_id` da
  placa. O app manda texto (`send`, só `chat`) e voz (`voz`) pela fila da placa,
  e a sua resposta continua indo para a placa como hoje. O app **não** faz
  `poll`/`ack`, não manda eventos e não marca a placa como online.
- **Ação nova `resposta`** (só para o app): devolve, pelos `message_id` que o
  `send`/`voz` retornou, o status do pedido e a cópia da sua resposta (texto,
  humor, voz). Filtra por `device_id` do token, `direction = agent_to_device` e
  `request_id`. Não confirma nada.
- **Marca de origem**: pedidos do app levam `payload.origem = "app"`, e o
  `complete` copia essa marca para a resposta. Por causa dela:
  - o `ack` da placa **não apaga** a voz dessa resposta, para o celular também
    ouvir. Ela some pelo TTL de 30 min, porque a limpeza agora roda também no
    `audio_up`. Conversa que começa na placa continua com a voz apagada no ack;
  - o histórico (`conversa`) ganha `origem: "app"` ou `"placa"`;
  - o relay avisa você nas instruções: "ela está no app do celular e pode
    estar longe da placa".
- **CORS** liberado (`OPTIONS` e cabeçalhos), para o app chamar a função direto.
  A autenticação continua só pelo `x-lylu-token`.
- O `GET` passa a ter `"app": true`.

A placa não precisa de nada para funcionar com isto. Outra sessão do Claude
preparou um firmware que separa as respostas dela das do app, e ele só vai ser
gravado quando a Mica aprovar.

## O que vai no pacote

- `supabase/migrations/20261028180000_chave_do_app.sql`: aceita `app` no CHECK
  de `role`, se houver, e cria a chave do app presa à placa que falou com a
  ponte por último. Só vai o SHA-256 da chave: a chave fica no app. Pode rodar
  mais de uma vez.
- `mudancas-v4-para-app.patch`: a diferença no `index.ts` e no `lylu_bridge.py`,
  contra a v4.
- Os arquivos completos, só como referência.

## Como aplicar (NESTA ORDEM)

Você tem a sua "versão 5" do `index.ts` (lease no claim, áudio repetido), que
não está aqui. **Não substitua o arquivo inteiro.** Aplique o patch por cima:
`git apply --3way mudancas-v4-para-app.patch` ou `patch -p1`. Ele não encosta
no claim.

1. **SQL**: rode `20261028180000_chave_do_app.sql` no projeto
   `ocsiyvpulcxzirzgojjv`. Se a `lylu_bridge_credentials` tiver outra coluna
   NOT NULL sem default, acrescente ao `insert`. Se o CHECK de `role` tiver
   papéis além de `device` e `relay`, inclua-os antes de rodar.
2. **Função**:
   `supabase functions deploy lylu-bridge --project-ref ocsiyvpulcxzirzgojjv --no-verify-jwt`.
3. **Relay**: aplique a parte do `lylu_bridge.py` e reinicie o relay. Se não
   reiniciar, tudo funciona; só não chega o aviso "ela está no celular".

## Teste

1. `curl https://ocsiyvpulcxzirzgojjv.supabase.co/functions/v1/lylu-bridge` →
   `"app": true`.
2. `select role, device_id, enabled from lylu_bridge_credentials;` → uma linha
   `app` com o mesmo `device_id` da placa.
3. Avise a Mica. Ela manda um "oi" pelo app, e a sua resposta aparece no
   celular e na placa. No `historico --hoje --tipo conversa`, a conversa vem
   com `origem: "app"`.

Testamos aqui contra um Supabase de mentira, com storage em memória. Foram 30
conferências do app: CORS, o que o app não pode fazer, a fila da placa, a
resposta pendente e pronta, a voz que fica depois do ack só quando é do app, o
histórico com origem, a voz mandada pelo app, nada de outra placa vazando e a
limpeza no `audio_up`. Os testes da v4 continuam passando, e o `deno check`
está limpo.

## Bom saber

- A chave do app é revogável sozinha:
  `update lylu_bridge_credentials set enabled = false where role = 'app';`.
  A placa continua funcionando.
- Pedidos de lista chegam como texto normal, pedindo para **somar** tarefas.
  Como o `tarefas` do `lylu_placa.py` troca a lista inteira da placa, mande a
  lista completa: as que já estavam e não foram feitas, mais as novas.
