// IMA ADPCM: a voz da conversa 4 vezes menor (16 kHz, 4 bits por amostra).
//
// A placa transfere uns poucos KB por segundo pela ponte, e a voz crua (PCM de
// 16 bits) ocupava 32 KB por segundo de fala — a maior parte da espera numa
// conversa era a voz subindo e descendo. O IMA ADPCM é do tempo do telefone
// digital: quase de graça para codificar, e para voz quase não se ouve diferença.
//
// O formato ("ima_adpcm_16000"), igual ao do relay em servidor/relay/lylu_voz.py:
//   bytes 0-1  previsão inicial (int16, little-endian)
//   byte  2    índice inicial do passo (0 a 88)
//   byte  3    0
//   depois     4 bits por amostra; o nibble de baixo é a amostra par
#pragma once
#include <stddef.h>
#include <stdint.h>

#define ADPCM_FORMATO "ima_adpcm_16000"
#define ADPCM_CABECALHO 4

size_t adpcm_tamanho(size_t amostras);
size_t adpcm_codifica(const int16_t *pcm, size_t amostras, uint8_t *saida);   // devolve os bytes escritos
size_t adpcm_amostras(size_t bytes);
size_t adpcm_decodifica(const uint8_t *entrada, size_t bytes, int16_t *pcm); // devolve as amostras escritas
