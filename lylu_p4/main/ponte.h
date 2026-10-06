#pragma once

#include <stdbool.h>

// Chamado na tarefa de rede da ponte. Quem mexer no LVGL precisa usar
// placa_trava(), como já acontece com os eventos de Wi-Fi.
typedef void (*ponte_aviso_t)(const char *texto, bool online);

void ponte_iniciar(ponte_aviso_t aviso);
bool ponte_enviar_texto(const char *texto);
bool ponte_configurada(void);
bool ponte_online(void);
