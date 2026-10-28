// Cliente mínimo e revogável da ponte Supabase. A placa nunca recebe chaves do
// Hermes, Todoist ou service_role: somente seu token estreito de dispositivo.
//
// v2: além do texto, manda a fala gravada (PCM cru), baixa a voz da resposta,
// recebe o que o Hermes manda sem ela ter perguntado (estado e comandos) e conta
// para ele o que está na tela (ponte_relatar). Ver servidor/ no repositório.
#include "ponte.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "adpcm.h"
#include "rede.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define LYLU_BRIDGE_URL ""
#define LYLU_DEVICE_TOKEN ""
#endif

static const char *TAG = "ponte";
#define MAX_JSON (48 * 1024)          // uma busca traz até 5 mensagens de estado
#define MAX_AUDIO 1310720             // o teto da ponte: ~40 s de voz
#define MAX_TEXTO 512
#define MAX_EVENTO 4096
#define MAX_RELATORIO 16384
// A cada pergunta é uma invocação da Edge Function. De 2,5 em 2,5 s davam ~1 milhão
// por mês só da placa; o plano gratuito tem 500 mil. Parada, ela pergunta a cada
// 10 s. Esperando a resposta de uma fala, ela dá 4 s de folga (ouvir, o Hermes
// pensar e a Francisca falar nunca levam menos que isso: medido 6,8 s em 28/10/2026)
// e daí pergunta a cada 0,4 s — o mesmo tanto de perguntas de antes, mais cedo.
#define BUSCA_PARADA_MS 10000
#define PRIMEIRA_BUSCA_MS 4000
#define BUSCA_ESPERANDO_MS 400
#define BUSCA_FALHOU_MS 5000
#define ESPERA_RESPOSTA_MS 90000

typedef enum { P_TEXTO, P_EVENTO, P_VOZ, P_RELATORIO } tipo_pedido_t;
typedef struct { tipo_pedido_t tipo; char *dados; int16_t *pcm; size_t n; bool calar; } pedido_t;

static QueueHandle_t fila;
static ponte_aviso_t avisar;
static bool online;
static int64_t esperando_ate, proxima_busca;
static portMUX_TYPE trava = portMUX_INITIALIZER_UNLOCKED;
static char *relatorio;               // o último retrato, esperando a vez
static bool relatorio_na_fila;

static int64_t agora_ms(void) { return esp_timer_get_time() / 1000; }

// As últimas mensagens já entregues à tela. Se a confirmação (ack) se perder na
// rede, a ponte devolve a mesma mensagem na busca seguinte — e a Lylu falava duas
// vezes. A repetida só é confirmada de novo, sem chegar na tela.
#define LEMBRA_IDS 16
static char ja_vistas[LEMBRA_IDS][40];
static int prox_vista;

static bool ja_vi(const char *id)
{
    for (int i = 0; i < LEMBRA_IDS; i++)
        if (!strcmp(ja_vistas[i], id)) return true;
    strlcpy(ja_vistas[prox_vista], id, sizeof ja_vistas[0]);
    prox_vista = (prox_vista + 1) % LEMBRA_IDS;
    return false;
}

// ---------------------------------------------------------------- HTTP
// Uma chamada à ponte: corpo JSON, ou binário (a fala) quando vem acao_bin. A
// resposta vem inteira para a PSRAM — a voz da resposta passa de 1 MB, e na pilha
// de 12 KB da tarefa nem a de 8 KB cabia junto com o TLS ("Stack protection fault").
// Devolve o status HTTP, ou 0 se a conversa com o servidor nem completou.
//
// A conexão é uma só, aberta na primeira chamada e reaproveitada nas seguintes:
// cada aperto de mão do TLS custava ~2 s (medido em 28/10/2026), e esperando a
// resposta de uma fala a placa só conseguia perguntar a cada ~3 s. Se o servidor
// fechou a conexão parada, ou ela caiu no meio, a placa abre outra sozinha.
static const char *formato_bin;       // x-lylu-formato da próxima chamada binária (ver envia_voz)
static esp_http_client_handle_t cliente;
static bool aberta;                   // a conexão ficou de pé depois da última resposta

