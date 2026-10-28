// IMA ADPCM — ver adpcm.h. A conta é a de sempre (a mesma do WAV "IMA ADPCM"),
// só a moldura é nossa: um cabeçalho de 4 bytes e um fluxo único, sem blocos,
// porque a ponte nunca perde pedaço (é tudo HTTPS).
#include "adpcm.h"

static const int16_t PASSO[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
    97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t AJUSTE[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int previsao, indice; } estado_t;

// Aplica um código ao estado: é o mesmo passo na ida e na volta, e é isso que
// mantém a placa e o relay andando juntos amostra por amostra.
static int16_t aplica(estado_t *e, int codigo)
{
    int passo = PASSO[e->indice];
    int delta = passo >> 3;
    if (codigo & 4) delta += passo;
    if (codigo & 2) delta += passo >> 1;
    if (codigo & 1) delta += passo >> 2;
    e->previsao += codigo & 8 ? -delta : delta;
    if (e->previsao > 32767) e->previsao = 32767;
    if (e->previsao < -32768) e->previsao = -32768;
    e->indice += AJUSTE[codigo];
    if (e->indice < 0) e->indice = 0;
    if (e->indice > 88) e->indice = 88;
    return (int16_t)e->previsao;
}

static int codifica_uma(estado_t *e, int amostra)
{
    int passo = PASSO[e->indice];
    int dif = amostra - e->previsao, codigo = 0;
    if (dif < 0) { codigo = 8; dif = -dif; }
    if (dif >= passo) { codigo |= 4; dif -= passo; }
    if (dif >= passo >> 1) { codigo |= 2; dif -= passo >> 1; }
    if (dif >= passo >> 2) codigo |= 1;
    aplica(e, codigo);
    return codigo;
}

size_t adpcm_tamanho(size_t amostras) { return ADPCM_CABECALHO + (amostras + 1) / 2; }
size_t adpcm_amostras(size_t bytes) { return bytes > ADPCM_CABECALHO ? (bytes - ADPCM_CABECALHO) * 2 : 0; }

size_t adpcm_codifica(const int16_t *pcm, size_t amostras, uint8_t *saida)
{
    estado_t e = { amostras ? pcm[0] : 0, 0 };
    saida[0] = (uint8_t)(e.previsao & 0xff);
    saida[1] = (uint8_t)((e.previsao >> 8) & 0xff);
    saida[2] = (uint8_t)e.indice;
    saida[3] = 0;
    uint8_t *p = saida + ADPCM_CABECALHO;
    for (size_t i = 0; i < amostras; i += 2) {
        int baixo = codifica_uma(&e, pcm[i]);
        int alto = i + 1 < amostras ? codifica_uma(&e, pcm[i + 1]) : 0;
        *p++ = (uint8_t)(baixo | alto << 4);
    }
    return (size_t)(p - saida);
}

size_t adpcm_decodifica(const uint8_t *entrada, size_t bytes, int16_t *pcm)
{
    if (bytes <= ADPCM_CABECALHO) return 0;
    estado_t e = { (int16_t)(entrada[0] | entrada[1] << 8), entrada[2] > 88 ? 88 : entrada[2] };
    size_t n = 0;
    for (size_t i = ADPCM_CABECALHO; i < bytes; i++) {
        pcm[n++] = aplica(&e, entrada[i] & 0x0f);
        pcm[n++] = aplica(&e, entrada[i] >> 4);
    }
    return n;
}
