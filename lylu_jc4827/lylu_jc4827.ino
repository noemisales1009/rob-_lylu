// ============================================================
//  LYLU - Tela de status na JC4827W543C_I (Guition 4.3", 480x272)
//
//  Porte da versao ESP32-4848S040C. Diferencas principais:
//  - Display NV3041A via QSPI (nao e RGB paralelo): o video tem
//    memoria propria, entao NAO existem tremor/pontinho/deslize.
//  - O codigo nao consegue LER a tela de volta; por isso mantemos
//    uma "sombra" da interface na PSRAM (canvas). A Lylu e montada
//    sobre a sombra num rascunho e so o retangulo dela e enviado.
//  - Tela menor (480x272 paisagem): layout compactado.
//
//  Frames: 150x150, RGB565 BIG-ENDIAN, 45.000 bytes, 12 por anim.
//  Magenta puro (255,0,255) = transparente.
// ============================================================

#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <TAMC_GT911.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= CONEXAO (PREENCHER AQUI!) =================
#define WIFI_NOME     "SUA_REDE_AQUI"
#define WIFI_SENHA    "SUA_SENHA_AQUI"
#define SUPABASE_URL  "https://SEUPROJETO.supabase.co"
#define SUPABASE_KEY  "SUA_CHAVE_ANON_AQUI"
#define MS_POLL_SUPABASE 30000
// =============================================================

#define GFX_BL 1   // luz de fundo desta placa fica no GPIO 1

// Se as cores sairem trocadas/negativas nesta tela, alterne 0/1 aqui
// (na 4848 era 1; paineis NV3041A normalmente ficam bem com 0).
#define TELA_INVERTE_CORES 0

// ---------- Display (JC4827W543: NV3041A via QSPI) ----------
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */);

Arduino_GFX *tela = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0 /* rotacao */, true /* IPS */);

// A "sombra": tudo e desenhado aqui (PSRAM) e enviado para a tela.
Arduino_Canvas *gfx = new Arduino_Canvas(480, 272, tela);

// ---------- Touch (GT911; nesta placa INT e RST SAO ligados) ----------
TAMC_GT911 touch(8 /*SDA*/, 4 /*SCL*/, 3 /*INT*/, 38 /*RST*/, 480, 272);

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
#define TELA_H        272
#define TITULO_H       32
#define LADO_LYLU_W   237

#define FRAME_W       150
#define FRAME_H       150
#define FRAME_PX      (FRAME_W * FRAME_H)
#define NUM_FRAMES     12
#define TOTAL_DIRECOES  4

#define LYLU_X_MAX    (TELA_W - FRAME_W)   // 330
#define LYLU_Y_MAX    (TELA_H - FRAME_H)   // 122
#define LYLU_PASSO      3

#define MS_POR_FRAME    80
#define MS_PAUSA_EMOCAO 3200

// ---------- Rotina de tedio/sono (em minutos) ----------
#define MIN_ATE_ENTEDIADA 10
#define MIN_ATE_DORMIR    20
#define MIN_ATE_APAGAR    30

// ---------- Humores ----------
struct Humor {
  const char *prefixo;
  const char *nome;
  const char *sub;
  uint8_t r, g, b;
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
uint16_t *frames = nullptr;
uint16_t *framesAndando[TOTAL_DIRECOES] = { nullptr, nullptr, nullptr, nullptr };
uint16_t *framesPensando = nullptr;
uint16_t *framesApontando = nullptr;
uint16_t *composicao = nullptr;   // rascunho (RAM interna) do retangulo da Lylu
uint8_t   linhaBruta[FRAME_W * 2];
bool      framesOk = false;
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
int       lyluX = 40;
int       lyluY = 60;
int       destinoX = 40;
int       destinoY = 60;
DirecaoLylu direcaoAtual = DIREITA;
AcaoLylu   acaoAtual = ANDANDO;
bool      horizontalPrimeiro = true;
bool      lyluEstaDesenhada = false;
unsigned long ultimoFrame = 0;
unsigned long fimDaAcao = 0;

// rotina de tedio/sono
int  estadoIdle = 0;
int  humorAntes = 0;
unsigned long ultimaAtividade = 0;

// modo de teste do display ('b' branca, 'p' preta, 'v' volta)
bool modoTeste = false;

void luzTela(uint8_t nivel) {
  ledcWrite(GFX_BL, nivel);
}

// ============================================================
//  Envio de regioes: copia um retangulo da sombra (canvas) para
//  a tela fisica. Como o rascunho tem 45 KB, regioes maiores sao
//  enviadas em faixas.
// ============================================================
void enviarRegiao(int x, int y, int w, int h) {
  if (!composicao) return;
  uint16_t *cv = gfx->getFramebuffer();
  if (!cv) return;

  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > TELA_W) w = TELA_W - x;
  if (y + h > TELA_H) h = TELA_H - y;
  if (w <= 0 || h <= 0) return;

