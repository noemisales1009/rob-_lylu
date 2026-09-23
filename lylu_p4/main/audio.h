// Os ouvidos (e, um dia, a voz) da Lylu.
//
// A placa tem um codec ES8311 soldado — ele respondeu no I2C em 0x18, no mesmo
// barramento do toque. É um chip de duas pontas: entrada de microfone e saída
// para alto-falante.
//
// Por enquanto só a entrada: audio_nivel() diz o quão alto está o som agora, que
// é o suficiente para a Lylu perceber que alguém falou com ela.
#pragma once
#include <stdbool.h>

// Liga o codec e começa a escutar. Não trava nem derruba nada se o chip não
// responder — a Lylu simplesmente segue surda.
void audio_iniciar(void);

// Existe microfone funcionando? Falso se o codec não subiu.
bool audio_escutando(void);

// O volume do som agora, de 0 a 100. Sobe na hora e desce devagar, senão a
// barra pisca a cada sílaba em vez de acompanhar a voz.
int audio_nivel(void);
