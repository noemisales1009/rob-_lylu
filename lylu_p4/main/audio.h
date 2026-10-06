// Os ouvidos (e, um dia, a voz) da Lylu.
//
// A placa tem um codec ES8311 soldado — ele respondeu no I2C em 0x18, no mesmo
// barramento do toque. É um chip de duas pontas: entrada de microfone e saída
// para alto-falante.
//
// A entrada: audio_nivel() diz o quão alto está o som agora, o suficiente para a
// Lylu perceber que alguém falou com ela. A saída vai para o alto-falante de
// 2 pinos (conector MX 1.25 na borda, abaixo do cartão SD; o de cima do cartão é
// a bateria), pelo amplificador NS4150 ligado no GPIO11.
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

// Um "blip" de três notas no alto-falante. Não espera tocar: volta na hora.
void audio_blip(void);
