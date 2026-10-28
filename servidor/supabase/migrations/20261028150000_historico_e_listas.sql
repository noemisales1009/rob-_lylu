-- Ponte Lylu v4: o que aconteceu (histórico) e as listas da placa em tabelas.
-- Pode rodar mais de uma vez sem estragar nada.

-- 1) Histórico: cada coisa que aconteceu, com dia e hora. Quem escreve é só a
--    Edge Function: os avisos da placa (tarefa feita, foco terminado, recado
--    visto...), as conversas (o que a Mica disse e o que o Hermes respondeu) e
--    o que o Hermes mandou para a placa.
create table if not exists public.lylu_historico (
  id bigint generated always as identity primary key,
  device_id text not null,
  em timestamptz not null default now(),
  tipo text not null,
  dados jsonb not null default '{}'::jsonb
);
create index if not exists lylu_historico_device_em on public.lylu_historico (device_id, em desc);
create index if not exists lylu_historico_device_tipo_em on public.lylu_historico (device_id, tipo, em desc);

-- 2) As listas que estão na placa, uma linha por item. Quem manda é a placa: a
--    cada retrato (report) a função acerta estas tabelas pelo que está na tela.
--    Assim elas continuam valendo com a placa desligada. feita_em é quando a
--    tarefa foi marcada (o retrato chega ~2 s depois do toque).
create table if not exists public.lylu_tarefas (
  device_id text not null,
  chave text not null,                 -- "id:<todoist>" ou "t:<texto>"
  lista text not null check (lista in ('missao', 'bonus')),
  ordem int not null default 0,
  texto text not null,
  todoist_id text,
  feita boolean not null default false,
  feita_em timestamptz,
  atualizado_em timestamptz not null default now(),
  primary key (device_id, chave)
);

create table if not exists public.lylu_recados (
  device_id text not null,
  chave text not null,                 -- "id:<id>" ou "t:<título>|<quando>"
  ordem int not null default 0,
  recado_id text,
  titulo text not null,
  quando text,                         -- AAAA-MM-DD ou AAAA-MM-DDTHH:MM, como o Hermes mandou
  dia text,
  hora text,
  detalhe text,
  visto boolean not null default false,
  atualizado_em timestamptz not null default now(),
  primary key (device_id, chave)
);

-- 3) Só a Edge Function (service_role) enxerga: RLS ligado e nenhuma política.
alter table public.lylu_historico enable row level security;
alter table public.lylu_tarefas enable row level security;
alter table public.lylu_recados enable row level security;
