// Os ouvidos e a voz da Lylu.
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
#include <stddef.h>
#include <stdint.h>

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

// ---- gravar a fala (para o Hermes) ----
// O som que já chega para o nível passa também para um buffer na PSRAM, em PCM
// de 16 kHz, mono, 16 bits — o formato que a ponte manda sem converter nada.
bool audio_gravar(int max_ms);       // começa (descarta a gravação anterior)
void audio_parar_gravacao(void);     // para; o que foi gravado fica guardado
bool audio_gravando(void);
int audio_gravado_ms(void);
bool audio_ouviu_fala(void);         // já teve voz de verdade, não só ruído
int audio_silencio_ms(void);         // quieto há quanto tempo, depois da última fala
// Entrega o que foi gravado; quem recebe é dono do buffer (free) e ele sai daqui.
int16_t *audio_pega_gravacao(size_t *amostras);

// ---- a voz da resposta ----
// Toca PCM de 16 kHz mono. A placa fica dona do buffer e dá free ao terminar.
void audio_tocar(int16_t *pcm, size_t amostras);
void audio_calar(void);              // corta o que estiver tocando
bool audio_falando(void);