static esp_http_client_handle_t pega_cliente(void)
{
    if (cliente) return cliente;
    esp_http_client_config_t cfg = {
        .url = LYLU_BRIDGE_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .keep_alive_enable = true,    // o TCP percebe sozinho se o Wi-Fi caiu com ela parada
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
        .save_client_session = true,  // reconectar reaproveita a sessão: aperto de mão curto
#endif
    };
    cliente = esp_http_client_init(&cfg);
    if (cliente) esp_http_client_set_header(cliente, "x-lylu-token", LYLU_DEVICE_TOKEN);
    return cliente;
}

// A conexão parada ainda serve? Sem pergunta nenhuma no ar, não devia ter nada
// para ler: se tem, é o servidor fechando (o FIN, ou o aviso de fim do TLS).
static bool conexao_viva(void)
{
    int s = esp_http_client_get_socket(cliente);
    char c;
    return s >= 0 && recv(s, &c, 1, MSG_PEEK | MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
}

static esp_err_t uma_vez(esp_http_client_handle_t h, const void *corpo, size_t n, uint8_t **resp, size_t *resp_n,
                         size_t max, int *status)
{
    uint8_t *buf = NULL;
    size_t usados = 0;
    esp_err_t e = esp_http_client_open(h, n);
    for (size_t foi = 0; e == ESP_OK && foi < n;) {
        int w = esp_http_client_write(h, (const char *)corpo + foi, n - foi);
        if (w <= 0) e = ESP_FAIL;
        else foi += w;
    }
    int64_t tam = e == ESP_OK ? esp_http_client_fetch_headers(h) : -1;
    if (e == ESP_OK && tam == -ESP_ERR_HTTP_EAGAIN) e = ESP_ERR_TIMEOUT;
    else if (e == ESP_OK && tam < 0) e = ESP_FAIL;
    if (e == ESP_OK) {
        *status = esp_http_client_get_status_code(h);
        size_t cap = tam > 0 ? (size_t)tam : 4096;
        if (cap > max) e = ESP_ERR_NO_MEM;                 // maior do que cabe: nem tenta
        else if (!(buf = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM))) e = ESP_ERR_NO_MEM;
        while (e == ESP_OK) {
            if (tam > 0 && usados >= (size_t)tam) break;   // veio tudo o que o cabeçalho prometeu
            if (usados == cap) {                           // sem content-length: cresce aos poucos
                size_t novo = cap * 2 > max ? max : cap * 2;
                uint8_t *b = novo > cap ? heap_caps_realloc(buf, novo + 1, MALLOC_CAP_SPIRAM) : NULL;
                if (!b) { e = ESP_ERR_NO_MEM; break; }
                buf = b;
                cap = novo;
            }
            int r = esp_http_client_read(h, (char *)buf + usados, cap - usados);
            if (r < 0) e = ESP_FAIL;
            else if (r == 0) break;
            else usados += r;
        }
    }
    // Só fica aberta se a resposta veio inteira e o servidor não pediu para fechar:
    // sobra de resposta no caminho embaralharia a pergunta seguinte.
    aberta = e == ESP_OK && esp_http_client_is_complete_data_received(h) && esp_http_client_is_persistent_connection(h);
    if (!aberta) esp_http_client_close(h);
    if (e != ESP_OK) {
        free(buf);
        return e;
    }
    buf[usados] = 0;
    *resp = buf;
    *resp_n = usados;
    return ESP_OK;
}

static int chama(const char *acao_bin, const void *corpo, size_t n, uint8_t **resp, size_t *resp_n, size_t max)
{
    *resp = NULL;
    *resp_n = 0;
    esp_http_client_handle_t h = pega_cliente();
    if (!h) return 0;
    esp_http_client_set_header(h, "content-type", acao_bin ? "application/octet-stream" : "application/json");
    if (acao_bin) esp_http_client_set_header(h, "x-lylu-action", acao_bin);
    else esp_http_client_delete_header(h, "x-lylu-action");
    if (acao_bin && formato_bin) esp_http_client_set_header(h, "x-lylu-formato", formato_bin);
    else esp_http_client_delete_header(h, "x-lylu-formato");

    if (aberta && !conexao_viva()) {
        esp_http_client_close(h);
        aberta = false;
    }
    bool reaproveitou = aberta;
    int status = 0;
    esp_err_t e = uma_vez(h, corpo, n, resp, resp_n, max, &status);
    // A reaproveitada pode ter morrido entre a conferência e a pergunta. Se nem
    // resposta veio, o servidor não chegou a atender: tenta de novo numa nova.
    if (e == ESP_FAIL && reaproveitou && !status) {
        ESP_LOGI(TAG, "a conexão parada tinha caído; abrindo outra");
        e = uma_vez(h, corpo, n, resp, resp_n, max, &status);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "ponte HTTP falhou: %s, status %d", esp_err_to_name(e), status);
        return 0;
    }
    return status;
}

