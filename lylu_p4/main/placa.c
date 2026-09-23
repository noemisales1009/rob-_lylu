// Hardware da Guition JC1060P470.
// Pinos vêm do projeto espcontrol (devices/guition-esp32-p4-jc1060p470);
// a sequência de inicialização da tela vem da demonstração de fábrica.

#include "placa.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9165.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

static const char *TAG = "placa";

static i2c_master_bus_handle_t barramento;

#define PINO_LCD_RESET   5
#define PINO_LUZ         23
#define PINO_I2C_SDA     7
#define PINO_I2C_SCL     8
#define PINO_TOQUE_RESET 22
#define PINO_TOQUE_INT   21
#define LDO_DSI_CANAL    3      // alimenta o PHY do MIPI-DSI
#define LDO_DSI_MV       2500


// Sequência de inicialização extraída da demonstração de fábrica desta placa
// (output/backup-p4-fabrica). As do ESPHome e a padrão do driver deixaram a tela
// com listras ou só branca: este painel é uma variante com sequência própria.
#define D(...) (const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ })
static const jd9165_lcd_init_cmd_t INIT_TELA[] = {
    { 0x30, D(0x00), 0 },
    { 0xF7, D(0x49, 0x61, 0x02, 0x00), 0 },
    { 0x30, D(0x01), 0 },
    { 0x04, D(0x0C), 0 },
    { 0x05, D(0x08), 0 },
    { 0x0B, D(0x11), 0 },
    { 0x20, D(0x04), 0 },
    { 0x1F, D(0x05), 0 },
    { 0x23, D(0x38), 0 },
    { 0x28, D(0x18), 0 },
    { 0x29, D(0x29), 0 },
    { 0x2A, D(0x01), 0 },
    { 0x2B, D(0x29), 0 },
    { 0x2C, D(0x01), 0 },
    { 0x30, D(0x02), 0 },
    { 0x00, D(0x05), 0 },
    { 0x01, D(0x22), 0 },
    { 0x02, D(0x08), 0 },
    { 0x03, D(0x12), 0 },
    { 0x04, D(0x16), 0 },
    { 0x05, D(0x64), 0 },
    { 0x06, D(0x00), 0 },
    { 0x07, D(0x00), 0 },
    { 0x08, D(0x78), 0 },
    { 0x09, D(0x00), 0 },
    { 0x0A, D(0x04), 0 },
    { 0x0B, D(0x16, 0x17, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x07, 0x09), 0 },
    { 0x0C, D(0x09, 0x1E, 0x1E, 0x1C, 0x1C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D), 0 },
    { 0x0D, D(0x0A, 0x05, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x06, 0x08), 0 },
    { 0x0E, D(0x08, 0x1F, 0x1F, 0x1D, 0x1D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D), 0 },
    { 0x0F, D(0x0A, 0x05, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1D, 0x1D, 0x1F), 0 },
    { 0x10, D(0x1F, 0x08, 0x08, 0x06, 0x06, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D), 0 },
    { 0x11, D(0x16, 0x17, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1C, 0x1C, 0x1E), 0 },
    { 0x12, D(0x1E, 0x09, 0x09, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D), 0 },
    { 0x13, D(0x00, 0x00, 0x00, 0x00), 0 },
    { 0x14, D(0x00, 0x00, 0x41, 0x41), 0 },
    { 0x15, D(0x00, 0x00, 0x00, 0x00), 0 },
    { 0x17, D(0x00), 0 },
    { 0x18, D(0x85), 0 },
    { 0x19, D(0x06, 0x09), 0 },
    { 0x1A, D(0x05, 0x08), 0 },
    { 0x1B, D(0x0A, 0x04), 0 },
    { 0x26, D(0x00), 0 },
    { 0x27, D(0x00), 0 },
    { 0x30, D(0x06), 0 },
    { 0x12, D(0x3F, 0x25, 0x27, 0x35, 0x1D, 0x1B, 0x1B, 0x1A, 0x18, 0x0A, 0x2A, 0x21, 0x19, 0x30), 0 },
    { 0x13, D(0x3F, 0x26, 0x27, 0x35, 0x1E, 0x1C, 0x1C, 0x1A, 0x18, 0x0B, 0x2A, 0x21, 0x19, 0x30), 0 },
    { 0x30, D(0x0A), 0 },
    { 0x02, D(0x4F), 0 },
    { 0x0B, D(0x40), 0 },
    { 0x30, D(0x0D), 0 },
    { 0x0D, D(0x04), 0 },
    { 0x10, D(0x05), 0 },
    { 0x11, D(0x0C), 0 },
    { 0x12, D(0x05), 0 },
    { 0x13, D(0x0C), 0 },
    { 0x30, D(0x00), 0 },
    { 0x3A, D(0x55), 0 },
    { 0x11, D(0x00), 120 },
    { 0x29, D(0x00), 20 },
};

