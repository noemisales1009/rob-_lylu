// ============================================================
//  LYLU - Tela de status no ESP32-4848S040C (Guition 480x480)
//
//  A tela inteira e a casa da Lylu: ela anda em quatro direcoes,
//  escolhe destinos e para para demonstrar suas emocoes.
//
//  Formato dos .bin: 150 x 150 pixels, RGB565, BIG-ENDIAN.
//                    45.000 bytes cada, 12 frames por humor.
//  Magenta puro (255,0,255) e tratado como transparente.
// ============================================================

#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <TAMC_GT911.h>   // touch capacitivo GT911
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= CONEXAO (PREENCHER AQUI!) =================
// O n8n grava o humor e a fala na tabela lylu_estado do Supabase.
// A placa le de la a cada 30 segundos e troca a carinha sozinha.
#define WIFI_NOME     "SUA_REDE_AQUI"
#define WIFI_SENHA    "SUA_SENHA_AQUI"
#define SUPABASE_URL  "https://SEUPROJETO.supabase.co"
#define SUPABASE_KEY  "SUA_CHAVE_ANON_AQUI"
#define MS_POLL_SUPABASE 30000
// =============================================================

#define GFX_BL 38

// Esta placa mostra as cores invertidas. Com 1, o codigo compensa
// automaticamente (tanto nos desenhos quanto nos frames da Lylu).
// Se um dia trocar de tela e as cores sairem erradas, poe 0 aqui.
#define TELA_INVERTE_CORES 1

// ANTI-TREMOR: a imagem da tela mora na PSRAM e o video fica lendo dela
// sem parar. Quando o processador tambem usa a PSRAM, os dois brigam pela
// mesma via e a tela treme. O "bounce buffer" usa a memoria interna rapida
// como intermediaria e acaba com a briga.
// 10 linhas = 9,6 KB de memoria interna. Se ainda tremer, tente 20 ou 40.
// Com 0 o recurso fica desligado (era assim que estava antes).
#define BOUNCE_LINHAS 40

// ---------- Display (configuracao que ja funciona nesta placa) ----------
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  18, 17, 16, 21,
  11, 12, 13, 14, 0,       // R
  8, 20, 3, 46, 9, 10,     // G
  4, 5, 6, 7, 15,          // B
  1, 10, 8, 20,            // hsync
  1, 10, 8, 10,            // vsync
  // Daqui pra baixo sao os opcionais. Os 5 primeiros sao os valores
  // padrao (nao mudam nada); o que importa e o ultimo.
  0,            // pclk_active_neg (1 desloca/treme a imagem nesta placa - nao usar)
  12000000L,    // pclk 12 MHz (mesmo padrao que a lib ja usava)
  false,        // useBigEndian
  0,            // de_idle_high
  0,            // pclk_idle_high
  BOUNCE_LINHAS * 480);   // <-- bounce buffer, o anti-tremor

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480, rgbpanel, 0, true,
  bus, GFX_NOT_DEFINED,
  st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

// ---------- Touch (GT911 nos pinos I2C 19/45) ----------
// Nesta placa os pinos INT e RST do touch nao sao ligados ao ESP32,
// por isso o -1. A biblioteca reclama no log, mas funciona.
TAMC_GT911 touch(19 /*SDA*/, 45 /*SCL*/, -1, -1, 480, 480);

// Traduz cor normal (r,g,b de 0 a 255) para o que a tela precisa receber
uint16_t cor(uint8_t r, uint8_t g, uint8_t b) {
#if TELA_INVERTE_CORES
  return gfx->color565(255 - r, 255 - g, 255 - b);
#else
  return gfx->color565(r, g, b);
#endif
}

// ---------- Medidas da tela ----------
#define TELA_W        480
#define TELA_H        480
#define TITULO_H       52
#define LADO_LYLU_W   210

#define FRAME_W       150
#define FRAME_H       150
#define FRAME_PX      (FRAME_W * FRAME_H)   // 22.500 pixels
#define NUM_FRAMES     12
#define TOTAL_DIRECOES  4

#define LYLU_PAINEL_X   ((LADO_LYLU_W - FRAME_W) / 2) // posicao antiga do cartao
#define LYLU_PAINEL_Y   (TITULO_H + 58)
#define LYLU_X_INICIAL  LYLU_PAINEL_X
#define LYLU_Y_INICIAL  LYLU_PAINEL_Y
#define LYLU_X_MAX     (TELA_W - FRAME_W)             // 330
#define LYLU_Y_MAX     (TELA_H - FRAME_H)             // 330
#define LYLU_PASSO      3

#define MS_POR_FRAME   80    // ~12 quadros por segundo
#define MS_PAUSA_EMOCAO 3200  // tempo mostrando uma emocao antes de voltar a andar

