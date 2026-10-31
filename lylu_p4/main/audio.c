// Ouvidos da Lylu — ver audio.h.
#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "placa.h"

static const char *TAG = "audio";

// Os pinos vêm do BSP da ESP32-P4-Function-EV-Board, a placa de referência da
// Espressif. Esta placa é uma cópia daquele projeto: a demonstração de fábrica
// dela foi compilada com esse mesmo BSP (dá pra ler "esp32_p4_function_ev_board.c"
// e "bsp_audio_codec_microphone_init" dentro do binário guardado em
// output/backup-p4-fabrica), e os pinos do SDIO já tinham batido exatamente.
// O I2C é o mesmo do toque, e foi lá que o ES8311 respondeu.
#define PINO_MCLK  GPIO_NUM_13
#define PINO_BCLK  GPIO_NUM_12
#define PINO_WS    GPIO_NUM_10
#define PINO_DOUT  GPIO_NUM_9    // a Lylu falando
#define PINO_DIN   GPIO_NUM_48   // a Lylu escutando
#define PINO_PA    GPIO_NUM_11   // liga o amplificador do alto-falante

// ATENÇÃO: estes dois últimos NÃO seguem a placa de referência, e foi aí que eu
// me perdi. Na EV board o dado do microfone entra pelo GPIO11; aqui o 11 é o
// liga/desliga do amplificador, e o microfone entra pelo 48. Lendo o 11 como
// dado, a amostra vinha zero perfeito — não era ruído baixo, era pino errado.

#define TAXA       16000
#define QUADRO     320           // 20 ms de som por leitura

static int64_t agora_ms(void) { return esp_timer_get_time() / 1000; }

static esp_codec_dev_handle_t ouvido;   // entrada e saída: o mesmo ES8311 faz as duas
static int nivel;
static bool ligado;

// A gravação da fala: a tarefa que escuta copia cada quadro para cá enquanto
// "gravando" estiver ligado. A trava protege a troca do buffer entre ela e a tela.
// O limiar é um chute de partida (fica entre o "ouvi alguma coisa" do log, 2000,
// e o silêncio): o log de cada gravação mostra o pico, para acertar com a voz real.
#define LIMIAR_FALA 1200
static int grav_pico;
static portMUX_TYPE grav_trava = portMUX_INITIALIZER_UNLOCKED;
static int16_t *grav;
static size_t grav_n, grav_max;
static volatile bool gravando, ouviu;
static volatile int64_t grav_inicio, ultima_fala;

// ---------------------------------------------------------------- o barramento de som
static bool monta_i2s(i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    i2s_chan_config_t c = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    c.auto_clear = true;
    if (i2s_new_channel(&c, tx, rx) != ESP_OK) return false;

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(TAXA),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PINO_MCLK, .bclk = PINO_BCLK, .ws = PINO_WS,
            .dout = PINO_DOUT, .din = PINO_DIN,
        },
    };
    if (i2s_channel_init_std_mode(*tx, &std) != ESP_OK) return false;
    if (i2s_channel_init_std_mode(*rx, &std) != ESP_OK) return false;
    return i2s_channel_enable(*tx) == ESP_OK && i2s_channel_enable(*rx) == ESP_OK;
}