  int maxLinhas = FRAME_PX / w;
  int feito = 0;
  while (feito < h) {
    int linhas = min(maxLinhas, h - feito);
    for (int r = 0; r < linhas; r++) {
      memcpy(composicao + (size_t)r * w,
             cv + (size_t)(y + feito + r) * TELA_W + x,
             (size_t)w * sizeof(uint16_t));
    }
    tela->draw16bitRGBBitmap(x, y + feito, composicao, w, linhas);
    feito += linhas;
  }
}

// Tira a Lylu da tela fisica (a sombra nunca contem a Lylu).
void removerLyluDaTela() {
  if (!lyluEstaDesenhada) return;
  enviarRegiao(lyluX, lyluY, FRAME_W, FRAME_H);
  lyluEstaDesenhada = false;
}

void escolherNovoDestino() {
  for (int tentativa = 0; tentativa < 8; tentativa++) {
    destinoX = random(0, LYLU_X_MAX + 1);
    destinoY = random(0, LYLU_Y_MAX + 1);
    if (abs(destinoX - lyluX) + abs(destinoY - lyluY) >= 60) break;
  }
  horizontalPrimeiro = random(0, 2) == 0;
  acaoAtual = ANDANDO;
  frameAtual = 0;
}

void iniciarAcaoParada(unsigned long agora) {
  int escolha = random(0, 100);
  if (pensandoOk && escolha < 25) {
    acaoAtual = PENSANDO;
  } else if (apontandoOk && lyluX >= 150 && escolha < 40) {
    acaoAtual = APONTANDO_LISTA;   // perto da lista (que fica a direita)
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
    if (lyluX < destinoX) { lyluX = min(lyluX + LYLU_PASSO, destinoX); mudarDirecao(DIREITA); }
    else                  { lyluX = max(lyluX - LYLU_PASSO, destinoX); mudarDirecao(ESQUERDA); }
    moveu = true;
  } else if (lyluY != destinoY) {
    if (lyluY < destinoY) { lyluY = min(lyluY + LYLU_PASSO, destinoY); mudarDirecao(BAIXO); }
    else                  { lyluY = max(lyluY - LYLU_PASSO, destinoY); mudarDirecao(CIMA); }
    moveu = true;
  } else if (lyluX != destinoX) {
    if (lyluX < destinoX) { lyluX = min(lyluX + LYLU_PASSO, destinoX); mudarDirecao(DIREITA); }
    else                  { lyluX = max(lyluX - LYLU_PASSO, destinoX); mudarDirecao(ESQUERDA); }
    moveu = true;
  }

  if (!moveu) iniciarAcaoParada(agora);
}