// ---------- Rotina de tedio/sono (em minutos) ----------
// Sem ninguem mexer: fica entediada -> dorme -> apaga a tela.
// Toque na tela ou humor novo do n8n acordam ela.
#define MIN_ATE_ENTEDIADA 10
#define MIN_ATE_DORMIR    20
#define MIN_ATE_APAGAR    30

// ---------- Humores ----------
struct Humor {
  const char *prefixo;   // nome dos arquivos: prefixo00.bin ... prefixo11.bin
  const char *nome;
  const char *sub;
  uint8_t r, g, b;       // cor de fundo do lado da Lylu (valores normais)
};

Humor HUMORES[] = {
  { "animada",     "ANIMADA",     "bora que hoje rende!",       31,  61,  43 },
  { "comemorando", "COMEMORANDO", "tudo feito, arrasou!",       61,  53,  18 },
  { "brincalhona", "BRINCALHONA", "da tempo de um joguinho",    51,  35,  74 },
  { "cuidadora",   "CUIDADORA",   "ja bebeu agua hoje?",        18,  58,  61 },
  { "descansando", "DESCANSANDO", "pausa merecida",             30,  36,  64 },
  { "entediada",   "ENTEDIADA",   "cade as tarefas?",           46,  46,  54 },
  { "alertando",   "ALERTANDO",   "tem coisa atrasada!",        74,  36,  18 },
  { "julgadora",   "JULGADORA",   "serio que nao fez nada?",    66,  18,  31 }
};
const int TOTAL_HUMORES = sizeof(HUMORES) / sizeof(HUMORES[0]);

// ---------- Tarefas (depois isso vem do Supabase via n8n) ----------
struct Tarefa {
  const char *texto;
  bool feita;
};

Tarefa TAREFAS[] = {
  { "Tomar remedio",     true  },
  { "Responder e-mails", true  },
  { "Beber 2L de agua",  false },
  { "Caminhada 20min",   false },
  { "Estudar ESP32",     false }
};
const int TOTAL_TAREFAS = sizeof(TAREFAS) / sizeof(TAREFAS[0]);

// ---------- Estado ----------
uint16_t *frames = nullptr;      // 12 frames do humor atual, na PSRAM
uint16_t *framesAndando[TOTAL_DIRECOES] = { nullptr, nullptr, nullptr, nullptr };
uint16_t *framesPensando = nullptr;
uint16_t *framesApontando = nullptr;
uint16_t *fundoLylu = nullptr;   // pixels que estavam atras da Lylu
uint16_t *composicao = nullptr;  // rascunho onde o quadro e montado antes de ir pra tela
uint8_t   linhaBruta[FRAME_W * 2];
bool      framesOk = false;      // os frames do humor atual carregaram mesmo?
bool      direcaoOk[TOTAL_DIRECOES] = { false, false, false, false };
bool      caminhadasOk = false;
bool      pensandoOk = false;
bool      apontandoOk = false;

enum DirecaoLylu { DIREITA = 0, ESQUERDA = 1, BAIXO = 2, CIMA = 3 };
enum AcaoLylu { ANDANDO, MOSTRANDO_EMOCAO, PENSANDO, APONTANDO_LISTA };

const char *PREFIXOS_DIRECAO[TOTAL_DIRECOES] = {
  "anddireita", "andesquerda", "andbaixo", "andcima"
};

int       humorAtual = 0;
int       frameAtual = 0;
int       lyluX = LYLU_X_INICIAL;
int       lyluY = LYLU_Y_INICIAL;
int       destinoX = LYLU_X_INICIAL;
int       destinoY = LYLU_Y_INICIAL;
DirecaoLylu direcaoAtual = DIREITA;
AcaoLylu   acaoAtual = ANDANDO;
bool      horizontalPrimeiro = true;
bool      lyluEstaDesenhada = false;
unsigned long ultimoFrame = 0;
unsigned long fimDaAcao = 0;

// rotina de tedio/sono
int  estadoIdle = 0;             // 0 acordada, 1 entediada, 2 dormindo, 3 tela apagada
int  humorAntes = 0;             // o que ela fazia antes de ficar de bobeira
unsigned long ultimaAtividade = 0;

// modo de teste do display: tela inteira de uma cor so, para
// procurar pixel preso ('b' branca, 'p' preta, 'v' volta ao normal)
bool modoTeste = false;

// brilho da tela (0 = apagada, 255 = maximo)
void luzTela(uint8_t nivel) {
  ledcWrite(GFX_BL, nivel);
}

// A tela RGB possui framebuffer acessivel. Antes de desenhar a Lylu,
// guardamos o retangulo que estava atras dela. No quadro seguinte esse
// fundo e restaurado, evitando rastros e preservando textos/checklists.
void removerLyluDaTela() {
  if (!lyluEstaDesenhada || !fundoLylu) return;

  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) return;

  for (int y = 0; y < FRAME_H; y++) {
    memcpy(fb + (lyluY + y) * TELA_W + lyluX,
           fundoLylu + y * FRAME_W,
           FRAME_W * sizeof(uint16_t));
  }
  lyluEstaDesenhada = false;
}

