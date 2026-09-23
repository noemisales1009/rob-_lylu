// Hardware da Guition JC1060P470 (ESP32-P4, tela 7" 1024x600 MIPI-DSI JD9165, toque GT911)
#pragma once
#include "driver/i2c_master.h"
#include "lvgl.h"

#define TELA_W 1024
#define TELA_H 600

// Liga tela, luz de fundo, toque e LVGL. Devolve o display pronto.
lv_display_t *placa_iniciar(void);

// O barramento I2C da placa. O toque e o codec de audio dividem o mesmo.
i2c_master_bus_handle_t placa_i2c(void);

// Brilho da luz de fundo, 0 a 100.
void placa_brilho(int porcento);

// Toda mexida no LVGL fora dos callbacks dele precisa estar entre trava/destrava.
bool placa_trava(void);
void placa_destrava(void);
