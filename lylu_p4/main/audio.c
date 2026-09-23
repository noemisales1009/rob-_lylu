// Ouvidos da Lylu — ver audio.h.
#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#define PINO_DIN   GPIO_NUM_11   // a Lylu escutando
#define PINO_PA    GPIO_NUM_20   // liga o amplificador do alto-falante

#define TAXA       16000
#define QUADRO     320           // 20 ms de som por leitura

static esp_codec_dev_handle_t ouvido;
static int nivel;
static bool ligado;

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

    int64_t conta = 0;
    for (;;) {
        if (esp_codec_dev_read(ouvido, buf, amostras * sizeof *buf) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t soma = 0;
        for (int i = 0; i < amostras; i++) soma += (int32_t)buf[i] * buf[i];
        int rms = (int)sqrt((double)soma / amostras);

        // 8000 de RMS já é voz alta a um palmo. O que passar disso satura em 100.
        int agora = rms / 80;
        if (agora > 100) agora = 100;
        // Sobe na hora, desce devagar: a barra acompanha a voz em vez de piscar.
        nivel = agora > nivel ? agora : (nivel * 7 + agora * 3) / 10;

        // DIAGNÓSTICO enquanto não existe a tela: tirar quando a barra existir.
        if (++conta % 25 == 0)
            ESP_LOGI(TAG, "nivel=%d rms=%d  cru: %d %d %d %d", nivel, rms,
                     buf[0], buf[1], buf[2], buf[3]);
    }
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
        .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = codec, .data_if = dados,
    };
    ouvido = esp_codec_dev_new(&cfg_dev);
    if (!ouvido) { ESP_LOGE(TAG, "não montei o dispositivo de áudio"); return; }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .sample_rate = TAXA,
    };
    int e = esp_codec_dev_open(ouvido, &fs);
    if (e != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "esp_codec_dev_open falhou (%d)", e); return; }
    esp_codec_dev_set_in_gain(ouvido, 30.0);

    // DIAGNÓSTICO: o que o chip diz de si mesmo depois de configurado.
    // 0x09/0x0A = formato do I2S, 0x14 = entrada do microfone e ganho do PGA,
    // 0x15..0x1B = o caminho do ADC, 0x17 = volume do ADC.
    char linha[160];
    int n = 0;
    for (int r = 0x09; r <= 0x1B && n < (int)sizeof linha - 8; r++) {
        int v = -1;
        esp_codec_dev_read_reg(ouvido, r, &v);
        n += snprintf(linha + n, sizeof linha - n, "%02X=%02X ", r, v & 0xff);
    }
    ESP_LOGI(TAG, "ES8311 %s", linha);

    ligado = true;
    xTaskCreate(escuta, "escuta", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "microfone de pé — fala alguma coisa");
}

bool audio_escutando(void) { return ligado; }
int audio_nivel(void) { return ligado ? nivel : 0; }