void guardarFundoDaLylu() {
  if (!fundoLylu) return;

  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) return;

  for (int y = 0; y < FRAME_H; y++) {
    memcpy(fundoLylu + y * FRAME_W,
           fb + (lyluY + y) * TELA_W + lyluX,
           FRAME_W * sizeof(uint16_t));
  }
}

void escolherNovoDestino() {
  // Evita escolher um destino colado na posicao atual.
  for (int tentativa = 0; tentativa < 8; tentativa++) {
    destinoX = random(0, LYLU_X_MAX + 1);
    destinoY = random(0, LYLU_Y_MAX + 1);
    if (abs(destinoX - lyluX) + abs(destinoY - lyluY) >= 90) break;
  }
  horizontalPrimeiro = random(0, 2) == 0;
  acaoAtual = ANDANDO;
  frameAtual = 0;
}

void iniciarAcaoParada(unsigned long agora) {
  int escolha = random(0, 100);

  // Na maior parte das paradas ela mostra o humor vindo do Supabase.
  // De vez em quando pensa; perto do lado esquerdo, pode apontar a lista.
  if (pensandoOk && escolha < 25) {
    acaoAtual = PENSANDO;
  } else if (apontandoOk && lyluX <= 120 && escolha < 40) {
    acaoAtual = APONTANDO_LISTA;
  } else {
    acaoAtual = MOSTRANDO_EMOCAO;
  }

  frameAtual = 0;
  fimDaAcao = agora + MS_PAUSA_EMOCAO;
}

void mudarDirecao(int novaDirecao) {
  if ((int)direcaoAtual != novaDirecao) {
    direcaoAtual = (DirecaoLylu)novaDirecao;
    frameAtual = 0;
  }
}

void moverRumoAoDestino(unsigned long agora) {
  bool moveu = false;

  if (horizontalPrimeiro && lyluX != destinoX) {
    if (lyluX < destinoX) {
      lyluX = min(lyluX + LYLU_PASSO, destinoX);
      mudarDirecao(DIREITA);
    } else {
      lyluX = max(lyluX - LYLU_PASSO, destinoX);
      mudarDirecao(ESQUERDA);
    }
    moveu = true;
  } else if (lyluY != destinoY) {
    if (lyluY < destinoY) {
      lyluY = min(lyluY + LYLU_PASSO, destinoY);
      mudarDirecao(BAIXO);
    } else {
      lyluY = max(lyluY - LYLU_PASSO, destinoY);
      mudarDirecao(CIMA);
    }
    moveu = true;
  } else if (lyluX != destinoX) {
    if (lyluX < destinoX) {
      lyluX = min(lyluX + LYLU_PASSO, destinoX);
      mudarDirecao(DIREITA);
    } else {
      lyluX = max(lyluX - LYLU_PASSO, destinoX);
      mudarDirecao(ESQUERDA);
    }
    moveu = true;
  }

  if (!moveu) iniciarAcaoParada(agora);
}