// ---------------------------------------------------------------- escutando
static void escuta(void *p)
{
    const int amostras = QUADRO;                        // mono: um canal só
    int16_t *buf = malloc(amostras * sizeof *buf);
    if (!buf) { vTaskDelete(NULL); return; }

    int64_t ultimo_aviso = 0;
    for (;;) {
        if (esp_codec_dev_read(ouvido, buf, amostras * sizeof *buf) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t soma = 0;
        for (int i = 0; i < amostras; i++) soma += (int32_t)buf[i] * buf[i];
        int rms = (int)sqrt((double)soma / amostras);

        if (gravando) {
            portENTER_CRITICAL(&grav_trava);
            if (gravando && grav) {
                size_t cabe = grav_max - grav_n, n = (size_t)amostras < cabe ? (size_t)amostras : cabe;
                memcpy(grav + grav_n, buf, n * sizeof *buf);
                grav_n += n;
                if (grav_n >= grav_max) gravando = false;      // chegou no teto: para sozinha
            }
            portEXIT_CRITICAL(&grav_trava);
            if (rms > grav_pico) grav_pico = rms;
            // Os primeiros 300 ms não contam como fala: é o toque do dedo no "Falar".
            if (rms > LIMIAR_FALA && agora_ms() - grav_inicio > 300) { ouviu = true; ultima_fala = agora_ms(); }
        }

        // 8000 de RMS já é voz alta a um palmo. O que passar disso satura em 100.
        int agora = rms / 80;
        if (agora > 100) agora = 100;
        // Sobe na hora, desce devagar: a barra acompanha a voz em vez de piscar.
        nivel = agora > nivel ? agora : (nivel * 7 + agora * 3) / 10;

        // Um aviso só quando alguém fala de verdade, e no máximo uma vez por
        // segundo: serve para saber que ela está escutando sem encher o log.
        if (agora > 25 && agora_ms() - ultimo_aviso > 1000) {
            ultimo_aviso = agora_ms();
            ESP_LOGI(TAG, "ouvi alguma coisa (nivel %d)", agora);
        }
    }
}

// ---------------------------------------------------------------- falando
// Um "blip" de três notas (dó, mi, sol), com as pontas suavizadas: começar ou
// parar uma onda no meio dá um estalo no alto-falante.
#define VOLUME 60

static void nota(int16_t *buf, int n, float freq)
{
    const int rampa = TAXA / 100;                       // 10 ms de subida e descida
    for (int i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < rampa) env = (float)i / rampa;
        else if (i > n - rampa) env = (float)(n - i) / rampa;
        buf[i] = (int16_t)(9000.0f * env * sinf(2.0f * (float)M_PI * freq * i / TAXA));
    }
}

// Tudo o que sai no alto-falante passa por esta fila, um de cada vez: o blip
// (pcm NULL) e a voz que veio do Hermes. A voz vai em pedaços de 50 ms para dar
// pra cortar no meio (audio_calar) sem esperar a frase inteira acabar.
typedef struct { int16_t *pcm; size_t n; } som_t;
static QueueHandle_t fila_som;
static volatile bool calar, falando;
static volatile int abertura_boca;
#define PEDACO (TAXA / 20)

static void blip(int16_t *buf, int n, int pausa)
{
    static const float NOTAS[3] = { 523.25f, 659.25f, 783.99f };
    for (int k = 0; k < 3; k++) {
        nota(buf, n, NOTAS[k]);
        memset(buf + n, 0, pausa * sizeof *buf);
        esp_codec_dev_write(ouvido, buf, (n + pausa) * sizeof *buf);
    }
}

static void fala(void *p)
{
    const int n = TAXA * 120 / 1000, pausa = TAXA * 30 / 1000;    // 120 ms cada, 30 ms entre
    int16_t *buf = calloc(n + pausa, sizeof *buf);
    if (!buf) { vTaskDelete(NULL); return; }
    for (;;) {
        som_t s;
        if (xQueueReceive(fila_som, &s, portMAX_DELAY) != pdTRUE) continue;
        if (!s.pcm) {
            abertura_boca = 0;
            blip(buf, n, pausa);
            // A voz que acaba com um blip na fila deixa "falando" ligado: desliga aqui.
            falando = uxQueueMessagesWaiting(fila_som) > 0;
            continue;
        }
        calar = false;
        falando = true;
        for (size_t i = 0; i < s.n && !calar; i += PEDACO) {
            size_t k = s.n - i < PEDACO ? s.n - i : PEDACO;
            // Usa a voz de SAÍDA em janelas de 50 ms, inclusive o silêncio
            // entre palavras. A UI lê apenas este nível, sem acessar o PCM.
            int64_t energia = 0;
            for (size_t j = 0; j < k; j++) energia += (int32_t)s.pcm[i + j] * s.pcm[i + j];
            int rms = (int)sqrt((double)energia / k);
            abertura_boca = rms < 160 ? 0 : rms < 800 ? 1 : rms < 1800 ? 2 : 3;
            esp_codec_dev_write(ouvido, s.pcm + i, k * sizeof *s.pcm);
        }
        abertura_boca = 0;
        free(s.pcm);
        falando = uxQueueMessagesWaiting(fila_som) > 0;
    }
}

void audio_blip(void)
{
    som_t s = { 0 };
    if (fila_som) xQueueSend(fila_som, &s, 0);
}

void audio_tocar(int16_t *pcm, size_t amostras)
{
    som_t s = { pcm, amostras };
    if (!pcm) return;
    if (!fila_som || !amostras || xQueueSend(fila_som, &s, 0) != pdTRUE) { free(pcm); return; }
    falando = true;
}

