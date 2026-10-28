// Ponte Lylu ↔ Hermes, versão 2.
//
// A placa e o relay nunca se falam direto: os dois só conversam com esta função,
// cada um com o seu token estreito (role "device" ou "relay"). A v2 acrescenta:
//   - voz: a placa sobe a fala (PCM 16 kHz), o relay baixa, transcreve, responde
//     e sobe a voz da resposta; a placa baixa e toca. Todo áudio é apagado assim
//     que é usado, e nenhum passa de 30 minutos guardado.
//   - estado: a placa conta o que está na tela (tarefas, recados, pomodoro...) e
//     o Hermes lê isso pelo relay.
//   - push: o Hermes manda coisas para a placa sem ela ter perguntado (recados,
//     tarefas, humor, comandos do pomodoro).
//   - claim com espera: o relay pode esperar até 25 s por uma mensagem, em vez
//     de chamar a função a cada 2 s (isso estourava a cota de invocações).
// A v4 guarda o que acontece (lylu_historico: avisos da placa, conversas e o que
// o Hermes mandou) e espelha as listas da tela em lylu_tarefas e lylu_recados,
// que continuam valendo com a placa desligada. O relay lê com "historico" e
// "listas". Precisa do SQL 20261028150000_historico_e_listas.sql.
//
// O app do celular (role "app", mesmo device_id da placa) manda texto e voz pela
// fila da placa, e a resposta continua indo para ela. O app lê uma cópia com
// "resposta", sem buscar nem confirmar a fila da placa. Pedidos e respostas do
// app levam origem "app": a voz dessas respostas fica até o TTL em vez de sumir
// no ack, e o histórico diz de onde veio. CORS liberado para o app chamar direto.
//
// Implantar SEM verificação de JWT (a placa não manda Authorization):
//   supabase functions deploy lylu-bridge --no-verify-jwt
import { createClient, type SupabaseClient } from "npm:@supabase/supabase-js@2";

// O app chama do navegador/WebView. Sem cookie: a chave vai no x-lylu-token.
const CORS = {
  "access-control-allow-origin": "*",
  "access-control-allow-methods": "GET, POST, OPTIONS",
  "access-control-allow-headers": "content-type, x-lylu-token, x-lylu-action, x-lylu-formato, x-lylu-tela",
  "access-control-expose-headers": "x-lylu-formato",
  "access-control-max-age": "86400",
};
const JSON_HEADERS = { ...CORS, "content-type": "application/json; charset=utf-8" };
const MAX_BODY = 32_768;
const MAX_TEXT = 4_000;
const MAX_PAYLOAD = 16_384;            // estado e comandos vindos do Hermes
const MAX_ESTADO = 16_384;             // o relatório da placa
const MAX_AUDIO = 1_310_720;           // ~40 s de PCM 16 kHz mono 16 bits
// Formatos de voz e quantos bytes cada um gasta por segundo. O ADPCM (4 bits por
// amostra) é a voz 4x menor: a placa transfere poucos KB/s e a voz crua era a
// maior parte da espera. O poll anuncia esta lista; a placa só manda ADPCM
// depois de ver o formato nela.
const FORMATOS: Record<string, number> = { pcm_s16le_16000: 32_000, ima_adpcm_16000: 8_000 };
const FORMATO_PADRAO = "pcm_s16le_16000";
const AUDIO_BUCKET = "lylu-audio";
const AUDIO_TTL_MS = 30 * 60 * 1000;
const ONLINE_MS = 2 * 60 * 1000;       // a placa pergunta a cada 10 s; 2 min sem notícia = fora
const MAX_WAIT_S = 25;
const DEVICE_KINDS = new Set(["chat", "event"]);
const PUSH_KINDS = new Set(["chat", "estado", "comando"]);

type Db = SupabaseClient;
type Obj = Record<string, unknown>;

function reply(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), { status, headers: JSON_HEADERS });
}

function audioReply(bytes: Uint8Array): Response {
  return new Response(bytes, {
    status: 200,
    headers: { ...CORS, "content-type": "application/octet-stream", "x-lylu-formato": "pcm_s16le_16000" },
  });
}