// Desenho SEM piscar: o quadro novo (fundo + Lylu por cima) e montado
// inteiro num rascunho na memoria interna rapida e copiado de uma vez
// para a tela. Nunca existe um instante "sem Lylu" visivel - era isso
// que causava a tremida/falha na regiao onde ela passava.
void desenharProximoFrameMovel(unsigned long agora) {
  if (!fundoLylu || !composicao) return;
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) return;

  int  velhoX = lyluX, velhoY = lyluY;
  bool haviaSprite = lyluEstaDesenhada;

  if (acaoAtual == ANDANDO) {
    moverRumoAoDestino(agora);
  } else if ((long)(agora - fimDaAcao) >= 0) {
    escolherNovoDestino();
    moverRumoAoDestino(agora);
  }

  uint16_t *animacao = nullptr;
  if (acaoAtual == ANDANDO && direcaoOk[direcaoAtual]) {
    animacao = framesAndando[direcaoAtual];
  } else if (acaoAtual == PENSANDO && pensandoOk) {
    animacao = framesPensando;
  } else if (acaoAtual == APONTANDO_LISTA && apontandoOk) {
    animacao = framesApontando;
  } else if (framesOk) {
    animacao = frames;
  }

  if (!animacao) {         // sem animacao valida: so tira a Lylu da tela
    removerLyluDaTela();
    return;
  }

  // 1) Fundo limpo da NOVA posicao: comeca do que esta na tela e, onde a
  //    regiao antiga da Lylu invade, usa o fundo guardado (sem a Lylu).
  for (int y = 0; y < FRAME_H; y++) {
    memcpy(composicao + y * FRAME_W,
           fb + (size_t)(lyluY + y) * TELA_W + lyluX,
           FRAME_W * sizeof(uint16_t));
  }
  if (haviaSprite) {
    int ix0 = max(lyluX, velhoX), ix1 = min(lyluX + FRAME_W, velhoX + FRAME_W);
    int iy0 = max(lyluY, velhoY), iy1 = min(lyluY + FRAME_H, velhoY + FRAME_H);
    for (int y = iy0; y < iy1; y++) {
      memcpy(composicao + (size_t)(y - lyluY) * FRAME_W + (ix0 - lyluX),
             fundoLylu  + (size_t)(y - velhoY) * FRAME_W + (ix0 - velhoX),
             (ix1 - ix0) * sizeof(uint16_t));
    }

    // 2) Restaura as tirinhas da posicao antiga que a nova nao cobre
    //    (o rastro de 3px atras do movimento).
    for (int y = 0; y < FRAME_H; y++) {
      int fy = velhoY + y;
      if (fy >= lyluY && fy < lyluY + FRAME_H) {
        // linha parcialmente coberta: restaura so as pontas expostas
        if (velhoX < lyluX) {
          int larg = min(lyluX - velhoX, FRAME_W);
          memcpy(fb + (size_t)fy * TELA_W + velhoX,
                 fundoLylu + (size_t)y * FRAME_W, larg * sizeof(uint16_t));
        }
        if (velhoX + FRAME_W > lyluX + FRAME_W) {
          int larg = min(velhoX - lyluX, FRAME_W);
          memcpy(fb + (size_t)fy * TELA_W + (velhoX + FRAME_W - larg),
                 fundoLylu + (size_t)y * FRAME_W + (FRAME_W - larg),
                 larg * sizeof(uint16_t));
        }
      } else {
        // linha totalmente fora da regiao nova: restaura inteira
        memcpy(fb + (size_t)fy * TELA_W + velhoX,
               fundoLylu + (size_t)y * FRAME_W, FRAME_W * sizeof(uint16_t));
      }
    }
  }

  // 3) O fundo limpo da nova posicao vira o "guardado" do proximo quadro.
  memcpy(fundoLylu, composicao, (size_t)FRAME_PX * sizeof(uint16_t));

  // 4) Poe a Lylu por cima do rascunho (magenta = transparente).
  uint16_t chave = cor(255, 0, 255);
  const uint16_t *spr = animacao + (size_t)frameAtual * FRAME_PX;
  for (int i = 0; i < FRAME_PX; i++) {
    uint16_t p = spr[i];
    if (p != chave) composicao[i] = p;
  }

  // 5) Copia o quadro pronto para a tela, linha a linha.
  for (int y = 0; y < FRAME_H; y++) {
    memcpy(fb + (size_t)(lyluY + y) * TELA_W + lyluX,
           composicao + (size_t)y * FRAME_W,
           FRAME_W * sizeof(uint16_t));
  }

  lyluEstaDesenhada = true;
  frameAtual = (frameAtual + 1) % NUM_FRAMES;
}

// ---------- Estado vindo do Supabase (escrito pela outra CPU) ----------
SemaphoreHandle_t trava;             // protege as variaveis abaixo
volatile bool remotoNovo   = false;  // chegou estado novo do Supabase?
volatile bool remotoAtivo  = false;  // ja conectou pelo menos 1 vez?
char remotoHumor[32] = "";
char remotoFala[176] = "";
char falaAtual[176]  = "";           // fala mostrada embaixo do nome

// Converte texto UTF-8 (acentos, emoji) para ASCII simples,
// porque a fonte da tela so desenha ASCII. Emoji e descartado.
char trocaAcento(uint16_t cp) {
  if (cp >= 0xE0 && cp <= 0xE5) return 'a';
  if (cp == 0xE7)               return 'c';
  if (cp >= 0xE8 && cp <= 0xEB) return 'e';
  if (cp >= 0xEC && cp <= 0xEF) return 'i';
  if (cp == 0xF1)               return 'n';
  if (cp >= 0xF2 && cp <= 0xF6) return 'o';
  if (cp >= 0xF9 && cp <= 0xFC) return 'u';
  if (cp >= 0xC0 && cp <= 0xC5) return 'A';
  if (cp == 0xC7)               return 'C';
  if (cp >= 0xC8 && cp <= 0xCB) return 'E';
  if (cp >= 0xCC && cp <= 0xCF) return 'I';
  if (cp == 0xD1)               return 'N';
  if (cp >= 0xD2 && cp <= 0xD6) return 'O';
  if (cp >= 0xD9 && cp <= 0xDC) return 'U';
  return 0;
}

void utf8ParaAscii(const char *in, char *out, size_t n) {
  size_t o = 0;
  const uint8_t *p = (const uint8_t *)in;
  while (*p && o < n - 1) {
    if (*p < 0x80) { out[o++] = *p++; }
    else if ((*p & 0xE0) == 0xC0) {
      if (!p[1]) break;
      uint16_t cp = ((uint16_t)(*p & 0x1F) << 6) | (p[1] & 0x3F);
      char c = trocaAcento(cp);
      if (c) out[o++] = c;
      p += 2;
    }
    else if ((*p & 0xF0) == 0xE0) { p += (p[1] && p[2]) ? 3 : 1; }   // pula
    else { p += (p[1] && p[2] && p[3]) ? 4 : 1; }                     // emoji: pula
  }
  out[o] = 0;
}

