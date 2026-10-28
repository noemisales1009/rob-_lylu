#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// O que a ponte conta para a tela. Tudo chega pela tarefa de rede da ponte:
// quem mexer no LVGL precisa guardar e mostrar depois (ver HERMES no main.c),
// como já acontece com os eventos de Wi-Fi.
typedef enum {
    PONTE_MUDOU,        // ficou online ou offline
    PONTE_MENSAGEM,     // chegou coisa do Hermes: kind ("chat", "estado", "comando") + payload JSON
    PONTE_RESPOSTA,     // igual, mas é a resposta a uma fala DESTA placa (não a um pedido do app)
    PONTE_VOZ_SUBIU,    // a fala chegou na ponte; agora é esperar a resposta
    PONTE_VOZ_FALHOU,   // não deu pra mandar a fala (texto = o motivo, curto)
} ponte_evento_t;

// audio: a voz da resposta (PCM 16 kHz mono), ou NULL. Quem recebe é dono dele.
typedef void (*ponte_aviso_t)(ponte_evento_t ev, const char *kind, const char *texto,
                              int16_t *audio, size_t amostras);

void ponte_iniciar(ponte_aviso_t aviso);
bool ponte_configurada(void);
bool ponte_online(void);

bool ponte_enviar_texto(const char *texto);          // conversa escrita
// Aviso para o Hermes (copiado). resposta_em_ms > 0: ele deve falar alguma coisa
// (a pausa do pomodoro), então, passado esse tempo, a ponte pergunta ligeiro até
// a resposta aparecer (no máximo uns 20 s). 0 = aviso que normalmente fica em silêncio.
// PONTE_JA_FALEI: a placa já deu o aviso sozinha (o áudio gravado do pomodoro); se o
// Hermes responder mesmo assim, a resposta é confirmada e descartada, sem tocar.
#define PONTE_JA_FALEI (-1)
bool ponte_enviar_evento(const char *payload_json, int resposta_em_ms);
bool ponte_enviar_voz(int16_t *pcm, size_t amostras); // a ponte fica dona do buffer

// O retrato do que está na tela, para o Hermes saber. Só o último vale: se
// chegar outro antes de enviar, o anterior é descartado.
void ponte_relatar(const char *estado_json);