function isUuid(value: unknown): value is string {
  return typeof value === "string" && /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value);
}

function isObj(value: unknown): value is Obj {
  return !!value && typeof value === "object" && !Array.isArray(value);
}

async function sha256Hex(value: string): Promise<string> {
  const bytes = new TextEncoder().encode(value);
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

// ---------------------------------------------------------------- áudio
// Caminhos: <device>/in/<id>.pcm é a fala da placa; <device>/out/<id>.pcm é a voz
// da resposta. O id é sempre um UUID gerado aqui, então não há como montar um
// caminho fora da pasta da placa.
function audioPath(deviceId: string, pasta: "in" | "out", id: string): string {
  return `${deviceId}/${pasta}/${id}.pcm`;
}

async function audioUp(db: Db, deviceId: string, pasta: "in" | "out", bytes: Uint8Array): Promise<string | null> {
  const id = crypto.randomUUID();
  const { error } = await db.storage.from(AUDIO_BUCKET).upload(audioPath(deviceId, pasta, id), bytes, {
    contentType: "application/octet-stream",
    upsert: false,
  });
  return error ? null : id;
}

async function audioGet(db: Db, path: string): Promise<Uint8Array | null> {
  const { data, error } = await db.storage.from(AUDIO_BUCKET).download(path);
  if (error || !data) return null;
  return new Uint8Array(await data.arrayBuffer());
}

async function audioDel(db: Db, paths: string[]): Promise<void> {
  if (paths.length) await db.storage.from(AUDIO_BUCKET).remove(paths);
}

// O que sobrou de uma conversa que caiu no meio não fica para sempre.
async function audioLimpaVelhos(db: Db, deviceId: string): Promise<void> {
  for (const pasta of ["in", "out"] as const) {
    const { data } = await db.storage.from(AUDIO_BUCKET).list(`${deviceId}/${pasta}`, { limit: 100 });
    const velhos = (data || [])
      .filter((f) => f.created_at && Date.now() - Date.parse(f.created_at) > AUDIO_TTL_MS)
      .map((f) => `${deviceId}/${pasta}/${f.name}`);
    await audioDel(db, velhos);
  }
}

function payloadAudioId(payload: unknown): string | null {
  return isObj(payload) && isUuid(payload.audio_id) ? payload.audio_id : null;
}

// ---------------------------------------------------------------- placas
async function resolveDevice(db: Db, wanted: unknown): Promise<string | null> {
  let q = db.from("lylu_devices").select("device_id").order("last_seen_at", { ascending: false, nullsFirst: false }).limit(1);
  if (typeof wanted === "string" && wanted) q = q.eq("device_id", wanted);
  const { data } = await q;
  return data?.[0]?.device_id ?? null;
}

async function deviceState(db: Db, deviceId: string): Promise<Obj | null> {
  const { data } = await db.from("lylu_devices")
    .select("device_id, last_seen_at, estado, estado_em")
    .eq("device_id", deviceId)
    .maybeSingle();
  if (!data) return null;
  const visto = data.last_seen_at ? Date.parse(data.last_seen_at) : 0;
  return { ...data, online: Date.now() - visto < ONLINE_MS };
}

// ---------------------------------------------------------------- histórico e listas
// Anotar nunca derruba a ação principal: se a tabela ainda não existe (função nova
// com o SQL velho) ou o banco piscou, a conversa segue e só o registro se perde.
async function registra(db: Db, deviceId: string, tipo: string, dados: Obj): Promise<void> {
  try {
    const { error } = await db.from("lylu_historico").insert({ device_id: deviceId, tipo: tipo.slice(0, 40), dados });
    if (error) console.warn("lylu_historico:", error.message);
  } catch (e) {
    console.warn("lylu_historico:", e);
  }
}

function limpo(v: unknown, max: number): string | null {
  return typeof v === "string" && v.trim() ? v.trim().slice(0, max) : null;
}

// Grava as linhas do retrato e tira as que não vieram nele (as que ficaram com
// atualizado_em antigo). Se a gravação falhar, não apaga nada.
async function acertaTabela(db: Db, tabela: string, deviceId: string, linhas: Obj[], agora: string): Promise<void> {
  if (linhas.length) {
    const { error } = await db.from(tabela).upsert(linhas, { onConflict: "device_id,chave" });
    if (error) {
      console.warn(`${tabela}:`, error.message);
      return;
    }
  }
  const { error } = await db.from(tabela).delete().eq("device_id", deviceId).lt("atualizado_em", agora);
  if (error) console.warn(`${tabela}:`, error.message);
}

// As listas do retrato viram linhas. A chave é o id (do Todoist) quando tem,
// senão o texto. As de exemplo (a placa avisa em "aviso") não entram.
async function espelhaListas(db: Db, deviceId: string, estado: Obj): Promise<void> {
  const agora = new Date().toISOString();
  const aviso = typeof estado.aviso === "string" ? estado.aviso : "";
  if (Array.isArray(estado.missao) && Array.isArray(estado.bonus) && !aviso.includes("tarefas")) {
    const { data: antes } = await db.from("lylu_tarefas").select("chave, feita, feita_em").eq("device_id", deviceId);
    const velhas = new Map((antes || []).map((r) => [r.chave as string, r]));
    const linhas = new Map<string, Obj>();
    for (const lista of ["missao", "bonus"] as const) {
      (estado[lista] as unknown[]).forEach((t, ordem) => {
        const texto = isObj(t) ? limpo(t.t, 200) : null;
        if (!isObj(t) || !texto) return;
        const id = limpo(t.id, 64);
        const chave = id ? `id:${id}` : `t:${texto}`;
        if (linhas.has(chave)) return;
        const feita = t.f === true;
        const velha = velhas.get(chave);
        linhas.set(chave, {
          device_id: deviceId, chave, lista, ordem, texto, todoist_id: id, feita,
          feita_em: feita ? (velha?.feita && velha.feita_em ? velha.feita_em : agora) : null,
          atualizado_em: agora,
        });
      });
    }
    await acertaTabela(db, "lylu_tarefas", deviceId, [...linhas.values()], agora);
  }
  if (Array.isArray(estado.recados) && !aviso.includes("recados")) {
    const linhas = new Map<string, Obj>();
    (estado.recados as unknown[]).forEach((r, ordem) => {
      const titulo = isObj(r) ? limpo(r.titulo, 200) : null;
      if (!isObj(r) || !titulo) return;
      const id = limpo(r.id, 64);
      const quando = limpo(r.quando, 32);
      const dia = limpo(r.dia, 40);
      const chave = id ? `id:${id}` : `t:${titulo}|${quando ?? dia ?? ""}`;
      if (linhas.has(chave)) return;
      linhas.set(chave, {
        device_id: deviceId, chave, ordem, recado_id: id, titulo, quando, dia,
        hora: limpo(r.hora, 8), detalhe: limpo(r.detalhe, 400), visto: r.visto === true, atualizado_em: agora,
      });
    });
    await acertaTabela(db, "lylu_recados", deviceId, [...linhas.values()], agora);
  }
}

async function leListas(db: Db, deviceId: string): Promise<Obj | null> {
  const [t, r] = await Promise.all([
    db.from("lylu_tarefas").select("lista, ordem, texto, todoist_id, feita, feita_em, atualizado_em")
      .eq("device_id", deviceId).order("ordem", { ascending: true }),
    db.from("lylu_recados").select("ordem, recado_id, titulo, quando, dia, hora, detalhe, visto, atualizado_em")
      .eq("device_id", deviceId).order("ordem", { ascending: true }),
  ]);
  if (t.error || r.error) return null;
  const tarefas = t.data || [];
  return {
    missao: tarefas.filter((x) => x.lista === "missao"),
    bonus: tarefas.filter((x) => x.lista === "bonus"),
    recados: r.data || [],
  };
}

// ---------------------------------------------------------------- fila
async function claimOne(db: Db): Promise<Obj | null> {
  const { data: rows, error } = await db.from("lylu_bridge_messages")
    .select("message_id, device_id, kind, payload, created_at")
    .eq("direction", "device_to_agent")
    .eq("status", "pending")
    .order("created_at", { ascending: true })
    .limit(1);
  if (error) throw new Error("claim_failed");
  const item = rows?.[0];
  if (!item) return null;
  const { data: claimed, error: updateError } = await db.from("lylu_bridge_messages")
    .update({ status: "processing", claimed_at: new Date().toISOString() })
    .eq("message_id", item.message_id)
    .eq("status", "pending")
    .select("message_id")
    .maybeSingle();
  if (updateError) throw new Error("claim_failed");
  return claimed ? item : null;
}

async function processingRequest(db: Db, requestId: string): Promise<Obj | null> {
  const { data } = await db.from("lylu_bridge_messages")
    .select("message_id, device_id, kind, payload")
    .eq("message_id", requestId)
    .eq("direction", "device_to_agent")
    .eq("status", "processing")
    .maybeSingle();
  return data;
}

async function enqueueToDevice(db: Db, deviceId: string, kind: string, payload: Obj, requestId: string | null) {
  return await db.from("lylu_bridge_messages").insert({
    device_id: deviceId,
    direction: "agent_to_device",
    kind,
    payload,
    request_id: requestId,
    status: "pending",
  }).select("message_id").single();
}

// A fala que chegou para ser respondida já foi lida pelo relay: some agora.
async function dropRequestAudio(db: Db, request: Obj): Promise<void> {
  const id = request.kind === "voz" ? payloadAudioId(request.payload) : null;
  if (id) await audioDel(db, [audioPath(request.device_id as string, "in", id)]);
}

// ---------------------------------------------------------------- a placa
async function deviceAction(db: Db, deviceId: string, action: string, body: Obj, audio: Uint8Array | null, formato: string, req: Request, origem: string | null = null) {
  if (action === "heartbeat") return reply(200, { ok: true });

  if (action === "report") {
    const estado = body.estado;
    if (!isObj(estado) || JSON.stringify(estado).length > MAX_ESTADO) return reply(400, { error: "invalid_estado" });
    const { error } = await db.from("lylu_devices")
      .update({ estado, estado_em: new Date().toISOString() })
      .eq("device_id", deviceId);
    if (error) return reply(500, { error: "report_failed" });
    await espelhaListas(db, deviceId, estado).catch((e) => console.warn("listas:", e));
    return reply(200, { ok: true });
  }

  if (action === "send") {
    const kind = typeof body.kind === "string" && DEVICE_KINDS.has(body.kind) ? body.kind : "chat";
    const payload = origem && isObj(body.payload) ? { ...body.payload, origem } : body.payload;
    if (!isObj(payload) || JSON.stringify(payload).length > MAX_PAYLOAD) return reply(400, { error: "invalid_payload" });
    const text = payload.text;
    if (kind === "chat" && (typeof text !== "string" || text.trim().length < 1 || text.length > MAX_TEXT)) {
      return reply(400, { error: "invalid_text" });
    }
    const { data, error } = await db.from("lylu_bridge_messages").insert({
      device_id: deviceId,
      direction: "device_to_agent",
      kind,
      payload,
      status: "pending",
    }).select("message_id").single();
    if (error) return reply(500, { error: "enqueue_failed" });
    if (kind === "event") {
      const { tipo, ...dados } = payload;
      await registra(db, deviceId, typeof tipo === "string" && tipo ? tipo : "evento", dados);
    }
    return reply(202, { ok: true, message_id: data.message_id });
  }

  if (action === "voz") {
    if (!audio) return reply(400, { error: "missing_audio" });
    await audioLimpaVelhos(db, deviceId);
    const audioId = await audioUp(db, deviceId, "in", audio);
    if (!audioId) return reply(500, { error: "audio_store_failed" });
    const ms = Math.round(audio.length * 1000 / FORMATOS[formato]);
    const { data, error } = await db.from("lylu_bridge_messages").insert({
      device_id: deviceId,
      direction: "device_to_agent",
      kind: "voz",
      payload: { audio_id: audioId, ms, formato, tela: req.headers.get("x-lylu-tela") || null, ...(origem ? { origem } : {}) },
      status: "pending",
    }).select("message_id").single();
    if (error) {
      await audioDel(db, [audioPath(deviceId, "in", audioId)]);
      return reply(500, { error: "enqueue_failed" });
    }
    return reply(202, { ok: true, message_id: data.message_id });
  }

  if (action === "poll") {
    const { data, error } = await db.from("lylu_bridge_messages")
      .select("message_id, kind, payload, request_id, created_at")
      .eq("device_id", deviceId)
      .eq("direction", "agent_to_device")
      .eq("status", "pending")
      .order("created_at", { ascending: true })
      .limit(5);
    if (error) return reply(500, { error: "poll_failed" });
    return reply(200, { ok: true, messages: data || [], audio_formatos: Object.keys(FORMATOS) });
  }

  if (action === "audio_get") {
    if (!isUuid(body.audio_id)) return reply(400, { error: "invalid_audio_id" });
    const bytes = await audioGet(db, audioPath(deviceId, "out", body.audio_id));
    if (!bytes) return reply(404, { error: "audio_not_found" });
    return audioReply(bytes);
  }

  if (action === "ack") {
    const ids = Array.isArray(body.message_ids) ? body.message_ids.filter(isUuid).slice(0, 10) : [];
    if (!ids.length) return reply(400, { error: "invalid_message_ids" });
    const { data: rows } = await db.from("lylu_bridge_messages")
      .select("payload")
      .eq("device_id", deviceId)
      .eq("direction", "agent_to_device")
      .in("message_id", ids);
    const { error } = await db.from("lylu_bridge_messages")
      .update({ status: "delivered", delivered_at: new Date().toISOString() })
      .eq("device_id", deviceId)
      .eq("direction", "agent_to_device")
      .in("message_id", ids);
    if (error) return reply(500, { error: "ack_failed" });
    // Entregue = tocada (a placa só confirma depois de baixar o áudio). A voz da
    // resposta a um pedido do app fica até o TTL: o celular também vai ouvir.
    const audios = (rows || [])
      .filter((r) => !(isObj(r.payload) && r.payload.origem === "app"))
      .map((r) => payloadAudioId(r.payload)).filter((x): x is string => !!x);
    await audioDel(db, audios.map((id) => audioPath(deviceId, "out", id)));
    return reply(200, { ok: true });
  }

  return reply(403, { error: "action_not_allowed" });
}

// ---------------------------------------------------------------- o relay (lado do Hermes)
async function relayAction(db: Db, action: string, body: Obj, audio: Uint8Array | null, req: Request) {
  if (action === "claim") {
    // Se o serviço caiu depois de reservar uma mensagem, ela não fica presa
    // para sempre. Chamadas ao Hermes têm limite menor que estes cinco minutos.
    const stale = new Date(Date.now() - 5 * 60 * 1000).toISOString();
    await db.from("lylu_bridge_messages")
      .update({ status: "pending", claimed_at: null })
      .eq("direction", "device_to_agent")
      .eq("status", "processing")
      .lt("claimed_at", stale);
    const waitS = Math.max(0, Math.min(MAX_WAIT_S, Number(body.wait_s) || 0));
    const deadline = Date.now() + waitS * 1000;
    for (;;) {
      let item: Obj | null;
      try {
        item = await claimOne(db);
      } catch {
        return reply(500, { error: "claim_failed" });
      }
      if (item) {
        const estado = await deviceState(db, item.device_id as string);
        return reply(200, { ok: true, message: item, device: estado });
      }
      if (Date.now() >= deadline) return reply(200, { ok: true, message: null });
      await sleep(250);   // com 1 s, a fala esperava até 1 s parada aqui (28/10/2026)
    }
  }

  if (action === "audio_get") {
    if (!isUuid(body.request_id)) return reply(400, { error: "invalid_request_id" });
    const request = await processingRequest(db, body.request_id);
    const id = request && request.kind === "voz" ? payloadAudioId(request.payload) : null;
    if (!request || !id) return reply(404, { error: "audio_not_found" });
    const bytes = await audioGet(db, audioPath(request.device_id as string, "in", id));
    if (!bytes) return reply(404, { error: "audio_not_found" });
    return audioReply(bytes);
  }

  if (action === "audio_up") {
    if (!audio) return reply(400, { error: "missing_audio" });
    const requestId = req.headers.get("x-lylu-request");
    let deviceId: string | null = null;
    if (requestId) {
      if (!isUuid(requestId)) return reply(400, { error: "invalid_request_id" });
      const request = await processingRequest(db, requestId);
      deviceId = (request?.device_id as string) ?? null;
    } else {
      deviceId = await resolveDevice(db, req.headers.get("x-lylu-device"));
    }
    if (!deviceId) return reply(404, { error: "device_not_found" });
    // A voz das respostas ao app não some no ack: a limpeza roda também aqui.
    await audioLimpaVelhos(db, deviceId);
    const audioId = await audioUp(db, deviceId, "out", audio);
    if (!audioId) return reply(500, { error: "audio_store_failed" });
    return reply(200, { ok: true, audio_id: audioId, device_id: deviceId });
  }

  if (action === "complete") {
    const requestId = body.request_id;
    if (!isUuid(requestId)) return reply(400, { error: "invalid_completion" });
    const text = body.text;
    const silent = body.silent === true && (text === undefined || text === null || text === "");
    if (!silent && (typeof text !== "string" || text.length < 1 || text.length > MAX_TEXT)) {
      return reply(400, { error: "invalid_completion" });
    }
    const request = await processingRequest(db, requestId);
    if (!request) return reply(409, { error: "request_not_processing" });
    if (!silent) {
      const pedido = isObj(request.payload) ? request.payload : {};
      const payload: Obj = { text };
      if (isUuid(body.audio_id)) payload.audio_id = body.audio_id;
      if (typeof body.formato === "string" && body.formato in FORMATOS) payload.formato = body.formato;
      if (typeof body.humor === "string" && body.humor.length <= 24) payload.humor = body.humor;
      if (typeof body.forte === "string" && body.forte.length <= 200) payload.forte = body.forte;
      if (typeof body.transcricao === "string") payload.transcricao = body.transcricao.slice(0, 500);
      // A resposta a um pedido do app vai para a placa também, marcada (ver o ack).
      if (pedido.origem === "app") payload.origem = "app";
      const { error: insertError } = await enqueueToDevice(db, request.device_id as string, "chat", payload, requestId);
      if (insertError) return reply(500, { error: "response_enqueue_failed" });
      if (request.kind === "event") {
        await registra(db, request.device_id as string, "comentario", { sobre: pedido.tipo ?? null, resposta: text });
      } else {
        const pergunta = request.kind === "voz" ? payload.transcricao ?? null : pedido.text ?? null;
        await registra(db, request.device_id as string, "conversa",
          { via: request.kind, origem: pedido.origem === "app" ? "app" : "placa", pergunta, resposta: text, humor: payload.humor ?? null });
      }
    }
    await db.from("lylu_bridge_messages")
      .update({ status: "completed", completed_at: new Date().toISOString() })
      .eq("message_id", requestId);
    await dropRequestAudio(db, request);
    return reply(200, { ok: true });
  }

  if (action === "fail") {
    const requestId = body.request_id;
    if (!isUuid(requestId)) return reply(400, { error: "invalid_request_id" });
    const request = await processingRequest(db, requestId);
    await db.from("lylu_bridge_messages")
      .update({ status: "failed", completed_at: new Date().toISOString(), error_code: "relay_error" })
      .eq("message_id", requestId)
      .eq("status", "processing");
    if (request) await dropRequestAudio(db, request);
    return reply(200, { ok: true });
  }

  if (action === "push") {
    const kind = typeof body.kind === "string" ? body.kind : "";
    const payload = body.payload;
    if (!PUSH_KINDS.has(kind)) return reply(400, { error: "invalid_kind" });
    if (!isObj(payload) || JSON.stringify(payload).length > MAX_PAYLOAD) return reply(400, { error: "invalid_payload" });
    if (kind === "chat" && (typeof payload.text !== "string" || !payload.text.trim() || payload.text.length > MAX_TEXT)) {
      return reply(400, { error: "invalid_text" });
    }
    const deviceId = await resolveDevice(db, body.device_id);
    if (!deviceId) return reply(404, { error: "device_not_found" });
    const { data, error } = await enqueueToDevice(db, deviceId, kind, payload, null);
    if (error) return reply(500, { error: "enqueue_failed" });
    const { audio_id: _a, formato: _f, ...dados } = payload;
    await registra(db, deviceId, kind === "chat" ? "hermes_falou" : `hermes_${kind}`, dados);
    return reply(202, { ok: true, message_id: data.message_id, device_id: deviceId });
  }

  if (action === "state") {
    const deviceId = await resolveDevice(db, body.device_id);
    if (!deviceId) return reply(404, { error: "device_not_found" });
    return reply(200, { ok: true, device: await deviceState(db, deviceId) });
  }

  // O que aconteceu na placa, do mais novo para o mais velho. "desde" (ISO)
  // ganha de "dias"; "tipo" filtra (tarefa_feita, foco_fim, conversa...).
  if (action === "historico") {
    const deviceId = await resolveDevice(db, body.device_id);
    if (!deviceId) return reply(404, { error: "device_not_found" });
    const dias = Math.max(1, Math.min(90, Number(body.dias) || 1));
    const pedido = typeof body.desde === "string" ? Date.parse(body.desde) : NaN;
    const desde = new Date(Number.isNaN(pedido) ? Date.now() - dias * 86_400_000 : pedido).toISOString();
    const limite = Math.max(1, Math.min(500, Number(body.limite) || 200));
    let q = db.from("lylu_historico").select("em, tipo, dados")
      .eq("device_id", deviceId).gte("em", desde).order("em", { ascending: false }).limit(limite);
    if (typeof body.tipo === "string" && body.tipo) q = q.eq("tipo", body.tipo);
    const { data, error } = await q;
    if (error) return reply(500, { error: "historico_failed" });
    return reply(200, { ok: true, device_id: deviceId, desde, itens: data || [] });
  }

  // As listas guardadas, que valem com a placa desligada. Logo depois de instalar
  // a v4 elas ainda estão vazias: aí saem do último retrato da placa.
  if (action === "listas") {
    const deviceId = await resolveDevice(db, body.device_id);
    if (!deviceId) return reply(404, { error: "device_not_found" });
    let listas = await leListas(db, deviceId);
    if (!listas) return reply(500, { error: "listas_failed" });
    const vazias = [listas.missao, listas.bonus, listas.recados].every((l) => !(l as unknown[]).length);
    const estado = vazias ? (await deviceState(db, deviceId))?.estado : null;
    if (isObj(estado)) {
      await espelhaListas(db, deviceId, estado).catch((e) => console.warn("listas:", e));
      listas = await leListas(db, deviceId) ?? listas;
    }
    return reply(200, { ok: true, device_id: deviceId, ...listas });
  }

  return reply(403, { error: "action_not_allowed" });
}

// ---------------------------------------------------------------- o app do celular
// Fala com o Hermes como a placa (send e voz entram na fila dela e a resposta vai
// para ela), mas não busca nem confirma a fila da placa e não a marca como online.
// A cópia da resposta o app lê com "resposta", pelos message_id que o send/voz devolveu.
async function appAction(db: Db, deviceId: string, action: string, body: Obj, audio: Uint8Array | null, formato: string, req: Request) {
  if (action === "resposta") {
    const ids = Array.isArray(body.message_ids) ? body.message_ids.filter(isUuid).slice(0, 20) : [];
    if (!ids.length) return reply(400, { error: "invalid_message_ids" });
    const [pedidos, respostas] = await Promise.all([
      db.from("lylu_bridge_messages").select("message_id, status")
        .eq("device_id", deviceId).eq("direction", "device_to_agent").in("message_id", ids),
      db.from("lylu_bridge_messages").select("request_id, payload")
        .eq("device_id", deviceId).eq("direction", "agent_to_device").in("request_id", ids),
    ]);
    if (pedidos.error || respostas.error) return reply(500, { error: "read_failed" });
    const porPedido = new Map((respostas.data || []).map((r) => [r.request_id, r.payload]));
    return reply(200, {
      ok: true,
      respostas: (pedidos.data || []).map((p) => ({
        message_id: p.message_id, status: p.status, resposta: porPedido.get(p.message_id) ?? null,
      })),
    });
  }
  // Só conversa: aviso de evento é coisa da placa.
  if (action === "send" && (body.kind ?? "chat") !== "chat") return reply(403, { error: "action_not_allowed" });
  if (action !== "send" && action !== "voz" && action !== "audio_get") return reply(403, { error: "action_not_allowed" });
  return await deviceAction(db, deviceId, action, body, audio, formato, req, "app");
}

// ---------------------------------------------------------------- entrada
Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") return new Response(null, { status: 204, headers: CORS });
  if (req.method === "GET") {
    return reply(200, {
      ok: true, service: "lylu-bridge", version: 4, stores_audio: "temporario", audio_ttl_min: 30,
      audio_formatos: Object.keys(FORMATOS), app: true,
    });
  }
  if (req.method !== "POST") return reply(405, { error: "method_not_allowed" });

  const token = req.headers.get("x-lylu-token") || "";
  if (token.length < 20 || token.length > 256) return reply(401, { error: "invalid_token" });

  // Corpo binário (áudio) quando vem x-lylu-action; JSON nos outros casos.
  const binaryAction = req.headers.get("x-lylu-action") || "";
  const declared = Number(req.headers.get("content-length") || "0");
  const formato = req.headers.get("x-lylu-formato") || FORMATO_PADRAO;
  let body: Obj = {};
  let audio: Uint8Array | null = null;
  if (binaryAction) {
    if (!(formato in FORMATOS)) return reply(400, { error: "invalid_audio_format" });
    if (declared > MAX_AUDIO) return reply(413, { error: "audio_too_large" });
    audio = new Uint8Array(await req.arrayBuffer());
    if (audio.length > MAX_AUDIO) return reply(413, { error: "audio_too_large" });
    // Menos de 0,2 s é toque sem querer; PCM de 16 bits tem sempre tamanho par.
    if (audio.length < FORMATOS[formato] / 5 || (formato === FORMATO_PADRAO && audio.length % 2)) {
      return reply(400, { error: "invalid_audio" });
    }
  } else {
    if (declared > MAX_BODY) return reply(413, { error: "body_too_large" });
    try {
      const raw = await req.text();
      if (raw.length > MAX_BODY) return reply(413, { error: "body_too_large" });
      const parsed = JSON.parse(raw || "{}");
      if (!isObj(parsed)) return reply(400, { error: "invalid_json" });
      body = parsed;
    } catch {
      return reply(400, { error: "invalid_json" });
    }
  }

  const url = Deno.env.get("SUPABASE_URL");
  const serviceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
  if (!url || !serviceKey) return reply(503, { error: "service_not_configured" });
  const db = createClient(url, serviceKey, { auth: { persistSession: false } });

  const tokenHash = await sha256Hex(token);
  const { data: credential, error: authError } = await db
    .from("lylu_bridge_credentials")
    .select("credential_id, role, device_id")
    .eq("token_sha256", tokenHash)
    .eq("enabled", true)
    .maybeSingle();
  if (authError || !credential) return reply(401, { error: "invalid_token" });

  await db.from("lylu_bridge_credentials")
    .update({ last_seen_at: new Date().toISOString() })
    .eq("credential_id", credential.credential_id);

  const action = binaryAction || (typeof body.action === "string" ? body.action : "");

  if (credential.role === "device") {
    const deviceId = credential.device_id as string;
    await db.from("lylu_devices").update({ last_seen_at: new Date().toISOString() }).eq("device_id", deviceId);
    if (binaryAction && action !== "voz") return reply(403, { error: "action_not_allowed" });
    return await deviceAction(db, deviceId, action, body, audio, formato, req);
  }

  if (credential.role === "app") {
    if (binaryAction && action !== "voz") return reply(403, { error: "action_not_allowed" });
    return await appAction(db, credential.device_id as string, action, body, audio, formato, req);
  }

  if (credential.role === "relay") {
    if (binaryAction && action !== "audio_up") return reply(403, { error: "action_not_allowed" });
    return await relayAction(db, action, body, audio, req);
  }

  return reply(403, { error: "role_not_allowed" });
});