// Descobre qual humor da lista corresponde ao nome que o n8n mandou.
// Aceita variacoes: "brincalhao"/"brincalhona", "julgador"/"julgadora"...
int humorPorNome(const char *nome) {
  char n[32];
  utf8ParaAscii(nome, n, sizeof(n));
  for (char *c = n; *c; c++) *c = tolower(*c);

  if (!strncmp(n, "anima",    5)) return 0;
  if (!strncmp(n, "comemor",  7)) return 1;
  if (!strncmp(n, "brincalh", 8)) return 2;
  if (!strncmp(n, "cuidad",   6)) return 3;
  if (!strncmp(n, "descans",  7)) return 4;
  if (!strncmp(n, "entedia",  7)) return 5;
  if (!strncmp(n, "alert",    5)) return 6;
  if (!strncmp(n, "julgad",   6)) return 7;
  return -1;
}

// ============================================================
//  Carregamento dos frames
// ============================================================
bool carregarAnimacao(const char *prefixo, uint16_t *destinoFrames) {
  if (!destinoFrames) return false;
  char caminho[40];

  for (int f = 0; f < NUM_FRAMES; f++) {
    snprintf(caminho, sizeof(caminho), "/%s%02d.bin", prefixo, f);

    File arq = LittleFS.open(caminho, "r");
    if (!arq) {
      Serial.printf("ERRO: nao achei %s no LittleFS\n", caminho);
      return false;
    }

    uint16_t *destino = destinoFrames + ((size_t)f * FRAME_PX);

    for (int y = 0; y < FRAME_H; y++) {
      if (arq.read(linhaBruta, sizeof(linhaBruta)) != sizeof(linhaBruta)) {
        Serial.printf("ERRO: %s acabou antes da hora (linha %d)\n", caminho, y);
        arq.close();
        return false;
      }
      for (int x = 0; x < FRAME_W; x++) {
        // arquivo esta em big-endian: primeiro byte e a parte alta
        uint16_t px = ((uint16_t)linhaBruta[x * 2] << 8) | linhaBruta[x * 2 + 1];
#if TELA_INVERTE_CORES
        px ^= 0xFFFF;   // inverte os 3 canais de uma vez
#endif
        destino[y * FRAME_W + x] = px;
      }
    }
    arq.close();
  }
  return true;
}

bool carregarHumor(int idx) {
  return carregarAnimacao(HUMORES[idx].prefixo, frames);
}

// ============================================================
//  Desenho
// ============================================================
void textoCentralizado(const char *txt, int centroX, int y, int tam, uint16_t c) {
  int largura = strlen(txt) * 6 * tam;
  gfx->setTextColor(c);
  gfx->setTextSize(tam);
  gfx->setCursor(centroX - largura / 2, y);
  gfx->print(txt);
}

void desenharTitulo() {
  gfx->fillRect(0, 0, TELA_W, TITULO_H, cor(26, 26, 46));
  gfx->fillRect(0, TITULO_H - 3, TELA_W, 3, cor(74, 74, 106));
  textoCentralizado("STATUS DO DIA DE LYLU", TELA_W / 2, 18, 2, cor(255, 255, 255));
}

#define TAREFA_Y0   (TITULO_H + 60)   // onde comeca a primeira tarefa
#define TAREFA_ALT  34                // altura de cada faixa de tarefa

// Desenha (ou redesenha) UMA tarefa. Assim, ao tocar, so a linha
// tocada e atualizada e o resto da tela nem pisca.
void desenharTarefa(int i) {
  int x0 = LADO_LYLU_W + 3;
  int y  = TAREFA_Y0 + i * TAREFA_ALT;
  uint16_t c = TAREFAS[i].feita ? cor(126, 217, 87) : cor(232, 232, 238);

  // apaga a faixa desta tarefa e desenha de novo
  gfx->fillRect(x0 + 10, y - 4, TELA_W - x0 - 14, TAREFA_ALT - 4, cor(22, 22, 30));

  gfx->drawRect(x0 + 14, y, 16, 16, c);
  if (TAREFAS[i].feita) {
    gfx->drawLine(x0 + 17, y + 3,  x0 + 27, y + 13, c);
    gfx->drawLine(x0 + 27, y + 3,  x0 + 17, y + 13, c);
  }

  gfx->setTextSize(1);
  gfx->setTextColor(c);
  gfx->setCursor(x0 + 38, y + 5);
  gfx->print(TAREFAS[i].texto);
}