void audio_calar(void)
{
    som_t s;
    while (fila_som && xQueueReceive(fila_som, &s, 0) == pdTRUE) free(s.pcm);
    calar = true;
    abertura_boca = 0;
}

bool audio_falando(void) { return falando; }
int audio_boca(void) { return abertura_boca; }

// ---------------------------------------------------------------- gravando
bool audio_gravar(int max_ms)
{
    if (!ligado || max_ms <= 0) return false;
    size_t max = (size_t)TAXA * max_ms / 1000;
    int16_t *novo = heap_caps_malloc(max * sizeof *novo, MALLOC_CAP_SPIRAM);
    if (!novo) return false;
    portENTER_CRITICAL(&grav_trava);
    int16_t *velho = grav;
    grav = novo;
    grav_n = 0;
    grav_max = max;
    ouviu = false;
    grav_pico = 0;
    grav_inicio = ultima_fala = agora_ms();
    gravando = true;
    portEXIT_CRITICAL(&grav_trava);
    free(velho);
    return true;
}

void audio_parar_gravacao(void)
{
    if (!gravando) return;
    gravando = false;
    ESP_LOGI(TAG, "gravei %d ms, pico de RMS %d (limiar %d)", audio_gravado_ms(), grav_pico, LIMIAR_FALA);
}

bool audio_gravando(void) { return gravando; }
int audio_gravado_ms(void) { return (int)(grav_n * 1000 / TAXA); }
bool audio_ouviu_fala(void) { return ouviu; }
int audio_silencio_ms(void) { return (int)(agora_ms() - ultima_fala); }

int16_t *audio_pega_gravacao(size_t *amostras)
{
    portENTER_CRITICAL(&grav_trava);
    gravando = false;
    int16_t *b = grav;
    *amostras = grav_n;
    grav = NULL;
    grav_n = grav_max = 0;
    portEXIT_CRITICAL(&grav_trava);
    return b;
}

// ---------------------------------------------------------------- começo
void audio_iniciar(void)
{
    i2s_chan_handle_t tx = NULL, rx = NULL;
    if (!monta_i2s(&tx, &rx)) { ESP_LOGE(TAG, "o I2S não subiu — sem som"); return; }

    audio_codec_i2s_cfg_t cfg_i2s = { .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx };
    const audio_codec_data_if_t *dados = audio_codec_new_i2s_data(&cfg_i2s);

    // O endereço aqui é o de 8 bits (0x30); no barramento ele aparece como 0x18.
    audio_codec_i2c_cfg_t cfg_i2c = {
        .port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = placa_i2c(),
    };
    const audio_codec_ctrl_if_t *controle = audio_codec_new_i2c_ctrl(&cfg_i2c);
    if (!dados || !controle) { ESP_LOGE(TAG, "não consegui falar com o ES8311"); return; }

    es8311_codec_cfg_t cfg_es = {
        .ctrl_if = controle,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = PINO_PA,
        .use_mclk = true,
        // Tensões do hardware, como no BSP de fábrica. Sem elas o driver calcula
        // o ganho de saída por cima de zero.
        .hw_gain = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&cfg_es);
    if (!codec) { ESP_LOGE(TAG, "o ES8311 não aceitou a configuração"); return; }

    esp_codec_dev_cfg_t cfg_dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec, .data_if = dados,
    };
    ouvido = esp_codec_dev_new(&cfg_dev);
    if (!ouvido) { ESP_LOGE(TAG, "não montei o dispositivo de áudio"); return; }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .sample_rate = TAXA,
    };
    int e = esp_codec_dev_open(ouvido, &fs);
    if (e != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "esp_codec_dev_open falhou (%d)", e); return; }
    esp_codec_dev_set_in_gain(ouvido, 30.0);
    esp_codec_dev_set_out_vol(ouvido, VOLUME);

    ligado = true;
    fila_som = xQueueCreate(4, sizeof(som_t));
    xTaskCreate(escuta, "escuta", 4096, NULL, 4, NULL);
    xTaskCreate(fala, "fala", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "microfone de pé — fala alguma coisa");
    audio_blip();                // o alto-falante (no conector da borda, abaixo do cartão SD) dá oi
}

bool audio_escutando(void) { return ligado; }
int audio_nivel(void) { return ligado ? nivel : 0; }
