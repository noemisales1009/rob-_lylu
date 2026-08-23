// ============================================================
//  TESTE DO CARTAO SD NA PLACA 4848 (ESP32-4848S040C)
//
//  Esta placa tem pinos de SD BEM diferentes do padrao:
//     CS=42, CLK=48, MOSI=47, MISO=41
//  (o CLK e o MOSI sao COMPARTILHADOS com o display!)
//
//  Por isso testamos com o display ainda desligado.
//  O resultado sai na serial E na tela.
// ============================================================

#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <SD.h>

#define GFX_BL 38
#define TELA_INVERTE_CORES 1

// Pinos do SD desta placa (fonte: homeding.github.io/boards/esp32s3)
#define SD_CS    42
#define SD_CLK   48
#define SD_MOSI  47
#define SD_MISO  41

SPIClass spiSD(HSPI);
int8_t gCS = SD_CS;

struct Res { int8_t clk, miso, mosi, cs; uint8_t resp; bool ok; };
Res testes[] = {
  { 48, 41, 47, 42, 0xFF, false },   // documentado para esta placa
  { 48, 47, 41, 42, 0xFF, false },   // dados invertidos
  { 12, 13, 11, 10, 0xFF, false },   // padrao comum, por garantia
  { 12, 11, 13, 10, 0xFF, false },
};
const int N = sizeof(testes) / sizeof(testes[0]);
bool montou = false;
uint64_t tamanhoMB = 0;
int nArquivos = 0;

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

void rodar(Res &t) {
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
    if (r != 0xFF) t.resp = r;
    if (r == 0x01) t.ok = true;
    else delay(25);
  }
  spiSD.endTransaction();

  Serial.printf("  CLK=%2d MISO=%2d MOSI=%2d CS=%2d -> 0x%02X %s\n",
                t.clk, t.miso, t.mosi, t.cs, t.resp, t.ok ? "<<< ACHOU!" : "");

  // se o cartao respondeu, tenta montar de verdade
  if (t.ok && !montou) {
    spiSD.end(); delay(20);
    spiSD.begin(t.clk, t.miso, t.mosi, t.cs);
    pinMode(t.miso, INPUT_PULLUP);
    if (SD.begin(t.cs, spiSD, 4000000)) {
      montou = true;
      tamanhoMB = SD.cardSize() / (1024ULL * 1024);
      File raiz = SD.open("/");
      for (File f = raiz.openNextFile(); f; f = raiz.openNextFile()) {
        nArquivos++; f.close();
        if (nArquivos > 400) break;
      }
      raiz.close();
      Serial.printf("  >>> MONTOU! %llu MB, %d arquivos\n", tamanhoMB, nArquivos);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\n=== TESTE DO SD NA PLACA 4848 ===");
  Serial.println("Pinos desta placa: CS=42 CLK=48 MOSI=47 MISO=41\n");

  // 1) testa o SD ANTES de ligar o display (eles dividem pinos!)
  for (int i = 0; i < N; i++) rodar(testes[i]);

  bool algum = montou;
  for (int i = 0; i < N; i++) if (testes[i].ok) algum = true;

  Serial.println(algum ? "\nCARTAO RESPONDEU!" : "\nCartao nao respondeu.");

  // 2) so agora liga o display, pra mostrar o resultado
  spiSD.end();
  delay(50);

  Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
  Arduino_ESP32RGBPanel *rgb = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21, 11, 12, 13, 14, 0, 8, 20, 3, 46, 9, 10,
    4, 5, 6, 7, 15, 1, 10, 8, 20, 1, 10, 8, 10,
    0, 12000000L, false, 0, 0, 40 * 480);
  Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgb, 0, true, bus, GFX_NOT_DEFINED,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

  if (!gfx->begin()) { Serial.println("display falhou"); return; }
  bus->beginWrite();
  bus->writeCommand(0xFF);
  bus->write(0x77); bus->write(0x01);
  bus->write(0x00); bus->write(0x00); bus->write(0x10);
  bus->writeC8D8(0xCD, 0x00);
  bus->endWrite();
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  // cores invertidas nesta tela
  auto cor = [&](uint8_t r, uint8_t g, uint8_t b) {
    return gfx->color565(255 - r, 255 - g, 255 - b);
  };

  gfx->fillScreen(cor(20, 20, 28));
  gfx->setTextSize(3);
  gfx->setTextColor(algum ? cor(120, 230, 120) : cor(255, 90, 90));
  gfx->setCursor(20, 30);
  gfx->print(algum ? "SD FUNCIONA!" : "SD NAO RESPONDE");

  gfx->setTextSize(1);
  gfx->setTextColor(cor(235, 235, 240));
  gfx->setCursor(20, 90);
  gfx->print("CLK MISO MOSI  CS  | resposta");
  gfx->drawFastHLine(20, 100, 300, cor(235, 235, 240));

  int y = 112;
  for (int i = 0; i < N; i++) {
    Res &t = testes[i];
    gfx->setTextColor(t.ok ? cor(120, 230, 120) : cor(200, 200, 205));
    gfx->setCursor(20, y);
    gfx->printf(" %2d   %2d   %2d   %2d  |  0x%02X %s",
                t.clk, t.miso, t.mosi, t.cs, t.resp, t.ok ? "<<< ESTE" : "");
    y += 16;
  }

  y += 16;
  gfx->setTextSize(2);
  if (montou) {
    gfx->setTextColor(cor(120, 230, 120));
    gfx->setCursor(20, y);
    gfx->printf("Cartao de %llu MB", tamanhoMB);
    gfx->setCursor(20, y + 26);
    gfx->printf("%d arquivos dentro", nArquivos);
    gfx->setTextSize(1);
    gfx->setTextColor(cor(235, 235, 240));
    gfx->setCursor(20, y + 60);
    gfx->print("Espaco ilimitado para a Lylu! :)");
  } else if (algum) {
    gfx->setTextColor(cor(255, 200, 80));
    gfx->setCursor(20, y);
    gfx->print("Responde mas nao monta");
    gfx->setTextSize(1);
    gfx->setCursor(20, y + 30);
    gfx->print("(formatar em FAT32)");
  } else {
    gfx->setTextSize(1);
    gfx->setTextColor(cor(235, 235, 240));
    gfx->setCursor(20, y);
    gfx->print("0xFF = silencio. Sem cartao ou slot sem contato.");
    gfx->setCursor(20, y + 16);
    gfx->print("Confira se o cartao esta encaixado ate clicar.");
  }
}

void loop() { delay(3000); }
