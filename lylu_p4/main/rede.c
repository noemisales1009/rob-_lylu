// Wi-Fi da Lylu — ver rede.h para o porquê do ESP-Hosted.
#include "rede.h"

#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "rede";

#define NVS_CAIXA  "lylu"
#define NVS_NOME   "wifi_nome"
#define NVS_SENHA  "wifi_senha"

#define ESPERA_VOLTAR_MS   15000   // rede caiu: tenta de novo daqui a pouco
#define TENTATIVAS_SENHA   3       // erro de senha na rede salva: insiste um pouco

static rede_aviso_t avisar;
static rede_estado_t estado = REDE_SEM_RADIO;
static rede_estado_t antes = REDE_SEM_RADIO;   // guardado enquanto a varredura roda

static char alvo_nome[REDE_NOME_MAX];
static char alvo_senha[REDE_SENHA_MAX];
static char meu_ip[16];
static bool tem_salva;
static bool pedido_agora;        // a pessoa está olhando a tela esperando resposta
static bool saida_nossa;         // a desconexão que nós mesmos mandamos, trocando de rede
static int  erros_de_senha;

static rede_achada_t lista[REDE_MAX_ACHADAS];
static int quantas;
static SemaphoreHandle_t trava;

static esp_timer_handle_t relogio_retry;
static esp_timer_handle_t relogio_dhcp;

// ---------------------------------------------------------------- estado
static void muda(rede_estado_t novo)
{
    antes = novo;
    if (estado != REDE_PROCURANDO) estado = novo;
}

static void avisa(void) { if (avisar) avisar(); }

// ---------------------------------------------------------------- senha guardada
static void guarda_senha(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_CAIXA, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_NOME, alvo_nome);
    nvs_set_str(h, NVS_SENHA, alvo_senha);
    nvs_commit(h);
    nvs_close(h);
    tem_salva = true;
}

static void le_senha_guardada(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_CAIXA, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof alvo_nome;
    if (nvs_get_str(h, NVS_NOME, alvo_nome, &n) == ESP_OK && alvo_nome[0]) {
        n = sizeof alvo_senha;
        if (nvs_get_str(h, NVS_SENHA, alvo_senha, &n) != ESP_OK) alvo_senha[0] = 0;
        tem_salva = true;
    }
    nvs_close(h);
}

// ---------------------------------------------------------------- hora certa
// O primeiro presente de ter internet: o relógio para de mostrar a hora em que o
// programa foi compilado. O fuso já foi ajustado em acerta_hora_inicial().
static void liga_a_hora_da_internet(void)
{
    static bool feito = false;
    if (feito) return;
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    feito = esp_netif_sntp_init(&c) == ESP_OK;
}

// ---------------------------------------------------------------- o IP que não vem
// Associar na rede e receber um IP são duas coisas: a primeira é o rádio, a
// segunda é o DHCP conversando com o roteador. Ela entra na rede e o IP não
// chega, então aqui a gente olha de perto em vez de adivinhar.
static esp_netif_t *minha_interface(void) { return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"); }

static void conta_o_dhcp(const char *quando)
{
    esp_netif_t *nif = minha_interface();
    if (!nif) { ESP_LOGW(TAG, "%s: interface WIFI_STA_DEF não existe", quando); return; }
    esp_netif_dhcp_status_t st = ESP_NETIF_DHCP_INIT;
    esp_netif_dhcpc_get_status(nif, &st);
    esp_netif_ip_info_t ip = { 0 };
    esp_netif_get_ip_info(nif, &ip);
    // 0 = nem começou, 1 = rodando, 2 = parado
    ESP_LOGI(TAG, "%s: dhcp=%d ip=" IPSTR, quando, (int)st, IP2STR(&ip.ip));
}

// Se passaram segundos e nada, cutuca o DHCP na mão. O retorno já diz o estado:
// ALREADY_STARTED = ele está rodando e é o roteador que não responde.
static void cutuca_o_dhcp(void *p)
{
    if (meu_ip[0]) return;
    conta_o_dhcp("passados 6s");
    esp_netif_t *nif = minha_interface();
    if (nif) ESP_LOGI(TAG, "dhcpc_start -> %s", esp_err_to_name(esp_netif_dhcpc_start(nif)));
}

// ---------------------------------------------------------------- tentar de novo
static void tenta_de_novo(void *p) { esp_wifi_connect(); }

static void agenda_tentativa(int ms)
{
    if (!relogio_retry) {
        esp_timer_create_args_t a = { .callback = tenta_de_novo, .name = "rede" };
        if (esp_timer_create(&a, &relogio_retry) != ESP_OK) return;
    }
    esp_timer_stop(relogio_retry);
    esp_timer_start_once(relogio_retry, (uint64_t)ms * 1000);
}

// ---------------------------------------------------------------- varredura
static uint8_t barras(int8_t rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -66) return 3;
    if (rssi >= -76) return 2;
    return 1;
}