// Monta o retangulo da Lylu (sombra + personagem) no rascunho e envia.
// As tirinhas expostas da posicao antiga sao apagadas com a sombra.
void desenharProximoFrameMovel(unsigned long agora) {
  if (!composicao) return;
  uint16_t *cv = gfx->getFramebuffer();
  if (!cv) return;

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

  if (!animacao) {
    removerLyluDaTela();
    return;
  }

  // 1) apaga o rastro: tirinhas da posicao antiga que a nova nao cobre
  if (haviaSprite) {
    int dx = lyluX - velhoX;
    int dy = lyluY - velhoY;
    if (dx > 0)      enviarRegiao(velhoX, velhoY, dx, FRAME_H);
    else if (dx < 0) enviarRegiao(lyluX + FRAME_W, velhoY, -dx, FRAME_H);
    if (dy > 0)      enviarRegiao(velhoX, velhoY, FRAME_W, dy);
    else if (dy < 0) enviarRegiao(velhoX, lyluY + FRAME_H, FRAME_W, -dy);
  }

  // 2) rascunho = fundo (sombra) + Lylu por cima (magenta transparente)
  for (int y = 0; y < FRAME_H; y++) {
    memcpy(composicao + (size_t)y * FRAME_W,
           cv + (size_t)(lyluY + y) * TELA_W + lyluX,
           FRAME_W * sizeof(uint16_t));
  }
  uint16_t chave = cor(255, 0, 255);
  const uint16_t *spr = animacao + (size_t)frameAtual * FRAME_PX;
  for (int i = 0; i < FRAME_PX; i++) {
    uint16_t p = spr[i];
    if (p != chave) composicao[i] = p;
  }

  // 3) envia o retangulo pronto de uma vez
  tela->draw16bitRGBBitmap(lyluX, lyluY, composicao, FRAME_W, FRAME_H);

  lyluEstaDesenhada = true;
  frameAtual = (frameAtual + 1) % NUM_FRAMES;
}

// ---------- Estado vindo do Supabase (escrito pela outra CPU) ----------
SemaphoreHandle_t trava;
volatile bool remotoNovo   = false;
volatile bool remotoAtivo  = false;
char remotoHumor[32] = "";
char remotoFala[176] = "";
char falaAtual[176]  = "";

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
    else if ((*p & 0xF0) == 0xE0) { p += (p[1] && p[2]) ? 3 : 1; }
    else { p += (p[1] && p[2] && p[3]) ? 4 : 1; }
  }
  out[o] = 0;
}

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
        uint16_t px = ((uint16_t)linhaBruta[x * 2] << 8) | linhaBruta[x * 2 + 1];
#if TELA_INVERTE_CORES
        px ^= 0xFFFF;
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
//  Desenho (tudo na sombra; enviar com enviarRegiao/flush)
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
  textoCentralizado("STATUS DO DIA DE LYLU", TELA_W / 2, 9, 2, cor(255, 255, 255));
}

#define TAREFA_Y0   76
#define TAREFA_ALT  30

void desenharTarefa(int i) {
  int x0 = LADO_LYLU_W + 3;
  int y  = TAREFA_Y0 + i * TAREFA_ALT;
  uint16_t c = TAREFAS[i].feita ? cor(126, 217, 87) : cor(232, 232, 238);

  gfx->fillRect(x0 + 6, y - 3, TELA_W - x0 - 10, TAREFA_ALT - 4, cor(22, 22, 30));

  gfx->drawRect(x0 + 10, y, 14, 14, c);
  if (TAREFAS[i].feita) {
    gfx->drawLine(x0 + 12, y + 2,  x0 + 22, y + 12, c);
    gfx->drawLine(x0 + 22, y + 2,  x0 + 12, y + 12, c);
  }

  gfx->setTextSize(1);
  gfx->setTextColor(c);
  gfx->setCursor(x0 + 32, y + 3);
  gfx->print(TAREFAS[i].texto);

  enviarRegiao(x0 + 6, y - 3, TELA_W - x0 - 10, TAREFA_ALT - 4);
}

void desenharTarefas() {
  int x0 = LADO_LYLU_W + 3;
  gfx->fillRect(x0, TITULO_H, TELA_W - x0, TELA_H - TITULO_H, cor(22, 22, 30));

  gfx->setTextSize(2);
  gfx->setTextColor(cor(232, 232, 238));
  gfx->setCursor(x0 + 10, TITULO_H + 10);
  gfx->print("Plano do Dia:");
  gfx->drawFastHLine(x0 + 10, TITULO_H + 28, 156, cor(232, 232, 238));

  for (int i = 0; i < TOTAL_TAREFAS; i++) {
    desenharTarefa(i);
  }
  enviarRegiao(x0, TITULO_H, TELA_W - x0, TELA_H - TITULO_H);
}

