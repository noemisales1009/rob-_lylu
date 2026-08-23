// ============================================================
//  TESTE 7 - resultado NA TELA (sem depender da serial)
//
//  Testa os pinos candidatos ao cartao com o display desligado,
//  guarda os resultados, e SO DEPOIS liga a tela para mostrar.
// ============================================================

#include <Arduino_GFX_Library.h>
#include <SPI.h>

#define GFX_BL 1

SPIClass spiSD(HSPI);
int8_t gCS;

struct Teste { int8_t clk, miso, mosi, cs; uint8_t resposta; bool ok; };

Teste testes[] = {
  { 12, 13, 11,  4, 0xFF, false },
  { 12, 13, 11, 45, 0xFF, false },
  { 12, 13, 11, 10, 0xFF, false },
  { 12, 11, 13,  4, 0xFF, false },
  { 12, 11, 13, 45, 0xFF, false },
  { 12, 11, 13, 10, 0xFF, false },
};
const int N = sizeof(testes) / sizeof(testes[0]);

uint8_t cmd0() {
  digitalWrite(gCS, LOW);
  spiSD.transfer(0x40); spiSD.transfer(0); spiSD.transfer(0);
  spiSD.transfer(0);    spiSD.transfer(0); spiSD.transfer(0x95);
  uint8_t r = 0xFF;
  for (int i = 0; i < 12 && r == 0xFF; i++) r = spiSD.transfer(0xFF);
  digitalWrite(gCS, HIGH);
  spiSD.transfer(0xFF);
  return r;
}

void rodar(Teste &t) {
  gCS = t.cs;
  spiSD.end();
  delay(15);
  spiSD.begin(t.clk, t.miso, t.mosi, t.cs);
  pinMode(t.cs, OUTPUT);
  digitalWrite(t.cs, HIGH);
  pinMode(t.miso, INPUT_PULLUP);
  delay(15);

  spiSD.beginTransaction(SPISettings(300000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 12; i++) spiSD.transfer(0xFF);

  for (int i = 0; i < 4 && !t.ok; i++) {
    uint8_t r = cmd0();
    if (r != 0xFF) t.resposta = r;
    if (r == 0x01) t.ok = true;
    else delay(25);
  }
  spiSD.endTransaction();
}

void setup() {
  Serial.begin(115200);

  // ---- 1) testa o cartao com a tela AINDA desligada ----
  for (int i = 0; i < N; i++) rodar(testes[i]);

  // libera o SPI antes de ligar o display (pode compartilhar pinos)
  spiSD.end();
  delay(50);

  // ---- 2) liga a tela e mostra o resultado ----
  Arduino_DataBus *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
  Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);
  if (!gfx->begin()) return;

  ledcAttach(GFX_BL, 5000, 8);
  ledcWrite(GFX_BL, 255);

  // esta tela mostra as cores invertidas: preto de verdade = branco aqui
  gfx->fillScreen(RGB565_WHITE);

  bool algum = false;
  for (int i = 0; i < N; i++) if (testes[i].ok) algum = true;

  gfx->setTextSize(2);
  gfx->setTextColor(algum ? RGB565_MAGENTA : RGB565_CYAN);  // invertido: verde / vermelho
  gfx->setCursor(8, 8);
  gfx->print(algum ? "CARTAO ENCONTRADO!" : "CARTAO NAO RESPONDE");

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_BLACK);   // invertido: branco
  gfx->setCursor(8, 36);
  gfx->print("CLK MISO MOSI  CS  | resposta ao CMD0");
  gfx->drawFastHLine(8, 46, 300, RGB565_BLACK);

  int y = 54;
  for (int i = 0; i < N; i++) {
    Teste &t = testes[i];
    gfx->setTextColor(t.ok ? RGB565_MAGENTA : RGB565_BLACK);
    gfx->setCursor(8, y);
    gfx->printf(" %2d   %2d   %2d   %2d  |  0x%02X  %s",
                t.clk, t.miso, t.mosi, t.cs, t.resposta,
                t.ok ? "<<< ESTE!" : (t.resposta == 0xFF ? "(mudo)" : ""));
    y += 14;
  }

  y += 10;
  gfx->setTextColor(RGB565_BLACK);
  gfx->setCursor(8, y);
  if (algum) {
    gfx->print("Anote a linha marcada e me diga!");
  } else {
    gfx->print("0xFF = silencio total (nada conectado)");
    gfx->setCursor(8, y + 14);
    gfx->print("0x01 seria a resposta correta do cartao");
  }
}

void loop() { delay(5000); }