void desenharTarefas() {
  int x0 = LADO_LYLU_W + 3;
  gfx->fillRect(x0, TITULO_H, TELA_W - x0, TELA_H - TITULO_H, cor(22, 22, 30));

  gfx->setTextSize(2);
  gfx->setTextColor(cor(232, 232, 238));
  gfx->setCursor(x0 + 14, TITULO_H + 18);
  gfx->print("Plano do Dia:");
  gfx->drawFastHLine(x0 + 14, TITULO_H + 36, 160, cor(232, 232, 238));

  for (int i = 0; i < TOTAL_TAREFAS; i++) {
    desenharTarefa(i);
  }
}

// Toque na tela: se caiu em cima de uma tarefa, marca/desmarca.
void tratarToque(int tx, int ty) {
  // so vale o lado direito (a lista); o lado da Lylu ignora
  if (tx < LADO_LYLU_W + 3) return;
  if (ty < TAREFA_Y0 - 6) return;

  int i = (ty - (TAREFA_Y0 - 6)) / TAREFA_ALT;
  if (i < 0 || i >= TOTAL_TAREFAS) return;

  removerLyluDaTela();
  TAREFAS[i].feita = !TAREFAS[i].feita;
  desenharTarefa(i);
  Serial.printf("Tarefa '%s' -> %s\n", TAREFAS[i].texto,
                TAREFAS[i].feita ? "FEITA" : "pendente");
}

// Escreve a fala embaixo do nome, quebrando em linhas que cabem no painel.
void desenharFala(const char *txt, int yInicio) {
  char copia[176];
  utf8ParaAscii(txt, copia, sizeof(copia));

  char linha[34] = "";
  int y = yInicio;
  for (char *w = strtok(copia, " "); w && y < TELA_H - 14; w = strtok(NULL, " ")) {
    if (strlen(w) > 30) w[30] = 0;   // palavra gigante: corta
    if (linha[0] && strlen(linha) + 1 + strlen(w) > 30) {
      textoCentralizado(linha, LADO_LYLU_W / 2, y, 1, cor(220, 220, 225));
      y += 14;
      linha[0] = 0;
    }
    if (linha[0]) strlcat(linha, " ", sizeof(linha));
    strlcat(linha, w, sizeof(linha));
  }
  if (linha[0] && y < TELA_H - 14) {
    textoCentralizado(linha, LADO_LYLU_W / 2, y, 1, cor(220, 220, 225));
  }
}

void desenharPainelLylu(int idx) {
  Humor &h = HUMORES[idx];

  // fundo do lado esquerdo + divisoria
  gfx->fillRect(0, TITULO_H, LADO_LYLU_W, TELA_H - TITULO_H, cor(h.r, h.g, h.b));
  gfx->fillRect(LADO_LYLU_W, TITULO_H, 3, TELA_H - TITULO_H, cor(70, 70, 80));

  // A emocao aparece na propria Lylu andando pela tela - sem texto de humor.
  // So a fala vinda do n8n (quando existir) e mostrada.
  if (falaAtual[0]) desenharFala(falaAtual, LYLU_PAINEL_Y + FRAME_H + 42);
}

void trocarHumor(int idx) {
  removerLyluDaTela();
  humorAtual = idx;
  frameAtual = 0;

  Serial.printf("Humor -> %s\n", HUMORES[idx].nome);

  desenharPainelLylu(idx);

  framesOk = carregarHumor(idx);
  if (framesOk) {
    acaoAtual = MOSTRANDO_EMOCAO;
    fimDaAcao = millis() + MS_PAUSA_EMOCAO;
  }
  if (!framesOk) {
    // Sem os frames, a memoria esta cheia de lixo. NUNCA desenhar isso:
    // pinta o quadro com a cor do fundo e avisa.
    Humor &h = HUMORES[idx];
    gfx->fillRect(LYLU_PAINEL_X, LYLU_PAINEL_Y, FRAME_W, FRAME_H, cor(h.r, h.g, h.b));
    gfx->drawRect(LYLU_PAINEL_X, LYLU_PAINEL_Y, FRAME_W, FRAME_H, cor(255, 80, 80));
    textoCentralizado("SEM FRAMES", LADO_LYLU_W / 2, LYLU_PAINEL_Y + 70, 2, cor(255, 80, 80));
  }
}

// Acorda a Lylu: acende a tela e, se ela estava so de bobeira
// (entediada/dormindo), volta pro humor de antes.
void acordarTela() {
  ultimaAtividade = millis();
  if (estadoIdle == 0) return;
  int voltarPara = humorAntes;
  estadoIdle = 0;
  luzTela(255);
  if (humorAtual == 4 || humorAtual == 5) trocarHumor(voltarPara);
  Serial.println("Acordei!");
}

