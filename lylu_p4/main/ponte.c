// Cliente mínimo e revogável da ponte Supabase. A placa nunca recebe chaves do
// Hermes, Todoist ou service_role: somente seu token estreito de dispositivo.
#include "ponte.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rede.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define LYLU_BRIDGE_URL ""
#define LYLU_DEVICE_TOKEN ""
#endif

static const char *TAG = "ponte";
#define MAX_RESPOSTA 8192
#define MAX_TEXTO 512
#define INTERVALO_POLL_MS 2500
#define INTERVALO_HEARTBEAT_MS 60000

typedef struct { char texto[MAX_TEXTO + 1]; } pedido_t;
typedef struct { char dados[MAX_RESPOSTA]; size_t usados; } resposta_t;

static QueueHandle_t fila;
static ponte_aviso_t avisar;
static bool online;
static int64_t ultimo_heartbeat;

static esp_err_t recebe_http(esp_http_client_event_t *e)
{
    resposta_t *r = e->user_data;
    if (e->event_id != HTTP_EVENT_ON_DATA || !r || !e->data || e->data_len <= 0) return ESP_OK;
    size_t cabe = sizeof r->dados - 1 - r->usados;
    size_t n = (size_t)e->data_len < cabe ? (size_t)e->data_len : cabe;
    memcpy(r->dados + r->usados, e->data, n);
    r->usados += n;
    r->dados[r->usados] = 0;
    return ESP_OK;
}

static cJSON *posta(cJSON *corpo)
{
    char *json = cJSON_PrintUnformatted(corpo);
    if (!json) return NULL;
    // A resposta (8 KB) vai para o heap: na pilha da tarefa ela estourava junto
    // com o TLS ("Stack protection fault" logo no primeiro heartbeat).
    resposta_t *r = calloc(1, sizeof *r);
    if (!r) { free(json); return NULL; }
    esp_http_client_config_t cfg = {
        .url = LYLU_BRIDGE_URL,
        .event_handler = recebe_http,
        .user_data = r,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) { free(json); free(r); return NULL; }
    esp_http_client_set_method(h, HTTP_METHOD_POST);
    esp_http_client_set_header(h, "content-type", "application/json");
    esp_http_client_set_header(h, "x-lylu-token", LYLU_DEVICE_TOKEN);
    esp_http_client_set_post_field(h, json, strlen(json));
    esp_err_t e = esp_http_client_perform(h);
    int status = e == ESP_OK ? esp_http_client_get_status_code(h) : 0;
    esp_http_client_cleanup(h);
    free(json);
    if (e != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "ponte HTTP falhou: %s, status %d", esp_err_to_name(e), status);
        free(r);
        return NULL;
    }
    cJSON *resp = cJSON_Parse(r->dados);
    free(r);
    return resp;
}

static bool acao_simples(const char *acao)
{
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", acao);
    cJSON *r = posta(q);
    cJSON_Delete(q);
    bool ok = r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
    cJSON_Delete(r);
    return ok;
}

static bool envia(const char *texto)
{
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "send");
    cJSON_AddStringToObject(q, "kind", "chat");
    cJSON *payload = cJSON_AddObjectToObject(q, "payload");
    cJSON_AddStringToObject(payload, "text", texto);
    cJSON *r = posta(q);
    cJSON_Delete(q);
    bool ok = r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
    cJSON_Delete(r);
    return ok;
}

static void confirma(const char *id)
{
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "ack");
    cJSON *ids = cJSON_AddArrayToObject(q, "message_ids");
    cJSON_AddItemToArray(ids, cJSON_CreateString(id));
    cJSON *r = posta(q);
    cJSON_Delete(q);
    cJSON_Delete(r);
}

static bool busca(void)
{
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "action", "poll");
    cJSON *r = posta(q);
    cJSON_Delete(q);
    if (!r) return false;
    cJSON *mensagens = cJSON_GetObjectItemCaseSensitive(r, "messages");
    cJSON *m;
    cJSON_ArrayForEach(m, mensagens) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(m, "message_id");
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(m, "payload");
        cJSON *texto = payload ? cJSON_GetObjectItemCaseSensitive(payload, "text") : NULL;
        if (cJSON_IsString(id) && cJSON_IsString(texto)) {
            if (avisar) avisar(texto->valuestring, true);
            confirma(id->valuestring);
        }
    }
    cJSON_Delete(r);
    return true;
}

static void muda_online(bool valor)
{
    if (online == valor) return;
    online = valor;
    if (avisar) avisar(NULL, online);
}

static void tarefa(void *p)
{
    for (;;) {
        if (rede_estado() != REDE_CONECTADA) {
            muda_online(false);
            vTaskDelay(pdMS_TO_TICKS(INTERVALO_POLL_MS));
            continue;
        }

        int64_t agora = esp_timer_get_time() / 1000;
        if (!online || agora - ultimo_heartbeat >= INTERVALO_HEARTBEAT_MS) {
            bool ok = acao_simples("heartbeat");
            muda_online(ok);
            if (ok) ultimo_heartbeat = agora;
            if (!ok) { vTaskDelay(pdMS_TO_TICKS(INTERVALO_POLL_MS)); continue; }
        }

        pedido_t pd;
        while (xQueueReceive(fila, &pd, 0) == pdTRUE) {
            if (!envia(pd.texto)) { muda_online(false); break; }
        }
        if (online && !busca()) muda_online(false);
        vTaskDelay(pdMS_TO_TICKS(INTERVALO_POLL_MS));
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
        if (avisar) avisar(NULL, false);
        return;
    }
    fila = xQueueCreate(4, sizeof(pedido_t));
    // 12 KB: o aperto de mão do TLS sozinho já passa de 6 KB de pilha.
    if (!fila || xTaskCreate(tarefa, "ponte", 12288, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "não consegui iniciar a ponte");
        return;
    }
    ESP_LOGI(TAG, "ponte iniciada; nenhuma chave administrativa está na placa");
}

bool ponte_enviar_texto(const char *texto)
{
    if (!fila || !texto || !*texto || strlen(texto) > MAX_TEXTO) return false;
    pedido_t pd = { 0 };
    strlcpy(pd.texto, texto, sizeof pd.texto);
    return xQueueSend(fila, &pd, 0) == pdTRUE;
}

bool ponte_online(void) { return online; }
