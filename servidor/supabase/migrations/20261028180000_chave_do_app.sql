-- Ponte Lylu: chave do app do celular (role "app").
-- O app usa o mesmo device_id da placa: manda texto e voz pela fila dela e lê a
-- cópia da resposta com a ação "resposta". A chave em si fica só no app; aqui
-- vai o SHA-256 dela. Pode rodar mais de uma vez.

-- Se a coluna role tiver CHECK, ele passa a aceitar 'app'.
do $$
declare
  c record;
  achou boolean := false;
begin
  for c in
    select conname from pg_constraint
    where conrelid = 'public.lylu_bridge_credentials'::regclass
      and contype = 'c'
      and pg_get_constraintdef(oid) ilike '%role%'
  loop
    execute format('alter table public.lylu_bridge_credentials drop constraint %I', c.conname);
    achou := true;
  end loop;
  if achou then
    alter table public.lylu_bridge_credentials
      add constraint lylu_bridge_credentials_role_check check (role in ('device', 'relay', 'app'));
  end if;
end $$;

-- A chave, presa à placa que falou com a ponte por último.
insert into public.lylu_bridge_credentials (role, device_id, token_sha256, enabled)
select 'app', c.device_id, '4d6cb4b787fa6bdeeef3dfcc70237c69347d629da76449c2e036651b5761e54a', true
from public.lylu_bridge_credentials c
where c.role = 'device' and c.enabled
  and not exists (select 1 from public.lylu_bridge_credentials x where x.token_sha256 = '4d6cb4b787fa6bdeeef3dfcc70237c69347d629da76449c2e036651b5761e54a')
order by c.last_seen_at desc nulls last
limit 1;