// ============================================================
//  Consulta ao Supabase - roda no OUTRO nucleo do ESP32,
//  entao a animacao nunca trava enquanto a internet responde.
// ============================================================
void tarefaSupabase(void *pv) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure clienteSeguro;
      clienteSeguro.setInsecure();   // sem validar certificado (ok pra este uso)

      HTTPClient http;
      String url = String(SUPABASE_URL) + "/rest/v1/lylu_estado?id=eq.1&select=humor,fala";
      if (http.begin(clienteSeguro, url)) {
        http.addHeader("apikey", SUPABASE_KEY);
        http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
        int codigo = http.GET();
        if (codigo == 200) {
          JsonDocument doc;
          if (deserializeJson(doc, http.getString()) == DeserializationError::Ok
              && doc.is<JsonArray>() && doc.size() > 0) {
            const char *h = doc[0]["humor"] | "";
            const char *f = doc[0]["fala"]  | "";
            xSemaphoreTake(trava, portMAX_DELAY);
            strlcpy(remotoHumor, h, sizeof(remotoHumor));
            strlcpy(remotoFala,  f, sizeof(remotoFala));
            remotoNovo  = true;
            remotoAtivo = true;
            xSemaphoreGive(trava);
          }
        } else {
          Serial.printf("Supabase respondeu %d\n", codigo);
        }
        http.end();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(MS_POLL_SUPABASE));
  }
}