static cJSON *posta(cJSON *corpo, int *status)
{
    char *json = cJSON_PrintUnformatted(corpo);
    if (!json) return NULL;
    uint8_t *r;
    size_t n;
    int st = chama(NULL, json, strlen(json), &r, &n, MAX_JSON);
    free(json);
    if (status) *status = st;
    cJSON *resp = NULL;
    if (st >= 200 && st < 300) resp = cJSON_Parse((const char *)r);
    else if (st) ESP_LOGW(TAG, "ponte respondeu %d: %.160s", st, r ? (const char *)r : "");
    free(r);
    return resp;
}

// Consome r. Se deu certo e pediram, copia o message_id que a ponte devolveu.
static bool deu_certo(cJSON *r, char *id, size_t cap)
{
    bool ok = r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
    if (id) {
        cJSON *m = ok ? cJSON_GetObjectItemCaseSensitive(r, "message_id") : NULL;
        strlcpy(id, cJSON_IsString(m) ? m->valuestring : "", cap);
    }
    cJSON_Delete(r);
    return ok;
}

// ---------------------------------------------------------------- indo
static bool envia(const char *kind, cJSON *payload, char *id, size_t cap)   // consome o payload
{
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "send");
    cJSON_AddStringToObject(q, "kind", kind);
    cJSON_AddItemToObject(q, "payload", payload);
    bool ok = deu_certo(posta(q, NULL), id, cap);
    cJSON_Delete(q);
    return ok;
}

// As falas desta placa que ainda esperam resposta, pelo message_id que a ponte
// devolveu. O que a Mica pergunta pelo app do celular também tem resposta tocando
// aqui, mas essa não é "a que eu esperava": não tira a Conversa do "pensando".
// Sem id (ponte que não devolve), vale a primeira resposta que chegar.
#define MAX_PENDENTES 4
static char pendentes[MAX_PENDENTES][40];
static int n_pendentes;

static void espera_resposta(const char *id)
{
    esperando_ate = agora_ms() + ESPERA_RESPOSTA_MS;
    // Já esperando outra? Então continua perguntando ligeiro: a de antes vem primeiro.
    if (n_pendentes == 0) proxima_busca = agora_ms() + PRIMEIRA_BUSCA_MS;
    if (n_pendentes == MAX_PENDENTES) {                  // a mais velha já não vem mais
        memmove(pendentes[0], pendentes[1], sizeof pendentes[0] * (MAX_PENDENTES - 1));
        n_pendentes--;
    }
    strlcpy(pendentes[n_pendentes++], id ? id : "", sizeof pendentes[0]);
}

// Aviso em que o Hermes deve falar (a pausa do pomodoro). Perguntar uma vez só,
// 8 s depois, perdia a resposta por pouco e ela esperava a volta seguinte: chegou
// 19 s depois do fim do foco (28/10/2026). Agora, passada a folga do servidor, a
// placa pergunta ligeiro por uns segundos, até a resposta desse aviso aparecer.
// Não é "a resposta que eu esperava": não mexe na Conversa.
#define AVISO_JANELA_MS 20000
#define BUSCA_AVISO_MS 500
static char aviso_id[40];
static int64_t aviso_ate, aviso_em;
// O aviso que a placa já deu sozinha (o áudio gravado do pomodoro): se o Hermes
// responder a ele mesmo assim, a mesma frase sairia duas vezes (28/10/2026).
static char calar_id[40];

static bool era_minha(const char *request_id)
{
    for (int i = 0; i < n_pendentes; i++) {
        if (pendentes[i][0] && strcmp(pendentes[i], request_id)) continue;
        memmove(pendentes[i], pendentes[i + 1], sizeof pendentes[0] * (n_pendentes - i - 1));
        if (--n_pendentes == 0) esperando_ate = 0;
        return true;
    }
    return false;
}

// Cronômetro da conversa: quanto a placa leva para subir a fala e baixar a
// resposta, e quanto o servidor (relay + Hermes) leva no meio. É o que separa
// "a placa está lenta" de "o Hermes está lento".
static int64_t voz_subiu_em;

// A ponte avisa, em cada busca, os formatos de voz que ela entende. Enquanto não
// disser que entende ADPCM (função antiga, ou relay ainda sem o decodificador),
// a fala vai crua, como sempre foi.
static bool servidor_adpcm;