static void guarda_varredura(void)
{
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 40) n = 40;
    wifi_ap_record_t *ap = n ? malloc(n * sizeof *ap) : NULL;
    if (!ap) { esp_wifi_clear_ap_list(); n = 0; }
    else if (esp_wifi_scan_get_ap_records(&n, ap) != ESP_OK) n = 0;

    // Da mais forte para a mais fraca, para que a repetida (a mesma rede em 2,4
    // e em 5 GHz) já caia depois da boa e possa ser simplesmente ignorada.
    for (int i = 1; i < n; i++) {
        wifi_ap_record_t x = ap[i];
        int j = i;
        for (; j > 0 && ap[j - 1].rssi < x.rssi; j--) ap[j] = ap[j - 1];
        ap[j] = x;
    }

    xSemaphoreTake(trava, portMAX_DELAY);
    quantas = 0;
    for (int i = 0; i < n && quantas < REDE_MAX_ACHADAS; i++) {
        const char *nome = (const char *)ap[i].ssid;
        if (!nome[0]) continue;                             // rede escondida
        bool repetida = false;
        for (int j = 0; j < quantas && !repetida; j++) repetida = !strcmp(lista[j].nome, nome);
        if (repetida) continue;
        strlcpy(lista[quantas].nome, nome, REDE_NOME_MAX);
        lista[quantas].barras = barras(ap[i].rssi);
        lista[quantas].com_senha = ap[i].authmode != WIFI_AUTH_OPEN;
        quantas++;
    }
    xSemaphoreGive(trava);
    free(ap);
    ESP_LOGI(TAG, "%d redes por perto", quantas);
}

// ---------------------------------------------------------------- eventos
static bool foi_a_senha(uint8_t motivo)
{
    return motivo == WIFI_REASON_AUTH_FAIL ||
           motivo == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           motivo == WIFI_REASON_HANDSHAKE_TIMEOUT ||
           motivo == WIFI_REASON_AUTH_EXPIRE;
}