void tratarToque(int tx, int ty) {
  if (tx >= 475) return;              // filtro de toques fantasmas da borda
  if (tx < LADO_LYLU_W + 3) return;   // so vale o lado da lista
  if (ty < TAREFA_Y0 - 8) return;

  int i = (ty - (TAREFA_Y0 - 8)) / TAREFA_ALT;
  if (i < 0 || i >= TOTAL_TAREFAS) return;

  removerLyluDaTela();
  TAREFAS[i].feita = !TAREFAS[i].feita;
  desenharTarefa(i);
  Serial.printf("Tarefa '%s' -> %s\n", TAREFAS[i].texto,
                TAREFAS[i].feita ? "FEITA" : "pendente");
}

void desenharFala(const char *txt, int yInicio) {
  char copia[176];
  utf8ParaAscii(txt, copia, sizeof(copia));

  char linha[36] = "";
  int y = yInicio;
  for (char *w = strtok(copia, " "); w && y < TELA_H - 12; w = strtok(NULL, " ")) {
    if (strlen(w) > 32) w[32] = 0;
    if (linha[0] && strlen(linha) + 1 + strlen(w) > 32) {
      textoCentralizado(linha, LADO_LYLU_W / 2, y, 1, cor(220, 220, 225));
      y += 13;
      linha[0] = 0;
    }
    if (linha[0]) strlcat(linha, " ", sizeof(linha));
    strlcat(linha, w, sizeof(linha));
  }
  if (linha[0] && y < TELA_H - 12) {
    textoCentralizado(linha, LADO_LYLU_W / 2, y, 1, cor(220, 220, 225));
  }
}

void desenharPainelLylu(int idx) {
  Humor &h = HUMORES[idx];

  gfx->fillRect(0, TITULO_H, LADO_LYLU_W, TELA_H - TITULO_H, cor(h.r, h.g, h.b));
  gfx->fillRect(LADO_LYLU_W, TITULO_H, 3, TELA_H - TITULO_H, cor(70, 70, 80));

  // sem texto de humor; so a fala do n8n (parte de baixo do lado esquerdo)
  if (falaAtual[0]) desenharFala(falaAtual, TELA_H - 56);

  enviarRegiao(0, TITULO_H, LADO_LYLU_W + 3, TELA_H - TITULO_H);
}