static void envia_voz(int16_t *pcm, size_t amostras)
{
    uint8_t *r, *adpcm = NULL;
    size_t n, bytes = amostras * sizeof *pcm;
    const void *corpo = pcm;
    if (servidor_adpcm && (adpcm = heap_caps_malloc(adpcm_tamanho(amostras), MALLOC_CAP_SPIRAM))) {
        bytes = adpcm_codifica(pcm, amostras, adpcm);
        corpo = adpcm;
        formato_bin = ADPCM_FORMATO;
    }
    int64_t t0 = agora_ms();
    int st = chama("voz", corpo, bytes, &r, &n, 4096);
    formato_bin = NULL;
    free(adpcm);
    char id[40] = "";
    if (st >= 200 && st < 300) deu_certo(cJSON_Parse((const char *)r), id, sizeof id);
    free(r);
    if (st >= 200 && st < 300) {
        voz_subiu_em = agora_ms();
        ESP_LOGI(TAG, "tempo: a fala (%u KB%s) subiu em %d ms", (unsigned)(bytes / 1024), adpcm ? ", ADPCM" : "",
                 (int)(voz_subiu_em - t0));
        espera_resposta(id);
        if (avisar) avisar(PONTE_VOZ_SUBIU, NULL, NULL, NULL, 0);
        return;
    }
    // A v1 da função não conhece "voz": recusa o corpo grande (413), o binário
    // (400) ou a ação (403). Aí não adianta tentar de novo até o servidor mudar.
    const char *motivo = st == 400 || st == 403 || st == 413 ? "A ponte ainda não sabe ouvir."
                       : st ? "A ponte recusou a minha gravação." : "Não consegui mandar. A internet tá boa?";
    ESP_LOGW(TAG, "a fala não subiu (status %d)", st);
    if (avisar) avisar(PONTE_VOZ_FALHOU, NULL, motivo, NULL, 0);
}

static void manda_relatorio(void)
{
    portENTER_CRITICAL(&trava);
    char *s = relatorio;
    relatorio = NULL;
    relatorio_na_fila = false;
    portEXIT_CRITICAL(&trava);
    cJSON *estado = s ? cJSON_Parse(s) : NULL;
    free(s);
    if (!estado) return;
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "report");
    cJSON_AddItemToObject(q, "estado", estado);
    cJSON_Delete(posta(q, NULL));          // na v1 isso dá 403; não faz mal
    cJSON_Delete(q);
}

static void muda_online(bool valor)
{
    if (online == valor) return;
    online = valor;
    if (avisar) avisar(PONTE_MUDOU, NULL, NULL, NULL, 0);
}

static void atende(pedido_t *pd)
{
    switch (pd->tipo) {
    case P_TEXTO: {
        cJSON *payload = cJSON_CreateObject();
        cJSON_AddStringToObject(payload, "text", pd->dados);
        char id[40];
        if (envia("chat", payload, id, sizeof id)) espera_resposta(id);
        else muda_online(false);
        break;
    }
    case P_EVENTO: {
        cJSON *payload = cJSON_Parse(pd->dados);
        if (!payload) break;
        char id[40];
        if (!envia("event", payload, id, sizeof id)) ESP_LOGW(TAG, "aviso para o Hermes perdido: %.80s", pd->dados);
        else if (pd->calar) strlcpy(calar_id, id, sizeof calar_id);
        else if (pd->n) {
            strlcpy(aviso_id, id, sizeof aviso_id);
            aviso_em = agora_ms();
            aviso_ate = aviso_em + pd->n + AVISO_JANELA_MS;
            if (agora_ms() + (int64_t)pd->n < proxima_busca) proxima_busca = agora_ms() + pd->n;
        }
        break;
    }
    case P_VOZ:
        envia_voz(pd->pcm, pd->n);
        free(pd->pcm);
        break;
    case P_RELATORIO:
        manda_relatorio();
        break;
    }
    free(pd->dados);
}