static void ev_wifi(void *arg, esp_event_base_t base, int32_t id, void *dados)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = dados;
        snprintf(meu_ip, sizeof meu_ip, IPSTR, IP2STR(&e->ip_info.ip));
        // O C6 guarda por conta própria a última rede que usou e entra nela sozinho,
        // antes de a gente pedir qualquer coisa. Então o nome vem dele, não de nós —
        // e aí não existe senha nossa pra guardar (guardaríamos um nome vazio).
        wifi_ap_record_t ap;
        if (!pedido_agora && !tem_salva && esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0])
            strlcpy(alvo_nome, (const char *)ap.ssid, sizeof alvo_nome);
        erros_de_senha = 0;
        if (pedido_agora || tem_salva) guarda_senha();
        pedido_agora = false;
        muda(REDE_CONECTADA);
        liga_a_hora_da_internet();
        ESP_LOGI(TAG, "conectada em \"%s\", IP %s", alvo_nome, meu_ip);
        avisa();
        return;
    }
    if (base != WIFI_EVENT) return;

    switch (id) {
    case WIFI_EVENT_STA_START:
        if (tem_salva) { muda(REDE_CONECTANDO); esp_wifi_connect(); }
        else muda(REDE_PARADA);
        avisa();
        break;

    case WIFI_EVENT_SCAN_DONE:
        guarda_varredura();
        estado = antes;
        avisa();
        break;

    case WIFI_EVENT_STA_CONNECTED:
        saida_nossa = false;        // entrou: a saída anterior já é história
        ESP_LOGI(TAG, "associada em \"%s\" — agora falta o IP", alvo_nome);
        conta_o_dhcp("ao associar");
        if (!relogio_dhcp) {
            esp_timer_create_args_t a = { .callback = cutuca_o_dhcp, .name = "dhcp" };
            esp_timer_create(&a, &relogio_dhcp);
        }
        if (relogio_dhcp) { esp_timer_stop(relogio_dhcp); esp_timer_start_once(relogio_dhcp, 6000000); }
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *e = dados;
        meu_ip[0] = 0;
        // Sair da rede antiga para entrar na nova gera uma desconexão que é NOSSA.
        // Sem tratar isso, o tratador lia como "a conexão caiu" e agendava outra
        // tentativa, que gerava outra desconexão: seis connect em 2 segundos.
        //
        // Mas a marca sozinha não serve: se ela NÃO estava conectada, o disconnect
        // não gera evento algum e a marca fica ligada — engolindo depois a falha de
        // verdade, e aí a tela fica "tentando entrar" pra sempre. Por isso o motivo
        // também precisa bater: ASSOC_LEAVE é a saída que parte de nós.
        if (saida_nossa && e->reason == WIFI_REASON_ASSOC_LEAVE) { saida_nossa = false; break; }
        saida_nossa = false;
        if (!tem_salva && !pedido_agora) { muda(REDE_PARADA); avisa(); break; }

        if (foi_a_senha(e->reason)) {
            // Quem acabou de digitar está esperando resposta: falar na hora.
            // A rede já salva pode ter só tropeçado — vale insistir um pouco.
            if (pedido_agora || ++erros_de_senha >= TENTATIVAS_SENHA) {
                pedido_agora = false;
                muda(REDE_SENHA_ERRADA);
                ESP_LOGW(TAG, "senha não bateu em \"%s\" (motivo %d)", alvo_nome, e->reason);
            } else {
                muda(REDE_CONECTANDO);
                agenda_tentativa(2000);
            }
        } else if (e->reason == WIFI_REASON_NO_AP_FOUND) {
            muda(REDE_SUMIU);
            agenda_tentativa(ESPERA_VOLTAR_MS);   // o roteador pode voltar sozinho
        } else {
            muda(REDE_CONECTANDO);
            agenda_tentativa(pedido_agora ? 1000 : 5000);
        }
        avisa();
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- a fila de pedidos
// Toda chamada esp_wifi_* atravessa o SDIO até o C6 e fica esperando a resposta
// dele. Feita na thread da tela, a tela congela junto — e o aviso de versão do
// C6 fala justamente em RPC timeouts. Então o toque só deixa um pedido na fila e
// quem conversa com o rádio é uma tarefa só nossa.
typedef enum { PEDE_PROCURAR, PEDE_CONECTAR, PEDE_ESQUECER } pedido_tipo_t;
typedef struct {
    pedido_tipo_t tipo;
    char nome[REDE_NOME_MAX];
    char senha[REDE_SENHA_MAX];
} pedido_t;

static QueueHandle_t fila;

static void faz_procurar(void)
{
    wifi_scan_config_t c = { .show_hidden = false };
    if (esp_wifi_scan_start(&c, false) == ESP_OK) return;
    estado = antes;                  // nem começou: volta pro que era
    avisa();
}

static void faz_conectar(const char *nome, const char *senha)
{
    esp_wifi_scan_stop();
    strlcpy(alvo_nome, nome, sizeof alvo_nome);
    strlcpy(alvo_senha, senha, sizeof alvo_senha);
    pedido_agora = true;
    erros_de_senha = 0;

    wifi_config_t c = { 0 };
    memcpy(c.sta.ssid, alvo_nome, strnlen(alvo_nome, sizeof c.sta.ssid));
    strlcpy((char *)c.sta.password, alvo_senha, sizeof c.sta.password);
    c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

    saida_nossa = true;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &c);
    esp_wifi_connect();
}

static void faz_esquecer(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_CAIXA, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_NOME);
        nvs_erase_key(h, NVS_SENHA);
        nvs_commit(h);
        nvs_close(h);
    }
    tem_salva = false;
    pedido_agora = false;
    alvo_nome[0] = alvo_senha[0] = meu_ip[0] = 0;
    if (relogio_retry) esp_timer_stop(relogio_retry);
    if (estado != REDE_SEM_RADIO) {
        saida_nossa = true;
        esp_wifi_disconnect();
        muda(REDE_PARADA);
    }
    avisa();
}

