-- Ponte Lylu ↔ Hermes, versão 2: voz, estado da placa e mensagens do Hermes.
-- Pode rodar mais de uma vez sem estragar nada.

-- 1) O que a placa está mostrando agora (ela manda a cada mudança).
alter table public.lylu_devices
  add column if not exists estado jsonb,
  add column if not exists estado_em timestamptz;

-- 2) Tipos novos de mensagem: voz (fala da placa), estado e comando (do Hermes).
--    A v1 aceitava só chat e event. Se a coluna tiver um CHECK, ele é trocado;
--    se for um ENUM, os valores entram nele.
do $$
declare
  c record;
  tipo text;
  tipo_dado text;
begin
  select data_type, udt_name into tipo_dado, tipo
    from information_schema.columns
   where table_schema = 'public' and table_name = 'lylu_bridge_messages' and column_name = 'kind';

  if tipo_dado = 'USER-DEFINED' then
    execute format('alter type public.%I add value if not exists %L', tipo, 'voz');
    execute format('alter type public.%I add value if not exists %L', tipo, 'estado');
    execute format('alter type public.%I add value if not exists %L', tipo, 'comando');
  else
    for c in
      select conname from pg_constraint
       where conrelid = 'public.lylu_bridge_messages'::regclass
         and contype = 'c'
         and pg_get_constraintdef(oid) ilike '%kind%'
    loop
      execute format('alter table public.lylu_bridge_messages drop constraint %I', c.conname);
    end loop;
    alter table public.lylu_bridge_messages
      add constraint lylu_bridge_messages_kind_check
      check (kind in ('chat', 'event', 'voz', 'estado', 'comando'));
  end if;
end $$;

-- 3) Onde o áudio espera alguns segundos entre a placa e o relay. Privado e sem
--    política nenhuma: só a Edge Function (service_role) enxerga. Cada arquivo
--    é apagado assim que é usado; o que sobrar some em 30 minutos.
insert into storage.buckets (id, name, public, file_size_limit)
values ('lylu-audio', 'lylu-audio', false, 1310720)
on conflict (id) do nothing;