// ---------------------------------------------------------------- vindo
// A voz da resposta, sempre em PCM para o alto-falante: se veio em ADPCM
// (payload.formato), é aberta aqui. *bytes conta o que passou pela rede.
static int16_t *baixa_audio(const char *id, const char *formato, size_t *amostras, size_t *bytes)
{
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "audio_get");
    cJSON_AddStringToObject(q, "audio_id", id);
    char *json = cJSON_PrintUnformatted(q);
    cJSON_Delete(q);
    if (!json) return NULL;
    uint8_t *r;
    size_t n;
    int st = chama(NULL, json, strlen(json), &r, &n, MAX_AUDIO);
    free(json);
    *bytes = n;
    if (st != 200 || n < 2) {
        ESP_LOGW(TAG, "não baixei a voz da resposta (status %d)", st);
        free(r);
        return NULL;
    }
    if (!formato || strcmp(formato, ADPCM_FORMATO)) {
        *amostras = n / 2;
        return (int16_t *)r;
    }
    int16_t *pcm = heap_caps_malloc(adpcm_amostras(n) * sizeof *pcm, MALLOC_CAP_SPIRAM);
    *amostras = pcm ? adpcm_decodifica(r, n, pcm) : 0;
    free(r);
    return pcm;
}

static bool busca(void)
{
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "poll");
    cJSON *r = posta(q, NULL);
    cJSON_Delete(q);
    if (!r) return false;
    bool adpcm = false;
    cJSON *f;
    cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(r, "audio_formatos"))
        if (cJSON_IsString(f) && !strcmp(f->valuestring, ADPCM_FORMATO)) adpcm = true;
    if (adpcm != servidor_adpcm) ESP_LOGI(TAG, "a ponte %s voz em ADPCM", adpcm ? "aceita" : "não aceita mais");
    servidor_adpcm = adpcm;
    cJSON *ids = cJSON_CreateArray();
    cJSON *m;
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(r, "messages")) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(m, "message_id");
        cJSON *kind = cJSON_GetObjectItemCaseSensitive(m, "kind");
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(m, "payload");
        if (!cJSON_IsString(id)) continue;
        // Confirma tudo, até o que não entende: o que fica sem confirmar volta em
        // toda busca e, cinco assim, tapam a fila inteira.
        cJSON_AddItemToArray(ids, cJSON_CreateString(id->valuestring));
        if (!cJSON_IsObject(payload) || ja_vi(id->valuestring)) continue;
        const char *k = cJSON_IsString(kind) ? kind->valuestring : "chat";
        cJSON *rid = cJSON_GetObjectItemCaseSensitive(m, "request_id");
        if (cJSON_IsString(rid) && calar_id[0] && !strcmp(rid->valuestring, calar_id)) {
            ESP_LOGI(TAG, "o Hermes repetiu um aviso que a placa já deu: fica calado");
            calar_id[0] = 0;
            continue;                                                   // já está nos ids do ack
        }
        bool minha = cJSON_IsString(rid) && era_minha(rid->valuestring);   // a resposta a uma fala daqui
        if (cJSON_IsString(rid) && aviso_id[0] && !strcmp(rid->valuestring, aviso_id)) {
            ESP_LOGI(TAG, "tempo: a fala do Hermes sobre o aviso apareceu %d ms depois dele", (int)(agora_ms() - aviso_em));
            aviso_ate = 0;
            aviso_id[0] = 0;
        }
        if (minha && voz_subiu_em) {
            ESP_LOGI(TAG, "tempo: a resposta apareceu %d ms depois da fala subir (relay + Hermes)",
                     (int)(agora_ms() - voz_subiu_em));
            voz_subiu_em = 0;
        }
        int16_t *audio = NULL;
        size_t n = 0;
        cJSON *aid = cJSON_GetObjectItemCaseSensitive(payload, "audio_id");
        if (cJSON_IsString(aid)) {
            cJSON *fmt = cJSON_GetObjectItemCaseSensitive(payload, "formato");
            size_t bytes = 0;
            int64_t t0 = agora_ms();
            audio = baixa_audio(aid->valuestring, cJSON_IsString(fmt) ? fmt->valuestring : NULL, &n, &bytes);
            ESP_LOGI(TAG, "tempo: baixei a voz (%u KB, %u ms de fala) em %d ms", (unsigned)(bytes / 1024),
                     (unsigned)(n * 1000 / 16000), (int)(agora_ms() - t0));
        }
        char *texto = cJSON_PrintUnformatted(payload);
        if (texto && avisar) avisar(minha ? PONTE_RESPOSTA : PONTE_MENSAGEM, k, texto, audio, n);
        else free(audio);
        free(texto);
    }
    cJSON_Delete(r);
    if (!cJSON_GetArraySize(ids)) { cJSON_Delete(ids); return true; }
    q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "ack");
    cJSON_AddItemToObject(q, "message_ids", ids);
    cJSON_Delete(posta(q, NULL));
    cJSON_Delete(q);
    return true;
}