static void tarefa_da_rede(void *p)
{
    for (;;) {
        pedido_t pd;
        if (xQueueReceive(fila, &pd, portMAX_DELAY) != pdTRUE) continue;
        switch (pd.tipo) {
        case PEDE_PROCURAR: faz_procurar(); break;
        case PEDE_CONECTAR: faz_conectar(pd.nome, pd.senha); break;
        case PEDE_ESQUECER: faz_esquecer(); break;
        }
    }
}

static void manda(pedido_t *pd) { if (fila) xQueueSend(fila, pd, 0); }

// ---------------------------------------------------------------- começo
static bool liga_o_radio(void)
{
    if (esp_netif_init() != ESP_OK) return false;
    if (esp_event_loop_create_default() != ESP_OK) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "o C6 não respondeu (%s) — a Lylu segue sem internet", esp_err_to_name(e));
        return false;
    }
    // A senha mora na NVS sob o nosso controle, não na cópia interna do driver.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ev_wifi, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ev_wifi, NULL, NULL);

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (tem_salva) {
        wifi_config_t c = { 0 };
        memcpy(c.sta.ssid, alvo_nome, strnlen(alvo_nome, sizeof c.sta.ssid));
        strlcpy((char *)c.sta.password, alvo_senha, sizeof c.sta.password);
        esp_wifi_set_config(WIFI_IF_STA, &c);
    }
    return esp_wifi_start() == ESP_OK;
}

void rede_iniciar(rede_aviso_t aviso)
{
    avisar = aviso;
    trava = xSemaphoreCreateMutex();
    fila = xQueueCreate(4, sizeof(pedido_t));
    xTaskCreate(tarefa_da_rede, "rede", 4096, NULL, 5, NULL);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e == ESP_OK) le_senha_guardada();

    if (liga_o_radio()) {
        muda(tem_salva ? REDE_CONECTANDO : REDE_PARADA);
    } else {
        estado = antes = REDE_SEM_RADIO;
    }
    avisa();
}

// ---------------------------------------------------------------- comandos da tela
// Só encostam na fila: o estado muda aqui mesmo para a tela responder na hora,
// e a conversa lenta com o rádio acontece depois, na tarefa.
void rede_procurar(void)
{
    if (estado == REDE_SEM_RADIO || estado == REDE_PROCURANDO) return;
    antes = estado;
    estado = REDE_PROCURANDO;
    pedido_t pd = { .tipo = PEDE_PROCURAR };
    manda(&pd);
    avisa();
}

void rede_conectar(const char *nome, const char *senha)
{
    if (estado == REDE_SEM_RADIO) return;
    pedido_t pd = { .tipo = PEDE_CONECTAR };
    strlcpy(pd.nome, nome, sizeof pd.nome);
    strlcpy(pd.senha, senha ? senha : "", sizeof pd.senha);
    strlcpy(alvo_nome, pd.nome, sizeof alvo_nome);   // a tela já mostra o nome certo
    estado = antes = REDE_CONECTANDO;                // a varredura, se houver, para junto
    manda(&pd);
    avisa();
}

void rede_esquecer(void)
{
    pedido_t pd = { .tipo = PEDE_ESQUECER };
    manda(&pd);
}

// ---------------------------------------------------------------- consultas
rede_estado_t rede_estado(void) { return estado; }
const char *rede_nome(void) { return alvo_nome; }
const char *rede_ip(void) { return meu_ip; }
bool rede_tem_salva(void) { return tem_salva; }

// As barras vêm da última varredura, não de uma pergunta ao C6: cada
// esp_wifi_sta_get_ap_info atravessa o SDIO até um ESP-Hosted 2.3.0 que já
// avisa de RPC timeouts, e o sinal não precisa de tanta precisão.
int rede_sinal(void)
{
    if (estado != REDE_CONECTADA || !trava) return 0;
    int b = 0;
    xSemaphoreTake(trava, portMAX_DELAY);
    for (int i = 0; i < quantas && !b; i++)
        if (!strcmp(lista[i].nome, alvo_nome)) b = lista[i].barras;
    xSemaphoreGive(trava);
    return b;
}

int rede_copia_lista(rede_achada_t *destino, int maximo)
{
    if (!trava) return 0;
    xSemaphoreTake(trava, portMAX_DELAY);
    int n = quantas < maximo ? quantas : maximo;
    memcpy(destino, lista, n * sizeof *destino);
    xSemaphoreGive(trava);
    return n;
}
