// O Wi-Fi da Lylu.
//
// A ESP32-P4 não tem rádio: quem fala com o ar é o ESP32-C6 soldado na placa,
// ligado por SDIO e comandado pelo ESP-Hosted. Do lado de cá isso é invisível —
// o esp_wifi_remote troca as funções esp_wifi_* por versões que atravessam o
// SDIO, então o código aqui é o mesmo de qualquer ESP32.
//
// A senha fica na NVS, nunca no código (era o que o sketch antigo fazia, com
// SUA_REDE_AQUI no topo do arquivo pra não vazar no GitHub).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define REDE_NOME_MAX   33      // 32 do SSID + o \0
#define REDE_SENHA_MAX  64
#define REDE_MAX_ACHADAS 20

typedef enum {
    REDE_SEM_RADIO,     // o C6 não respondeu — a placa segue sem internet
    REDE_PARADA,        // rádio ligado, nenhuma rede salva
    REDE_PROCURANDO,
    REDE_CONECTANDO,
    REDE_CONECTADA,
    REDE_SENHA_ERRADA,
    REDE_SUMIU,         // a rede salva não apareceu por perto
} rede_estado_t;

typedef struct {
    char    nome[REDE_NOME_MAX];
    uint8_t barras;     // 0 a 4
    bool    com_senha;
} rede_achada_t;

// Chamado sempre que o estado ou a lista muda. Roda na thread de eventos do
// ESP-IDF, não na da tela: quem for mexer no LVGL precisa de placa_trava().
typedef void (*rede_aviso_t)(void);

// Liga o rádio e, se houver rede salva, já tenta conectar. Não trava: se o C6
// não responder, o estado vira REDE_SEM_RADIO e a Lylu segue a vida offline.
void rede_iniciar(rede_aviso_t aviso);

void rede_procurar(void);
void rede_conectar(const char *nome, const char *senha);
void rede_esquecer(void);

rede_estado_t rede_estado(void);
const char *rede_nome(void);    // rede conectada ou salva; "" se nenhuma
const char *rede_ip(void);      // "" enquanto não conecta
bool rede_tem_salva(void);
int rede_sinal(void);           // barras (1 a 4) da rede conectada; 0 se não conectou ou não mediu

// Copia a última varredura para o chamador (a lista muda na thread de eventos).
// Devolve quantas redes copiou.
int rede_copia_lista(rede_achada_t *destino, int maximo);