// ---------------------------------------------------------------- a tarefa
static void tarefa(void *p)
{
    for (;;) {
        if (rede_estado() != REDE_CONECTADA) {
            muda_online(false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        // Espera a hora da próxima busca, mas acorda na hora se aparecer pedido
        // (uma fala gravada não fica esperando 10 s para subir).
        int64_t falta = proxima_busca - agora_ms();
        pedido_t pd;
        if (xQueueReceive(fila, &pd, pdMS_TO_TICKS(falta > 0 ? falta : 0)) == pdTRUE) {
            atende(&pd);
            continue;
        }
        bool ok = busca();
        muda_online(ok);
        if (esperando_ate <= agora_ms()) n_pendentes = 0;    // as que não vieram em 90 s não vêm mais
        proxima_busca = agora_ms() + (esperando_ate > agora_ms() ? BUSCA_ESPERANDO_MS
                                      : aviso_ate > agora_ms() ? BUSCA_AVISO_MS
                                      : ok ? BUSCA_PARADA_MS : BUSCA_FALHOU_MS);
    }
}

bool ponte_configurada(void)
{
    return LYLU_BRIDGE_URL[0] && LYLU_DEVICE_TOKEN[0] &&
           strcmp(LYLU_BRIDGE_URL, "COLE_A_URL_AQUI") &&
           strcmp(LYLU_DEVICE_TOKEN, "COLE_O_TOKEN_AQUI");
}

void ponte_iniciar(ponte_aviso_t aviso)
{
    avisar = aviso;
    if (!ponte_configurada()) {
        ESP_LOGW(TAG, "ponte não configurada: crie main/secrets.h sem colocar no Git");
        if (avisar) avisar(PONTE_MUDOU, NULL, NULL, NULL, 0);
        return;
    }
    fila = xQueueCreate(8, sizeof(pedido_t));
    // 12 KB: o aperto de mão do TLS sozinho já passa de 6 KB de pilha.
    if (!fila || xTaskCreate(tarefa, "ponte", 12288, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "não consegui iniciar a ponte");
        return;
    }
    ESP_LOGI(TAG, "ponte iniciada; nenhuma chave administrativa está na placa");
}

static bool enfileira(pedido_t *pd)
{
    return fila && xQueueSend(fila, pd, 0) == pdTRUE;
}

static bool enfileira_texto(tipo_pedido_t tipo, const char *s, size_t max, size_t n, bool calar)
{
    if (!fila || !s || !*s || strlen(s) > max) return false;
    pedido_t pd = { .tipo = tipo, .dados = strdup(s), .n = n, .calar = calar };
    if (!pd.dados) return false;
    if (enfileira(&pd)) return true;
    free(pd.dados);
    return false;
}

bool ponte_enviar_texto(const char *texto) { return enfileira_texto(P_TEXTO, texto, MAX_TEXTO, 0, false); }
bool ponte_enviar_evento(const char *payload_json, int resposta_em_ms)
{
    return enfileira_texto(P_EVENTO, payload_json, MAX_EVENTO, resposta_em_ms > 0 ? (size_t)resposta_em_ms : 0,
                           resposta_em_ms == PONTE_JA_FALEI);
}

bool ponte_enviar_voz(int16_t *pcm, size_t amostras)
{
    pedido_t pd = { .tipo = P_VOZ, .pcm = pcm, .n = amostras };
    if (pcm && amostras && enfileira(&pd)) return true;
    free(pcm);
    return false;
}

void ponte_relatar(const char *estado_json)
{
    if (!fila || !estado_json || strlen(estado_json) > MAX_RELATORIO) return;
    char *s = strdup(estado_json);
    if (!s) return;
    portENTER_CRITICAL(&trava);
    char *velho = relatorio;
    relatorio = s;
    bool ja = relatorio_na_fila;
    relatorio_na_fila = true;
    portEXIT_CRITICAL(&trava);
    free(velho);
    if (ja) return;                        // já tem um pedido na fila; ele leva o mais novo
    pedido_t pd = { .tipo = P_RELATORIO };
    if (!enfileira(&pd)) {
        portENTER_CRITICAL(&trava);
        relatorio_na_fila = false;
        portEXIT_CRITICAL(&trava);
    }
}

bool ponte_online(void) { return online; }
