# Ponte Lylu v4: histórico e listas guardados no Supabase

Para: Hermes. Da: Noemi / Claude Code.

## Por que

Hoje o Supabase só guarda o **retrato de agora** da placa (`lylu_devices.estado`).
Ninguém sabe o que aconteceu ontem, e com a placa desligada você não tem as
listas em forma de tabela. A v4 acrescenta:

- **`lylu_historico`**: cada coisa que aconteceu, com dia e hora. A própria Edge
  Function anota, sem depender do relay:
  - os avisos da placa (`tarefa_feita`, `tarefa_desfeita`, `foco_fim`,
    `recado_entendi`, `recado_adiado`);
  - as conversas (`conversa`: o que a Mica disse e o que você respondeu) e os
    seus comentários sobre avisos (`comentario`);
  - o que você mandou para a placa (`hermes_falou`, `hermes_estado`,
    `hermes_comando`; sem o áudio).
- **`lylu_tarefas`** e **`lylu_recados`**: a missão, o bônus e os recados, uma
  linha por item. Quem manda nelas é a placa: a cada retrato (`report`) a função
  acerta as tabelas pelo que está na tela, e elas continuam valendo com a placa
  desligada. Cada tarefa tem `feita_em`, a hora em que ela marcou. As listas de
  exemplo não entram.
- Duas leituras novas, só para o relay: `historico` e `listas`. A placa não
  consegue ler nenhuma das duas.

Nada muda na placa nem no `lylu_bridge.py`.

## O que vai no pacote

- `supabase/migrations/20261028150000_historico_e_listas.sql`: as 3 tabelas, com
  RLS ligado e sem política (só a service_role da função enxerga). Pode rodar
  mais de uma vez.
- `mudancas-v3-para-v4.patch`: a diferença no `index.ts`, no `lylu_placa.py` e
  na skill `lylu-placa`, contra a v3 que te mandamos.
- Os arquivos completos, só como referência.

## Como aplicar (NESTA ORDEM)

Você já mexeu no `index.ts` (o lease contra resposta repetida, a espera de
0,25 s no claim). **Não substitua o arquivo inteiro**: aplique o patch por cima
da sua versão (`git apply --3way` ou `patch -p1`). Ele não encosta no claim.

1. **SQL primeiro**: rode `20261028150000_historico_e_listas.sql` (SQL Editor do
   projeto `ocsiyvpulcxzirzgojjv`, ou `supabase db push`).
2. **Depois a função**:
   `supabase functions deploy lylu-bridge --project-ref ocsiyvpulcxzirzgojjv --no-verify-jwt`.
   Se a função subir antes do SQL, nada quebra: a placa continua funcionando,
   só deixa de anotar até as tabelas existirem.
3. **`lylu_placa.py` e a skill `lylu-placa`**: troque pelos novos, ou aplique a
   parte deles no patch. Não precisa reiniciar o relay, porque o `lylu_bridge.py`
   não mudou.

## Teste

1. `curl https://ocsiyvpulcxzirzgojjv.supabase.co/functions/v1/lylu-bridge` → `"version":4`.
2. `lylu_placa.py listas` → missão, bônus e recados. Logo depois de instalar, as
   tabelas estão vazias e a função preenche pelo último retrato da placa.
3. A Mica marca uma tarefa na placa. Em ~3 s:
   - `lylu_placa.py historico --hoje` mostra `tarefa_feita`;
   - `lylu_placa.py listas` mostra a tarefa com `feita: true` e `feita_em`.
4. A Mica fala com você pela placa → `lylu_placa.py historico --hoje --tipo conversa`
   mostra a `pergunta` (a transcrição) e a sua `resposta`.

Testamos aqui contra um Supabase de mentira (34 conferências: retrato, tarefa que
sai da tela, `feita_em` que não muda a cada retrato, listas de exemplo de fora,
avisos, conversa, comentário, push, filtros do histórico, placa sem acesso,
retrato malformado), e o `lylu_placa.py` contra uma ponte falsa.

## Bom saber

- O `em` do histórico está em UTC (Brasília = UTC−3). O `--hoje` do
  `lylu_placa.py` já começa na meia-noite de Brasília.
- O histórico guarda o **texto** das conversas, nunca o áudio. Para apagar:
  `delete from lylu_historico where tipo = 'conversa';` (ou tudo, com `truncate`).
- Antes de um `falar` espontâneo, vale olhar `historico --hoje --tipo hermes_falou`
  para não repetir o mesmo palpite no mesmo dia.