void trocarHumor(int idx) {
  lyluEstaDesenhada = false;   // o envio do painel ja limpou a regiao
  humorAtual = idx;
  frameAtual = 0;

  Serial.printf("Humor -> %s\n", HUMORES[idx].nome);

  desenharPainelLylu(idx);

  framesOk = carregarHumor(idx);
  if (framesOk) {
    acaoAtual = MOSTRANDO_EMOCAO;
    fimDaAcao = millis() + MS_PAUSA_EMOCAO;
  } else {
    textoCentralizado("SEM FRAMES", LADO_LYLU_W / 2, 120, 2, cor(255, 80, 80));
    enviarRegiao(0, 100, LADO_LYLU_W, 50);
  }
}

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
//  Supabase no nucleo 0
// ============================================================
void tarefaSupabase(void *pv) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure clienteSeguro;
      clienteSeguro.setInsecure();

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
    trocarHumor(idx);
  } else if (falaMudou) {
    desenharPainelLylu(humorAtual);
    lyluEstaDesenhada = false;
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Lylu JC4827W543 ===");

  // luz de fundo com controle de brilho
  ledcAttach(GFX_BL, 5000, 8);
  luzTela(255);

  // canvas->begin tambem inicializa o display por dentro
  if (!gfx->begin()) {
    Serial.println("ERRO: canvas/display nao inicializou (PSRAM ligada?)");
    return;
  }

  touch.begin();

  gfx->fillScreen(cor(22, 22, 30));
  desenharTitulo();
  gfx->flush();

  // ---- PSRAM: humor + 4 caminhadas + pensando + apontando ----
  size_t bytesAnimacao = (size_t)NUM_FRAMES * FRAME_PX * 2;
  frames = (uint16_t *)ps_malloc(bytesAnimacao);
  for (int d = 0; d < TOTAL_DIRECOES; d++) {
    framesAndando[d] = (uint16_t *)ps_malloc(bytesAnimacao);
  }
  framesPensando = (uint16_t *)ps_malloc(bytesAnimacao);
  framesApontando = (uint16_t *)ps_malloc(bytesAnimacao);
  composicao = (uint16_t *)heap_caps_malloc((size_t)FRAME_PX * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool memoriaOk = frames && framesPensando && framesApontando && composicao;
  for (int d = 0; d < TOTAL_DIRECOES; d++) memoriaOk = memoriaOk && framesAndando[d];
  if (!memoriaOk) {
    Serial.println("ERRO: sem memoria. Ative 'PSRAM: OPI PSRAM' nas opcoes da placa.");
    textoCentralizado("SEM PSRAM", TELA_W / 2, 120, 3, cor(255, 80, 80));
    gfx->flush();
    return;
  }

  // ---- LittleFS ----
  if (!LittleFS.begin(false)) {
    Serial.println("ERRO: LittleFS nao montou (faltou gravar as imagens).");
    textoCentralizado("SEM IMAGENS", TELA_W / 2, 120, 3, cor(255, 80, 80));
    textoCentralizado("Falta gravar o littlefs.bin em 0x410000", TELA_W / 2, 160, 1, cor(230, 230, 235));
    gfx->flush();
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
  if (!caminhadasOk) Serial.println("AVISO: falta alguma caminhada direcional");
  if (!pensandoOk)   Serial.println("AVISO: faltam pensando00-11.bin");
  if (!apontandoOk)  Serial.println("AVISO: faltam apontandolista00-11.bin");

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
  xTaskCreatePinnedToCore(tarefaSupabase, "supabase", 12288, NULL, 1, NULL, 0);
}

void loop() {
  if (!frames || !composicao) return;

  unsigned long agora = millis();

  if (!modoTeste && (framesOk || caminhadasOk || pensandoOk || apontandoOk)
      && agora - ultimoFrame >= MS_POR_FRAME) {
    ultimoFrame = agora;
    desenharProximoFrameMovel(agora);
  }

  if (remotoNovo) {
    acordarTela();
    aplicarEstadoRemoto();
  }

  unsigned long parada = agora - ultimaAtividade;
  if (estadoIdle == 0 && parada >= MIN_ATE_ENTEDIADA * 60000UL) {
    estadoIdle = 1;
    humorAntes = humorAtual;
    trocarHumor(5);
  } else if (estadoIdle == 1 && parada >= MIN_ATE_DORMIR * 60000UL) {
    estadoIdle = 2;
    trocarHumor(4);
    luzTela(50);
  } else if (estadoIdle == 2 && parada >= MIN_ATE_APAGAR * 60000UL) {
    estadoIdle = 3;
    luzTela(0);
    Serial.println("Tela apagada (zzz)");
  }

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
        acordarTela();
      } else {
        ultimaAtividade = agora;
        tratarToque(tx, ty);
      }
    } else if (!touch.isTouched) {
      tocando = false;
    }
  }

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
      gfx->flush();
      Serial.println(c == 'b' ? "Teste: tela BRANCA" : "Teste: tela PRETA");
    } else if (c == 'v') {
      modoTeste = false;
      gfx->fillScreen(cor(22, 22, 30));
      desenharTitulo();
      desenharTarefas();
      desenharPainelLylu(humorAtual);
      gfx->flush();
      acordarTela();
      Serial.println("Teste encerrado, tela normal");
    }
  }
}