// Aplica na tela o que chegou do Supabase (chamado pelo loop principal).
void aplicarEstadoRemoto() {
  char h[32], f[176];
  xSemaphoreTake(trava, portMAX_DELAY);
  strlcpy(h, remotoHumor, sizeof(h));
  strlcpy(f, remotoFala,  sizeof(f));
  remotoNovo = false;
  xSemaphoreGive(trava);

  bool falaMudou = (strcmp(falaAtual, f) != 0);
  strlcpy(falaAtual, f, sizeof(falaAtual));

  int idx = humorPorNome(h);
  if (idx >= 0 && idx != humorAtual) {
    Serial.printf("Supabase mandou: %s -> trocando\n", h);
    trocarHumor(idx);          // ja redesenha o painel (com a fala nova)
  } else if (falaMudou) {
    removerLyluDaTela();
    desenharPainelLylu(humorAtual);   // so a fala mudou: redesenha o painel
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Lylu ESP32 ===");

  gfx->begin();

  // desativa o flag MDT
  bus->beginWrite();
  bus->writeCommand(0xFF);
  bus->write(0x77); bus->write(0x01);
  bus->write(0x00); bus->write(0x00); bus->write(0x10);
  bus->writeC8D8(0xCD, 0x00);
  bus->endWrite();

  // luz de fundo com controle de brilho (pra poder escurecer ao dormir)
  ledcAttach(GFX_BL, 5000, 8);
  luzTela(255);

  touch.begin();   // liga o touch (endereco padrao 0x5D)

  gfx->fillScreen(cor(22, 22, 30));
  desenharTitulo();

  // ---- PSRAM: humor + quatro caminhadas + acoes + fundo sob a personagem ----
  size_t bytesAnimacao = (size_t)NUM_FRAMES * FRAME_PX * 2;
  frames = (uint16_t *)ps_malloc(bytesAnimacao);
  for (int d = 0; d < TOTAL_DIRECOES; d++) {
    framesAndando[d] = (uint16_t *)ps_malloc(bytesAnimacao);
  }
  framesPensando = (uint16_t *)ps_malloc(bytesAnimacao);
  framesApontando = (uint16_t *)ps_malloc(bytesAnimacao);
  // O fundo e o rascunho ficam na memoria INTERNA (rapida): e ela que faz o
  // desenho sair de uma vez so, sem brigar com o video pela PSRAM.
  fundoLylu  = (uint16_t *)heap_caps_malloc((size_t)FRAME_PX * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  composicao = (uint16_t *)heap_caps_malloc((size_t)FRAME_PX * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool memoriaOk = frames && framesPensando && framesApontando && fundoLylu && composicao;
  for (int d = 0; d < TOTAL_DIRECOES; d++) memoriaOk = memoriaOk && framesAndando[d];
  if (!memoriaOk) {
    Serial.println("ERRO: sem PSRAM. Ative 'PSRAM: OPI PSRAM' nas opcoes da placa.");
    textoCentralizado("SEM PSRAM", TELA_W / 2, 200, 3, cor(255, 80, 80));
    return;
  }

  // ---- LittleFS ----
  if (!LittleFS.begin(false)) {
    Serial.println("ERRO: LittleFS nao montou.");
    Serial.println("Falta rodar: Ctrl+Shift+P -> Upload LittleFS");
    gfx->fillRect(0, TITULO_H, TELA_W, TELA_H - TITULO_H, cor(22, 22, 30));
    textoCentralizado("SEM LITTLEFS", TELA_W / 2, 170, 3, cor(255, 80, 80));
    textoCentralizado("Falta subir a pasta data:", TELA_W / 2, 230, 1, cor(230, 230, 235));
    textoCentralizado("Ctrl+Shift+P", TELA_W / 2, 255, 2, cor(255, 255, 255));
    textoCentralizado("Upload LittleFS to Pico/ESP8266/ESP32", TELA_W / 2, 285, 1, cor(230, 230, 235));
    return;
  }
  Serial.printf("LittleFS ok: %u KB usados de %u KB\n",
                (unsigned)(LittleFS.usedBytes() / 1024),
                (unsigned)(LittleFS.totalBytes() / 1024));

  desenharTarefas();
  trocarHumor(0);

  caminhadasOk = true;
  for (int d = 0; d < TOTAL_DIRECOES; d++) {
    direcaoOk[d] = carregarAnimacao(PREFIXOS_DIRECAO[d], framesAndando[d]);
    caminhadasOk = caminhadasOk && direcaoOk[d];
  }
  pensandoOk = carregarAnimacao("pensando", framesPensando);
  apontandoOk = carregarAnimacao("apontandolista", framesApontando);
  if (!caminhadasOk) Serial.println("AVISO: falta alguma caminhada direcional no LittleFS");
  if (!pensandoOk)   Serial.println("AVISO: faltam pensando00.bin ... pensando11.bin");
  if (!apontandoOk)  Serial.println("AVISO: faltam apontandolista00.bin ... apontandolista11.bin");

  randomSeed(esp_random());
  escolherNovoDestino();

  // ---- WiFi + Supabase ----
  trava = xSemaphoreCreateMutex();
  WiFi.begin(WIFI_NOME, WIFI_SENHA);
  Serial.print("Conectando no WiFi");
  unsigned long inicioWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicioWifi < 10000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi ok! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nSem WiFi por enquanto (a placa segue tentando sozinha).");
  }
  // a consulta ao Supabase roda no nucleo 0; a tela roda no nucleo 1
  xTaskCreatePinnedToCore(tarefaSupabase, "supabase", 12288, NULL, 1, NULL, 0);
}

void loop() {
  if (!frames || !fundoLylu) return;

  unsigned long agora = millis();

  // A Lylu anda pela casa/tela e intercala caminhadas com emocoes.
  if (!modoTeste && (framesOk || caminhadasOk || pensandoOk || apontandoOk)
      && agora - ultimoFrame >= MS_POR_FRAME) {
    ultimoFrame = agora;
    desenharProximoFrameMovel(agora);
  }

  // chegou humor/fala novo do Supabase? acorda e aplica
  if (remotoNovo) {
    acordarTela();
    aplicarEstadoRemoto();
  }

  // ninguem mexe ha muito tempo? entediada -> dorme -> apaga a tela
  unsigned long parada = agora - ultimaAtividade;
  if (estadoIdle == 0 && parada >= MIN_ATE_ENTEDIADA * 60000UL) {
    estadoIdle = 1;
    humorAntes = humorAtual;
    trocarHumor(5);                    // entediada
  } else if (estadoIdle == 1 && parada >= MIN_ATE_DORMIR * 60000UL) {
    estadoIdle = 2;
    trocarHumor(4);                    // descansando (dormindo)
    luzTela(50);                       // escurece
  } else if (estadoIdle == 2 && parada >= MIN_ATE_APAGAR * 60000UL) {
    estadoIdle = 3;
    luzTela(0);                        // tela apagada, boa noite
    Serial.println("Tela apagada (zzz)");
  }

  // touch: verifica a cada 30 ms; reage so no momento em que
  // o dedo ENCOSTA (nao repete enquanto segura)
  static unsigned long ultimoPollToque = 0;
  static bool tocando = false;
  if (agora - ultimoPollToque >= 30) {
    ultimoPollToque = agora;
    touch.read();
    if (touch.isTouched && !tocando) {
      tocando = true;
      int tx = touch.points[0].x;
      int ty = touch.points[0].y;
      Serial.printf("Toque em %d,%d\n", tx, ty);
      if (estadoIdle != 0) {
        acordarTela();     // dormindo? o primeiro toque so acorda
      } else {
        ultimaAtividade = agora;
        tratarToque(tx, ty);
      }
    } else if (!touch.isTouched) {
      tocando = false;
    }
  }

  // digite 0-7 no Monitor Serial para forcar um humor;
  // 'b'/'p' = teste de pixel preso (tela branca/preta); 'v' = volta
  if (Serial.available()) {
    int c = Serial.read();
    if (c >= '0' && c <= '7') {
      int idx = c - '0';
      if (idx < TOTAL_HUMORES) {
        acordarTela();
        trocarHumor(idx);
      }
    } else if (c == 'b' || c == 'p') {
      modoTeste = true;
      lyluEstaDesenhada = false;
      luzTela(255);
      gfx->fillScreen(c == 'b' ? cor(255, 255, 255) : cor(0, 0, 0));
      Serial.println(c == 'b' ? "Teste: tela BRANCA" : "Teste: tela PRETA");
    } else if (c == 'v') {
      modoTeste = false;
      gfx->fillScreen(cor(22, 22, 30));
      desenharTitulo();
      desenharTarefas();
      desenharPainelLylu(humorAtual);
      acordarTela();
      Serial.println("Teste encerrado, tela normal");
    }
  }
}