static void luz_iniciar(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = PINO_LUZ, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0, .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void placa_brilho(int porcento)
{
    if (porcento < 0) porcento = 0;
    if (porcento > 100) porcento = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, porcento * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// O GT911 desta placa reporta direto em 1024x600 — a mesma resolução da tela.
//
// Havia aqui uma conversão de 800x480 para 1024x600, copiada da demonstração de
// fábrica. Ela empurrava todo toque 28% para a direita e 25% para baixo. Perto do
// topo o erro era de uns 25px e ainda dava pra acertar um alvo grande; embaixo,
// onde fica o teclado, passava de 90px — quase duas fileiras de tecla — e as de
// baixo grudavam todas na borda. Medido na serial: um toque cru em y=540 (acima
// dos 480 que a conversão supunha) virava 599.
//
// Fica só o limite, para nenhuma leitura estranha cair fora da tela.
static void limita_toque(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *forca,
                         uint8_t *n, uint8_t max)
{
    for (int i = 0; i < *n; i++) {
        if (x[i] >= TELA_W) x[i] = TELA_W - 1;
        if (y[i] >= TELA_H) y[i] = TELA_H - 1;
    }
}

lv_display_t *placa_iniciar(void)
{
    luz_iniciar();

    // Tela: LDO do PHY -> barramento DSI (2 vias) -> painel JD9165
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = LDO_DSI_CANAL, .voltage_mv = LDO_DSI_MV };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo));

    esp_lcd_dsi_bus_handle_t dsi = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi, &dbi_cfg, &io));

    // Tempos lidos do código da demonstração de fábrica (52 MHz; os padrões do driver não servem).
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 52,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = TELA_W, .v_size = TELA_H,
            .hsync_pulse_width = 24, .hsync_back_porch = 136, .hsync_front_porch = 160,
            .vsync_pulse_width = 2, .vsync_back_porch = 21, .vsync_front_porch = 12,
        },
        .flags.use_dma2d = true,
    };
    jd9165_vendor_config_t vendor = {
        .init_cmds = INIT_TELA,
        .init_cmds_size = sizeof(INIT_TELA) / sizeof(INIT_TELA[0]),
        .mipi_config = { .dsi_bus = dsi, .dpi_config = &dpi_cfg },
    };
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = PINO_LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    esp_lcd_panel_handle_t painel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(io, &dev_cfg, &painel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(painel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(painel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(painel, true));

    // LVGL desenha em faixas de 100 linhas e o DMA2D copia para o framebuffer, como a
    // demonstração de fábrica. Desenhar direto em dois framebuffers alternados (avoid_tearing)
    // fazia a imagem tremer com a Lylu sempre animada.
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 12288;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = painel,
        .buffer_size = TELA_W * 100,
        .double_buffer = true,
        .hres = TELA_W,
        .vres = TELA_H,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = { .buff_spiram = true },
    };
    lvgl_port_display_dsi_cfg_t dsi_cfg = { .flags.avoid_tearing = false };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

    // Toque GT911 no I2C (o codec de áudio também mora aqui — ver placa_i2c)
    i2c_master_bus_handle_t i2c = NULL;
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PINO_I2C_SDA,
        .scl_io_num = PINO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c));
    barramento = i2c;

    // Quem mora no barramento. Serve para achar o toque e também para saber o que
    // mais a placa tem soldado sem precisar de esquema: 0x18/0x19 é o codec de
    // áudio ES8311 (microfone e alto-falante), 0x5D ou 0x14 é o toque GT911.
    for (int a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(i2c, a, 20) != ESP_OK) continue;
        const char *quem = a == 0x18 || a == 0x19 ? "  <- codec de audio ES8311"
                         : a == 0x5D || a == 0x14 ? "  <- toque GT911" : "";
        ESP_LOGI(TAG, "I2C responde em 0x%02X%s", a, quem);
    }

    // O GT911 atende em 0x5D ou 0x14, conforme o estado do pino INT no reset.
    // Procura nos dois antes de desistir.
    const uint8_t enderecos[] = { ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP };
    esp_lcd_touch_handle_t tp = NULL;
    for (int i = 0; i < 2 && !tp; i++) {
        if (i2c_master_probe(i2c, enderecos[i], 50) != ESP_OK) continue;
        ESP_LOGI(TAG, "toque GT911 encontrado em 0x%02X", enderecos[i]);

        esp_lcd_panel_io_handle_t tp_io = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        tp_io_cfg.dev_addr = enderecos[i];
        tp_io_cfg.scl_speed_hz = 400000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c, &tp_io_cfg, &tp_io));

        esp_lcd_touch_io_gt911_config_t gt_cfg = { .dev_addr = enderecos[i] };
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = TELA_W,
            .y_max = TELA_H,
            .rst_gpio_num = GPIO_NUM_NC,   // não resetar: o reset mudaria o endereço que acabamos de achar
            .int_gpio_num = GPIO_NUM_NC,
            .process_coordinates = limita_toque,
            .driver_data = &gt_cfg,
        };
        if (esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp) != ESP_OK) tp = NULL;
    }
    if (tp) {
        lvgl_port_touch_cfg_t t = { .disp = disp, .handle = tp };
        lvgl_port_add_touch(&t);
    } else {
        ESP_LOGE(TAG, "toque GT911 não respondeu em 0x5D nem 0x14; seguindo sem toque");
    }

    placa_brilho(95);
    ESP_LOGI(TAG, "tela 1024x600 pronta");
    return disp;
}

i2c_master_bus_handle_t placa_i2c(void) { return barramento; }

bool placa_trava(void) { return lvgl_port_lock(0); }
void placa_destrava(void) { lvgl_port_unlock(); }
