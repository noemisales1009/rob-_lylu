// Lylu na Guition ESP32-P4 de 7" — as seis telas do protótipo (docs/telas-da-lylu.md).
// As tarefas ainda são de exemplo; o Supabase vem depois. O Wi-Fi já é de verdade,
// e se configura na própria tela (ver rede.c e a seção WI-FI aqui embaixo).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "gif/lv_gif_private.h"   // copia corrigida do GIF da LVGL (ver gif/gifdec.c)
#include "audio.h"
#include "placa.h"
#include "rede.h"

static const char *TAG = "lylu";

// ---------------------------------------------------------------- cores
// Verde-noite: fundo escuro puxado pro verde; cartões e balão um tom acima;
// botões no oliva. O fundo é liso de propósito: a tela tem 16 bits de cor, e
// um degradê nesses tons escuros vira faixas verticais de cores diferentes.
#define C_FUNDO   lv_color_hex(0x112119)
#define C_CARTAO  lv_color_hex(0x182d25)
#define C_BALAO   lv_color_hex(0x1f3a30)
#define C_LINHA   lv_color_hex(0x2b4a3e)
#define C_TINTA   lv_color_hex(0xeef3ec)
#define C_FRACO   lv_color_hex(0x93a99c)
#define C_OLIVA   lv_color_hex(0xbfd384)
#define C_AMBAR   lv_color_hex(0xf4b860)
#define C_CEU     lv_color_hex(0x8ec5ff)
#define C_LILAS   lv_color_hex(0xb9a6ff)
#define C_ESCURO  lv_color_hex(0x1b2612)

// ---------------------------------------------------------------- arquivos embutidos
#define EMB(nome) extern const uint8_t nome##_start[] asm("_binary_" #nome "_start"); \
                  extern const uint8_t nome##_end[]   asm("_binary_" #nome "_end");
EMB(concentrada_frente_gif) EMB(comemorando_v2_gif) EMB(pensando_gif) EMB(tomando_cafe_frente_gif)
EMB(apontando_lista_gif) EMB(cuidadora_v2_gif) EMB(rindo_carinho_frente_gif)
EMB(andando_direita_v2_gif) EMB(andando_esquerda_v2_gif) EMB(lendo_frente_gif)
EMB(soprando_bolhas_frente_gif) EMB(brincando_frente_gif) EMB(bocejando_frente_gif)
EMB(dormindo_frente_gif) EMB(tomando_agua_frente_gif) EMB(jogando_nintendo_frente_gif)
EMB(lylu_fala_gif)
EMB(corpo_ttf) EMB(corpo_negrito_ttf) EMB(pixel_ttf)

typedef enum {
    A_CONCENTRADA, A_COMEMORANDO, A_PENSANDO, A_CAFE, A_APONTANDO, A_CUIDADORA, A_RINDO,
    A_ANDA_DIR, A_ANDA_ESQ, A_LENDO, A_BOLHAS, A_BRINCANDO, A_BOCEJANDO, A_DORMINDO,
    A_AGUA, A_NINTENDO, A_FALA, A_TOTAL
} anim_t;

static lv_image_dsc_t GIF[A_TOTAL];

static void registra_gif(anim_t a, const uint8_t *ini, const uint8_t *fim)
{
    GIF[a] = (lv_image_dsc_t){ .data = ini, .data_size = fim - ini };
}
#define REG(a, nome) registra_gif(a, nome##_start, nome##_end)

static void carrega_gifs(void)
{
    REG(A_CONCENTRADA, concentrada_frente_gif); REG(A_COMEMORANDO, comemorando_v2_gif);
    REG(A_PENSANDO, pensando_gif); REG(A_CAFE, tomando_cafe_frente_gif);
    REG(A_APONTANDO, apontando_lista_gif); REG(A_CUIDADORA, cuidadora_v2_gif);
    REG(A_RINDO, rindo_carinho_frente_gif); REG(A_ANDA_DIR, andando_direita_v2_gif);
    REG(A_ANDA_ESQ, andando_esquerda_v2_gif); REG(A_LENDO, lendo_frente_gif);
    REG(A_BOLHAS, soprando_bolhas_frente_gif); REG(A_BRINCANDO, brincando_frente_gif);
    REG(A_BOCEJANDO, bocejando_frente_gif); REG(A_DORMINDO, dormindo_frente_gif);
    REG(A_AGUA, tomando_agua_frente_gif); REG(A_NINTENDO, jogando_nintendo_frente_gif);
    REG(A_FALA, lylu_fala_gif);
}

// ---------------------------------------------------------------- fontes
static lv_font_t *f_corpo, *f_corpo_p, *f_corpo_g, *f_negrito, *f_negrito_p, *f_negrito_m, *f_negrito_g, *f_px_p, *f_px_m, *f_relogio, *f_px_foco;

static lv_font_t *ttf(const uint8_t *ini, const uint8_t *fim, int tam)
{
    return lv_tiny_ttf_create_data_ex(ini, fim - ini, tam, LV_FONT_KERNING_NONE, 256);
}

static void carrega_fontes(void)
{
    f_corpo_p = ttf(corpo_ttf_start, corpo_ttf_end, 17);
    f_corpo   = ttf(corpo_ttf_start, corpo_ttf_end, 20);
    f_corpo_g = ttf(corpo_ttf_start, corpo_ttf_end, 30);
    f_negrito = ttf(corpo_negrito_ttf_start, corpo_negrito_ttf_end, 22);
    f_negrito_p = ttf(corpo_negrito_ttf_start, corpo_negrito_ttf_end, 17);
    f_negrito_m = ttf(corpo_negrito_ttf_start, corpo_negrito_ttf_end, 27);
    f_negrito_g = ttf(corpo_negrito_ttf_start, corpo_negrito_ttf_end, 34);
    f_px_p    = ttf(pixel_ttf_start, pixel_ttf_end, 20);
    f_px_m    = ttf(pixel_ttf_start, pixel_ttf_end, 38);
    f_px_foco = ttf(pixel_ttf_start, pixel_ttf_end, 84);
    f_relogio = ttf(corpo_negrito_ttf_start, corpo_negrito_ttf_end, 128);
}

// ---------------------------------------------------------------- estado
typedef struct { const char *t; bool f; } tarefa_t;

static tarefa_t missao[] = { { "Enviar orçamento pro cliente", false }, { "Tomar o remédio", true } };
static tarefa_t bonus[]  = { { "Responder os e-mails", true }, { "Organizar a mesa", false },
                             { "Ler o capítulo 3", false }, { "Lavar a louça", false } };
#define N_MISSAO (sizeof(missao) / sizeof(missao[0]))
#define N_BONUS  (sizeof(bonus) / sizeof(bonus[0]))

static bool dificil = false, cortada = false;
static int64_t carinho_ate = 0;

// O pomodoro da tela da frente anda em quatro passos (ver POMODORO).
typedef enum { P_ESCOLHER, P_CONFIGURAR, P_FOCO, P_PAUSA } passo_t;
static passo_t passo = P_ESCOLHER;
typedef struct { uint8_t foco, curta, blocos, longa, sozinha; } pomo_t;   // minutos; "avançar sozinha"
static pomo_t pomo = { 25, 5, 4, 20, 1 };
static char tarefa_atual[64] = "O que você quiser";
static struct { bool rodando; int resto, bloco, feitos; } foco = { false, 25 * 60, 1, 0 };
static struct { bool rodando, longa; int resto, cuidado; } pausa = { false, false, 5 * 60, -1 };

enum { T_FOCO, T_TAREFAS, T_RECADOS, T_SEMANA, T_RELOGIO, T_CASA, T_AJUSTES, N_TELAS };   // a ordem do modelo
static int tela_atual = T_FOCO;

static int64_t agora_ms(void) { return esp_timer_get_time() / 1000; }
static int n_missao(void) { return cortada ? 1 : (int)N_MISSAO; }
static int missao_feita(void)
{
    int n = 0;
    for (int i = 0; i < n_missao(); i++) n += missao[i].f;
    return n;
}
static bool missao_completa(void) { return missao_feita() == n_missao(); }

// ---------------------------------------------------------------- objetos
static lv_obj_t *tv, *tiles[N_TELAS], *pontos[N_TELAS], *caixa_pontos;
static lv_obj_t *lylu, *lylu_outra;  // dois GIFs que se revezam; ver lylu_mostra()
static anim_t lylu_anim = A_TOTAL;
static lv_obj_t *st_hora, *st_missao, *st_humor, *st_humor_ponto, *st_gentil;
static lv_obj_t *tar_balao, *lista_missao, *lista_bonus, *caixa_bonus, *btn_cortar;
static lv_obj_t *aj_dificil;

static void atualizar(void);

// ---------------------------------------------------------------- utilidades de desenho
static lv_obj_t *caixa(lv_obj_t *pai)
{
    lv_obj_t *o = lv_obj_create(pai);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    // A LVGL deixa todo lv_obj clicável. Numa caixa que é só desenho, isso engole
    // o toque antes dele chegar na linha que tem o tratador — era por isso que
    // tocar em "Wi-Fi" nos Ajustes não fazia nada: a coluna de texto ficava por
    // cima da linha inteira. Quem precisa de toque pede o flag na mão.
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *texto(lv_obj_t *pai, const char *s, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(pai);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static lv_obj_t *balao(lv_obj_t *pai, int x, int y, int largura)
{
    lv_obj_t *b = caixa(pai);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, C_BALAO, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_pad_ver(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, 2, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    // A fala e, embaixo e em negrito, o pedaço que ela quer que fique.
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = texto(b, "", i ? f_negrito : f_corpo, C_TINTA);
        lv_obj_set_width(l, largura);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    // O biquinho, em degraus, logo abaixo da borda. Girar um quadrado ficaria mais
    // liso, mas objeto girado vira uma camada extra redesenhada a cada quadro — e
    // o arrastar entre as telas engasgava.
    lv_obj_t *bico = caixa(b);
    lv_obj_add_flag(bico, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(bico, 18, 15);
    lv_obj_align(bico, LV_ALIGN_BOTTOM_RIGHT, -60, 14 + 15);   // 14 = o respiro de baixo
    for (int i = 0; i < 3; i++) {
        lv_obj_t *d = caixa(bico);
        lv_obj_set_size(d, 18 - i * 6, 5);
        lv_obj_set_pos(d, i * 3, i * 5);
        lv_obj_set_style_bg_color(d, C_BALAO, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
    return b;
}

static void balao_fala(lv_obj_t *b, const char *s, const char *forte)
{
    if (!s || !*s) { lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lv_obj_get_child(b, 0), s);
    lv_obj_t *l = lv_obj_get_child(b, 1);
    if (forte && *forte) { lv_label_set_text(l, forte); lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
}

static void balao_texto(lv_obj_t *b, const char *s) { balao_fala(b, s, NULL); }

static lv_obj_t *cartao(lv_obj_t *pai)
{
    lv_obj_t *c = caixa(pai);
    lv_obj_set_style_bg_color(c, C_CARTAO, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, C_LINHA, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 14, 0);
    return c;
}

// Cabeçalho das telas: a etiqueta em oliva com o pontinho, o título e, se tiver,
// a linha de baixo. Filhos da coluna: 0 = linha da etiqueta (o texto é o filho 1
// dela), 1 = título, 2 = linha de baixo.
static lv_obj_t *cabeca(lv_obj_t *t, int x, const char *tag, const char *titulo, const char *sub, int largura)
{
    lv_obj_t *col = caixa(t);
    lv_obj_set_pos(col, x, 84);
    lv_obj_set_width(col, largura);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_t *linha = caixa(col);
    lv_obj_set_flex_flow(linha, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(linha, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(linha, 10, 0);
    lv_obj_t *ponto = caixa(linha);
    lv_obj_set_size(ponto, 6, 6);
    lv_obj_set_style_radius(ponto, 3, 0);
    lv_obj_set_style_bg_color(ponto, C_OLIVA, 0);
    lv_obj_set_style_bg_opa(ponto, LV_OPA_COVER, 0);
    texto(linha, tag, f_negrito_p, C_OLIVA);
    if (titulo) texto(col, titulo, f_negrito_g, C_TINTA);
    if (sub) {
        lv_obj_t *l = texto(col, sub, f_corpo_p, C_FRACO);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    return col;
}

static lv_obj_t *pilula(lv_obj_t *pai, const char *s, lv_color_t cor)
{
    lv_obj_t *p = caixa(pai);
    lv_obj_set_style_bg_color(p, C_LINHA, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 99, 0);
    lv_obj_set_style_pad_hor(p, 10, 0);
    lv_obj_set_style_pad_ver(p, 3, 0);
    texto(p, s, f_corpo_p, cor);
    return p;
}

static lv_obj_t *botao(lv_obj_t *pai, const char *s, bool principal, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(pai);
    lv_obj_set_style_bg_color(b, principal ? C_OLIVA : C_CARTAO, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 26, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_t *l = texto(b, s, f_negrito, principal ? C_ESCURO : C_TINTA);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

// ---------------------------------------------------------------- o sinal do Wi-Fi
// Quatro barrinhas que contam o estado sem precisar ler: entrando, elas enchem
// uma a uma, em ciclo; conectada, mostram a força do sinal em verde; se a rede
// sumiu ou a senha não bateu, a primeira pisca em âmbar. Todas as cópias (barra
// de cima, Ajustes, tela do Wi-Fi) andam juntas, pintadas por um timer só.
#define MAX_SINAIS 4
static lv_obj_t *sinais[MAX_SINAIS];
static int n_sinais;

static lv_obj_t *sinal_cria(lv_obj_t *pai)
{
    lv_obj_t *b = caixa(pai);
    lv_obj_set_size(b, 4 * 5 + 3 * 3, 20);
    for (int i = 0; i < 4; i++) {
        int h = 5 + i * 5;
        lv_obj_t *t = caixa(b);
        lv_obj_set_size(t, 5, h);
        lv_obj_set_pos(t, i * 8, 20 - h);
        lv_obj_set_style_radius(t, 2, 0);
        lv_obj_set_style_bg_color(t, C_LINHA, 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    }
    if (n_sinais < MAX_SINAIS) sinais[n_sinais++] = b;
    return b;
}

static void sinal_pinta(lv_timer_t *t)
{
    static int passo, antes_cheias = -1;
    static lv_color_t antes_cor;
    passo = (passo + 1) % 5;

    int cheias = 0;
    lv_color_t cor = C_TINTA;
    switch (rede_estado()) {
    case REDE_PROCURANDO:
    case REDE_CONECTANDO:   cheias = passo; break;
    case REDE_CONECTADA:    cheias = rede_sinal() ? rede_sinal() : 4; cor = C_OLIVA; break;
    case REDE_SUMIU:
    case REDE_SENHA_ERRADA: cheias = passo % 2; cor = C_AMBAR; break;
    default: break;
    }
    // Parada no mesmo desenho, não pinta de novo (a tela só redesenha o que mudou).
    if (cheias == antes_cheias && lv_color_eq(cor, antes_cor)) return;
    antes_cheias = cheias;
    antes_cor = cor;
    for (int s = 0; s < n_sinais; s++)
        for (int i = 0; i < 4; i++)
            lv_obj_set_style_bg_color(lv_obj_get_child(sinais[s], i), i < cheias ? cor : C_LINHA, 0);
}

// ---------------------------------------------------------------- a Lylu
// O decodificador de GIF da LVGL começa o quadro pintando a cor de fundo OPACA e só
// a torna transparente onde um quadro já foi apagado — sobrava um quadrado preto
// em volta da Lylu. Os GIFs usam magenta (índice 255) como fundo e transparência,
// então basta zerar a opacidade de todo pixel magenta ao abrir cada animação.
static void limpa_fundo_gif(lv_obj_t *obj)
{
    lv_gif_t *g = (lv_gif_t *)obj;
    if (!g->gif) return;
    uint8_t *px = g->gif->canvas;   // BGRA
    for (int i = 0, n = g->gif->width * g->gif->height; i < n; i++, px += 4)
        if (px[0] == 0xff && px[1] == 0x00 && px[2] == 0xff) px[3] = 0;
    lv_image_cache_drop(&g->imgdsc);
    lv_obj_invalidate(obj);
}

// Trocar o GIF na hora corta a pose no meio e fica seco. Por isso são duas: a que
// está saindo fica INTEIRA por baixo até o fim, enquanto a que entra aparece por
// cima. Esmaecer as duas ao mesmo tempo abriria um buraco no meio da passagem —
// daria pra ver o fundo através das duas — e é justamente isso que parece piscada.
#define TROCA_MS 160

static void opacidade(void *o, int32_t v) { lv_obj_set_style_image_opa(o, v, 0); }

static void guarda_a_que_saiu(void)
{
    lv_obj_add_flag(lylu_outra, LV_OBJ_FLAG_HIDDEN);
    lv_gif_pause(lylu_outra);      // escondida não precisa gastar CPU decodificando
}

static void fim_da_troca(lv_anim_t *a) { guarda_a_que_saiu(); }

static void loga_memoria(const char *quando)
{
    ESP_LOGI(TAG, "memoria (%s): interna %u livre / %u maior bloco, psram %u livre / %u maior bloco", quando,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

static void lylu_mostra(anim_t a)
{
    if (carinho_ate > agora_ms()) a = A_RINDO;
    if (a == lylu_anim) return;
    anim_t antes = lylu_anim;
    bool primeira = antes == A_TOTAL;
    lylu_anim = a;

    if (!primeira) {
        // Se a passagem anterior ainda estava correndo, ela acaba agora — assim
        // as duas nunca ficam no meio do caminho ao mesmo tempo.
        lv_anim_delete(lylu, opacidade);
        lv_obj_set_style_image_opa(lylu, LV_OPA_COVER, 0);
        guarda_a_que_saiu();
        lv_obj_t *saindo = lylu;
        lylu = lylu_outra;
        lylu_outra = saindo;       // as duas já estão no mesmo lugar (ver lylu_em)
    }

    lv_gif_set_src(lylu, &GIF[a]);
    // Cada GIF aberto pede uns 650 KB de uma vez. Se não vier, a LVGL só avisa e
    // deixa o objeto vazio — e como lylu_anim já dizia a pose nova, ninguém tentava
    // de novo: ela sumia de vez. Agora a troca é desfeita (a que ia sair continua
    // na tela) e a próxima chamada tenta outra vez.
    if (!((lv_gif_t *)lylu)->gif) {
        ESP_LOGW(TAG, "GIF %d nao abriu", (int)a);
        loga_memoria("gif falhou");
        lylu_anim = antes;
        if (!primeira) {
            lv_obj_t *falhou = lylu;
            lylu = lylu_outra;
            lylu_outra = falhou;
        }
        return;
    }
    lv_gif_resume(lylu);
    limpa_fundo_gif(lylu);
    lv_obj_remove_flag(lylu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lylu);

    if (primeira) { lv_obj_set_style_image_opa(lylu, LV_OPA_COVER, 0); return; }

    lv_obj_set_style_image_opa(lylu, LV_OPA_TRANSP, 0);
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, lylu);
    lv_anim_set_values(&an, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&an, TROCA_MS);
    lv_anim_set_exec_cb(&an, opacidade);
    lv_anim_set_completed_cb(&an, fim_da_troca);
    lv_anim_start(&an);
}

// As duas andam sempre juntas: na hora da troca, a que entra já está no lugar certo.
static void lylu_em(lv_obj_t *pai, int x, int y)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_t *g = i ? lylu_outra : lylu;
        if (lv_obj_get_parent(g) != pai) lv_obj_set_parent(g, pai);
        lv_obj_set_pos(g, x, y);
        lv_obj_add_flag(g, LV_OBJ_FLAG_CLICKABLE);   // a senha do Wi-Fi tira; aqui volta
    }
    lv_obj_move_foreground(lylu);
}

static void lylu_na_tela(int t, int x, int y) { lylu_em(tiles[t], x, y); }

static void fim_carinho(lv_timer_t *t) { atualizar(); }

static void ev_carinho(lv_event_t *e)
{
    carinho_ate = agora_ms() + 2600;
    atualizar();
    lv_timer_t *t = lv_timer_create(fim_carinho, 2700, NULL);
    lv_timer_set_repeat_count(t, 1);
}

// ---------------------------------------------------------------- barra de status
static void cria_status(void)
{
    lv_obj_t *bar = caixa(lv_layer_top());
    lv_obj_set_size(bar, TELA_W, 44);
    lv_obj_set_style_pad_hor(bar, 22, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 18, 0);

    st_hora = texto(bar, "--:--", f_px_p, C_TINTA);
    lv_obj_t *m = texto(bar, "Missão", f_corpo_p, C_FRACO);
    (void)m;
    st_missao = texto(bar, "0/2", f_negrito, C_AMBAR);
    lv_obj_set_style_margin_left(st_missao, -10, 0);
    st_gentil = texto(bar, "modo gentil", f_corpo_p, C_CEU);

    lv_obj_t *esp = caixa(bar);
    lv_obj_set_flex_grow(esp, 1);

    sinal_cria(bar);

    st_humor = caixa(bar);
    lv_obj_set_style_bg_color(st_humor, C_CARTAO, 0);
    lv_obj_set_style_bg_opa(st_humor, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(st_humor, 99, 0);
    lv_obj_set_style_pad_hor(st_humor, 12, 0);
    lv_obj_set_style_pad_ver(st_humor, 4, 0);
    lv_obj_set_flex_flow(st_humor, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(st_humor, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(st_humor, 8, 0);
    st_humor_ponto = caixa(st_humor);
    lv_obj_set_size(st_humor_ponto, 10, 10);
    lv_obj_set_style_radius(st_humor_ponto, 5, 0);
    lv_obj_set_style_bg_opa(st_humor_ponto, LV_OPA_COVER, 0);
    texto(st_humor, "animada", f_corpo_p, C_TINTA);
}

static void ev_ponto(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_tileview_set_tile_by_index(tv, i, 0, LV_ANIM_ON);
}

static void cria_pontos(void)
{
    lv_obj_t *p = caixa_pontos = caixa(lv_layer_top());
    lv_obj_align(p, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p, 10, 0);
    lv_obj_set_style_pad_all(p, 6, 0);
    for (int i = 0; i < N_TELAS; i++) {
        pontos[i] = caixa(p);
        lv_obj_set_size(pontos[i], 10, 10);
        lv_obj_set_style_radius(pontos[i], 5, 0);
        lv_obj_set_style_bg_opa(pontos[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(pontos[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(pontos[i], 10);
        lv_obj_add_event_cb(pontos[i], ev_ponto, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// ---------------------------------------------------------------- o teclado
// O teclado pronto da LVGL marca as teclas de comando com símbolos da fonte
// Montserrat, que as fontes TTF da Lylu não têm — sairia um quadradinho vazio.
// Então o mapa é nosso, escrito em português, num lv_buttonmatrix puro.
#define T_SO_UM LV_BUTTONMATRIX_CTRL_NO_REPEAT
#define T_VERDE LV_BUTTONMATRIX_CTRL_CHECKED

static const char *TECLAS_MIN[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","Apagar","\n",
    "ABC","z","x","c","v","b","n","m",".","-","\n",
    "#+="," ","Cancelar","Conectar","" };

static const char *TECLAS_MAI[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","Apagar","\n",
    "abc","Z","X","C","V","B","N","M","_","+","\n",
    "#+="," ","Cancelar","Conectar","" };

static const char *TECLAS_SIMB[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "!","@","#","$","%","&","*","(",")","_","\n",
    "-","+","=","/","\\",":",";","?","^","Apagar","\n",
    "\"","'","[","]","{","}","<",">","|","~","\n",
    "abc"," ","Cancelar","Conectar","" };

static const lv_buttonmatrix_ctrl_t TECLAS_LARGURA[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 2,            // "Apagar" ocupa duas
    1 | T_SO_UM, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // a tecla de trocar o mapa
    2 | T_SO_UM, 4, 2 | T_SO_UM, 3 | T_SO_UM | T_VERDE };

#define N_TECLAS (sizeof TECLAS_MIN / sizeof *TECLAS_MIN)

// Cada teclado tem a sua cópia dos mapas porque a tecla verde diz o que vai
// acontecer: "Conectar" no Wi-Fi, "Usar esta" na tarefa.
typedef struct {
    lv_obj_t *bm, *campo;
    const char *ok_txt;
    const char *mapa[3][N_TECLAS];
    void (*ok)(void), (*cancela)(void);
} teclado_t;

static void teclado_mapa(teclado_t *tc, int m)
{
    lv_buttonmatrix_set_map(tc->bm, tc->mapa[m]);
    lv_buttonmatrix_set_ctrl_map(tc->bm, TECLAS_LARGURA);   // o set_map zera as larguras
}

static void ev_tecla(lv_event_t *e)
{
    teclado_t *tc = lv_event_get_user_data(e);
    uint32_t i = lv_buttonmatrix_get_selected_button(tc->bm);
    const char *t = lv_buttonmatrix_get_button_text(tc->bm, i);
    if (!t) return;
    if      (!strcmp(t, "ABC"))      teclado_mapa(tc, 1);
    else if (!strcmp(t, "abc"))      teclado_mapa(tc, 0);
    else if (!strcmp(t, "#+="))      teclado_mapa(tc, 2);
    else if (!strcmp(t, "Apagar"))   lv_textarea_delete_char(tc->campo);
    else if (!strcmp(t, "Cancelar")) tc->cancela();
    else if (!strcmp(t, tc->ok_txt)) tc->ok();
    else lv_textarea_add_text(tc->campo, t);
}

static void teclado_cria(teclado_t *tc, lv_obj_t *pai, int y, const char *ok_txt)
{
    const char **base[3] = { TECLAS_MIN, TECLAS_MAI, TECLAS_SIMB };
    tc->ok_txt = ok_txt;
    for (int m = 0; m < 3; m++)
        for (size_t k = 0; k < N_TECLAS; k++)
            tc->mapa[m][k] = strcmp(base[m][k], "Conectar") ? base[m][k] : ok_txt;

    lv_obj_t *bm = tc->bm = lv_buttonmatrix_create(pai);
    lv_obj_set_size(bm, TELA_W, 268);
    lv_obj_set_pos(bm, 0, y);
    lv_buttonmatrix_set_one_checked(bm, false);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_pad_all(bm, 8, 0);
    lv_obj_set_style_pad_row(bm, 8, 0);
    lv_obj_set_style_pad_column(bm, 8, 0);
    lv_obj_set_style_bg_color(bm, C_CARTAO, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(bm, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(bm, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, C_TINTA, LV_PART_ITEMS);
    lv_obj_set_style_text_font(bm, f_corpo, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bm, C_LINHA, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(bm, C_OLIVA, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(bm, C_ESCURO, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(bm, ev_tecla, LV_EVENT_VALUE_CHANGED, tc);
    teclado_mapa(tc, 0);
}

// Várias telas se refazem inteiras, inclusive o botão tocado: redesenha só
// depois que o toque termina.
static void atualizar_depois(void *p) { atualizar(); }

// ---------------------------------------------------------------- POMODORO
// A tela da frente é um caminho de quatro passos: escolher a tarefa, ajustar os
// tempos (só se quiser, pela engrenagem), focar e pausar. Cada passo é um painel
// à esquerda; a Lylu e o balão ficam à direita e só mudam de fala.
static lv_obj_t *painel[4], *p_balao, *p_engrenagem, *p_mesa;
static lv_obj_t *esc_lista, *esc_campo_txt;
static char esc_texto[64];
static lv_obj_t *cfg_valor[4], *cfg_sozinha;
static pomo_t pomo_editando;
static passo_t passo_antes_cfg = P_FOCO;
static lv_obj_t *foco_arco, *foco_num, *foco_sub, *foco_tarefa, *foco_btn_txt, *foco_parar, *foco_blocos;
static lv_obj_t *pau_selo, *pau_num, *pau_nome, *pau_btn_txt, *pau_cuidado[3];
static lv_obj_t *dg_tela, *dg_campo;
static teclado_t dg_tc;

static void vai_para(passo_t p) { passo = p; lv_async_call(atualizar_depois, NULL); }

// ---- os tempos guardados ----
#define NVS_POMO "pomodoro"

static void pomo_carrega(void)
{
    nvs_handle_t h;
    if (nvs_open("lylu", NVS_READONLY, &h) != ESP_OK) return;   // nunca salvou: fica o padrão
    pomo_t p;
    size_t n = sizeof p;
    if (nvs_get_blob(h, NVS_POMO, &p, &n) == ESP_OK && n == sizeof p && p.foco && p.blocos) pomo = p;
    nvs_close(h);
    foco.resto = pomo.foco * 60;
}

static void pomo_guarda(void)
{
    nvs_handle_t h;
    if (nvs_open("lylu", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_POMO, &pomo, sizeof pomo);
    nvs_commit(h);
    nvs_close(h);
}

// ---- de um passo pro outro ----
static void fim_do_foco(void)
{
    foco.rodando = false;
    foco.feitos++;
    pausa.longa = foco.bloco >= pomo.blocos;
    pausa.resto = (pausa.longa ? pomo.longa : pomo.curta) * 60;
    pausa.rodando = pomo.sozinha;
    pausa.cuidado = -1;
    passo = P_PAUSA;
    atualizar();
}

static void fim_da_pausa(void)
{
    pausa.rodando = false;
    foco.bloco = pausa.longa ? 1 : foco.bloco + 1;
    foco.resto = pomo.foco * 60;
    foco.rodando = pomo.sozinha;
    passo = P_FOCO;
    atualizar();
}

// ---- 1. escolher a tarefa ----
static void ev_escolhe(lv_event_t *e)
{
    tarefa_t *t = lv_event_get_user_data(e);
    strlcpy(tarefa_atual, t->t, sizeof tarefa_atual);
    vai_para(P_FOCO);
}

static void ev_usar_esta(lv_event_t *e)
{
    strlcpy(tarefa_atual, esc_texto[0] ? esc_texto : "O que você quiser", sizeof tarefa_atual);
    vai_para(P_FOCO);
}

static void cartao_tarefa(tarefa_t *t, bool da_missao)
{
    lv_obj_t *c = cartao(esc_lista);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_style_pad_hor(c, 18, 0);
    lv_obj_set_style_pad_ver(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 16, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(c, C_OLIVA, LV_STATE_PRESSED);
    lv_obj_add_event_cb(c, ev_escolhe, LV_EVENT_CLICKED, t);

    lv_obj_t *cx = caixa(c);
    lv_obj_set_size(cx, 26, 26);
    lv_obj_set_style_radius(cx, 7, 0);
    lv_obj_set_style_border_width(cx, 2, 0);
    lv_obj_set_style_border_color(cx, C_FRACO, 0);

    lv_obj_t *col = caixa(c);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    texto(col, da_missao ? "Missão do dia" : "Bônus, se der", f_corpo_p, C_FRACO);
    lv_obj_t *nome = texto(col, t->t, f_negrito, C_TINTA);
    lv_obj_set_width(nome, 420);
    lv_label_set_long_mode(nome, LV_LABEL_LONG_DOT);

    if (da_missao) {
        lv_obj_t *pr = caixa(c);
        lv_obj_set_style_bg_color(pr, C_LINHA, 0);
        lv_obj_set_style_bg_opa(pr, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pr, 8, 0);
        lv_obj_set_style_pad_hor(pr, 10, 0);
        lv_obj_set_style_pad_ver(pr, 3, 0);
        texto(pr, "P1", f_corpo_p, C_TINTA);
    }
}

static void atualiza_escolher(void)
{
    lv_obj_clean(esc_lista);
    int n = 0;
    for (int i = 0; i < n_missao() && n < 3; i++)
        if (!missao[i].f) { cartao_tarefa(&missao[i], true); n++; }
    for (int i = 0; i < (int)N_BONUS && n < 2 && !dificil; i++)   // no dia difícil, só a missão
        if (!bonus[i].f) { cartao_tarefa(&bonus[i], false); n++; }
    if (!n) {
        lv_obj_t *l = texto(esc_lista, "A missão de hoje já foi. Se quiser mais uma coisa, escreve aqui embaixo.",
                            f_corpo, C_FRACO);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    lv_label_set_text(esc_campo_txt, esc_texto[0] ? esc_texto : "O que você quiser");
    lv_obj_set_style_text_color(esc_campo_txt, esc_texto[0] ? C_TINTA : C_FRACO, 0);
}

// Digitar a tarefa: uma tela por cima, como a senha do Wi-Fi.
static void ev_abre_digitar(lv_event_t *e)
{
    lv_textarea_set_text(dg_campo, esc_texto);
    lv_obj_add_state(dg_campo, LV_STATE_FOCUSED);
    teclado_mapa(&dg_tc, 0);
    lv_obj_remove_flag(dg_tela, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(caixa_pontos, LV_OBJ_FLAG_HIDDEN);   // aqui não se arrasta pro lado
}

static void fecha_digitar(void)
{
    lv_obj_add_flag(dg_tela, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(caixa_pontos, LV_OBJ_FLAG_HIDDEN);
}

static void digitar_ok(void)
{
    strlcpy(esc_texto, lv_textarea_get_text(dg_campo), sizeof esc_texto);
    fecha_digitar();
    if (esc_texto[0]) { strlcpy(tarefa_atual, esc_texto, sizeof tarefa_atual); vai_para(P_FOCO); }
    else lv_async_call(atualizar_depois, NULL);
}

static void digitar_cancela(void) { fecha_digitar(); }

static void cria_digitar(void)
{
    dg_tela = caixa(lv_layer_top());
    lv_obj_set_size(dg_tela, TELA_W, TELA_H - 44);
    lv_obj_set_pos(dg_tela, 0, 44);
    lv_obj_set_style_bg_color(dg_tela, C_FUNDO, 0);
    lv_obj_set_style_bg_opa(dg_tela, LV_OPA_COVER, 0);
    lv_obj_add_flag(dg_tela, LV_OBJ_FLAG_CLICKABLE);    // segura o arrastar do tileview
    lv_obj_add_flag(dg_tela, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *l = texto(dg_tela, "O que você vai fazer agora?", f_negrito_g, C_TINTA);
    lv_obj_set_pos(l, 34, 24);
    l = texto(dg_tela, "Uma coisa só, do jeito que vier. Não precisa ser bonito.", f_corpo, C_FRACO);
    lv_obj_set_pos(l, 34, 72);

    dg_campo = lv_textarea_create(dg_tela);
    lv_textarea_set_one_line(dg_campo, true);
    lv_textarea_set_max_length(dg_campo, sizeof esc_texto - 1);
    lv_obj_set_size(dg_campo, 800, 64);
    lv_obj_set_pos(dg_campo, 34, 120);
    lv_textarea_set_placeholder_text(dg_campo, "O que você quiser");
    lv_obj_set_style_bg_color(dg_campo, C_CARTAO, 0);
    lv_obj_set_style_border_color(dg_campo, C_LINHA, 0);
    lv_obj_set_style_border_width(dg_campo, 2, 0);
    lv_obj_set_style_radius(dg_campo, 12, 0);
    lv_obj_set_style_text_color(dg_campo, C_TINTA, 0);
    lv_obj_set_style_text_font(dg_campo, f_corpo_g, 0);

    dg_tc.campo = dg_campo;
    dg_tc.ok = digitar_ok;
    dg_tc.cancela = digitar_cancela;
    teclado_cria(&dg_tc, dg_tela, 288, "Usar esta");
}

// ---- 2. configurar ----
static const struct { const char *nome, *sub, *un; uint8_t min, max, passo; } AJUSTE[4] = {
    { "Tempo de foco",          "Trabalho sem interrupção", " min", 5, 90, 5 },
    { "Pausa curta",            "Depois de cada bloco",     " min", 1, 30, 1 },
    { "Blocos até pausa longa", "Ciclo completo",           "",     1,  8, 1 },
    { "Pausa longa",            "Depois do último bloco",   " min", 5, 60, 5 },
};

static uint8_t *ajuste_valor(int i)
{
    switch (i) {
    case 0:  return &pomo_editando.foco;
    case 1:  return &pomo_editando.curta;
    case 2:  return &pomo_editando.blocos;
    default: return &pomo_editando.longa;
    }
}

static void atualiza_config(void)
{
    char s[16];
    for (int i = 0; i < 4; i++) {
        snprintf(s, sizeof s, "%d%s", *ajuste_valor(i), AJUSTE[i].un);
        lv_label_set_text(cfg_valor[i], s);
    }
}

static void ev_ajusta(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e), i = k / 2;
    uint8_t *v = ajuste_valor(i);
    int n = *v + (k % 2 ? AJUSTE[i].passo : -AJUSTE[i].passo);
    if (n < AJUSTE[i].min) n = AJUSTE[i].min;
    if (n > AJUSTE[i].max) n = AJUSTE[i].max;
    *v = n;
    atualiza_config();
}

static void ev_engrenagem(lv_event_t *e)
{
    pomo_editando = pomo;
    if (pomo.sozinha) lv_obj_add_state(cfg_sozinha, LV_STATE_CHECKED);
    else lv_obj_remove_state(cfg_sozinha, LV_STATE_CHECKED);
    passo_antes_cfg = passo;
    vai_para(P_CONFIGURAR);
}

static void ev_cfg_salvar(lv_event_t *e)
{
    pomo_editando.sozinha = lv_obj_has_state(cfg_sozinha, LV_STATE_CHECKED);
    pomo = pomo_editando;
    pomo_guarda();
    foco.resto = pomo.foco * 60;          // a engrenagem só aparece com o foco parado
    if (foco.bloco > pomo.blocos) foco.bloco = 1;
    vai_para(passo_antes_cfg);
}

static void ev_cfg_cancelar(lv_event_t *e) { vai_para(passo_antes_cfg); }

static lv_obj_t *botao_redondo(lv_obj_t *pai, const char *s, lv_event_cb_t cb, void *dado)
{
    lv_obj_t *b = lv_button_create(pai);
    lv_obj_set_size(b, 46, 46);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_bg_color(b, C_FUNDO, 0);
    lv_obj_set_style_bg_color(b, C_LINHA, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, C_LINHA, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = texto(b, s, f_negrito, C_TINTA);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, dado);
    return b;
}

static lv_obj_t *linha_config(lv_obj_t *pai, const char *nome, const char *sub, bool ultima)
{
    lv_obj_t *l = caixa(pai);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_ver(l, 7, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (!ultima) {
        lv_obj_set_style_border_side(l, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(l, 1, 0);
        lv_obj_set_style_border_color(l, C_LINHA, 0);
    }
    lv_obj_t *txt = caixa(l);
    lv_obj_set_flex_grow(txt, 1);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    texto(txt, nome, f_negrito, C_TINTA);
    texto(txt, sub, f_corpo_p, C_FRACO);
    return l;
}

// ---- 3. foco ----
static void ev_foco_btn(lv_event_t *e) { foco.rodando = !foco.rodando; atualizar(); }

static void ev_foco_parar(lv_event_t *e)
{
    foco.rodando = false;
    foco.resto = pomo.foco * 60;
    atualizar();
}

static void ev_trocar_tarefa(lv_event_t *e) { if (!foco.rodando) vai_para(P_ESCOLHER); }

static void atualiza_foco(void)
{
    char s[64];
    int total = pomo.foco * 60;
    snprintf(s, sizeof s, "%02d:%02d", foco.resto / 60, foco.resto % 60);
    lv_label_set_text(foco_num, s);
    lv_arc_set_range(foco_arco, 0, total);
    lv_arc_set_value(foco_arco, total - foco.resto);

    bool comecado = foco.resto < total;
    lv_label_set_text(foco_btn_txt, foco.rodando ? "Pausar" : comecado ? "Continuar" : "Começar");
    if (comecado) lv_obj_remove_flag(foco_parar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(foco_parar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(foco_sub, foco.rodando ? "foco" : comecado ? "pausado" : "pronta quando você estiver");
    lv_label_set_text(foco_tarefa, tarefa_atual);

    char hoje[32];
    if (!foco.feitos) strcpy(hoje, "nenhum concluído hoje");
    else snprintf(hoje, sizeof hoje, "%d %s hoje", foco.feitos, foco.feitos == 1 ? "concluído" : "concluídos");
    snprintf(s, sizeof s, "Bloco %d de %d · %s", foco.bloco, pomo.blocos, hoje);
    lv_label_set_text(foco_blocos, s);

    if (foco.rodando) balao_fala(p_balao, NULL, NULL);   // quieta: divide a mesa com o seu foco
    else if (comecado) balao_fala(p_balao, "Pausa é pausa.", "Tô aqui quando voltar.");
    else if (dificil) balao_fala(p_balao, "Se quiser, dez minutinhos já valem.", NULL);
    else balao_fala(p_balao, "Começar é a parte difícil.", "Eu começo junto.");
}

// ---- 4. pausa ----
static const char *CUIDADO[3] = { "Beber água", "Levantar", "Alongar" };
static const char *CUIDADO_FALA[3][2] = {
    { "Um copo inteiro, tá?", "Eu espero." },
    { "Vai até a janela e volta.", "O corpo agradece." },
    { "Ombro longe da orelha.", "Devagarinho." },
};

static void ev_pausa_btn(lv_event_t *e) { pausa.rodando = !pausa.rodando; atualizar(); }
static void ev_pular(lv_event_t *e) { fim_da_pausa(); }

static void ev_cuidado(lv_event_t *e)
{
    pausa.cuidado = (int)(intptr_t)lv_event_get_user_data(e);
    atualizar();
}

static void atualiza_pausa(void)
{
    static const char *EXTENSO[] = { "zero", "um", "dois", "três", "quatro", "cinco",
                                     "seis", "sete", "oito", "nove", "dez" };
    char s[80];
    int total = (pausa.longa ? pomo.longa : pomo.curta) * 60;
    snprintf(s, sizeof s, pausa.longa ? "CICLO DE %d BLOCOS CONCLUÍDO" : "BLOCO %d CONCLUÍDO",
             pausa.longa ? pomo.blocos : foco.bloco);
    lv_label_set_text(pau_selo, s);
    snprintf(s, sizeof s, "%02d:%02d", pausa.resto / 60, pausa.resto % 60);
    lv_label_set_text(pau_num, s);
    lv_label_set_text(pau_nome, pausa.longa ? "Pausa longa" : "Pausa curta");
    lv_label_set_text(pau_btn_txt, pausa.rodando ? "Pausar" : pausa.resto < total ? "Continuar" : "Começar pausa");
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_border_color(pau_cuidado[i], i == pausa.cuidado ? C_OLIVA : C_LINHA, 0);

    if (pausa.cuidado >= 0) balao_fala(p_balao, CUIDADO_FALA[pausa.cuidado][0], CUIDADO_FALA[pausa.cuidado][1]);
    else if (pausa.longa) balao_fala(p_balao, "Ciclo inteiro! Isso é muito.", "Agora descansa de verdade.");
    else {
        int m = pomo.curta;
        if (m <= 10) snprintf(s, sizeof s, "Vai existir longe da cadeira por %s %s.", EXTENSO[m], m == 1 ? "minuto" : "minutos");
        else snprintf(s, sizeof s, "Vai existir longe da cadeira por %d minutos.", m);
        balao_fala(p_balao, "Bloco concluído.", s);
    }
}

// ---- a tela ----
static lv_obj_t *novo_painel(lv_obj_t *t)
{
    lv_obj_t *p = caixa(t);
    lv_obj_set_size(p, 640, TELA_H);
    lv_obj_set_pos(p, 0, 0);
    return p;
}

static lv_obj_t *titulo(lv_obj_t *pai, const char *s, const char *sub)
{
    lv_obj_t *l = texto(pai, s, f_negrito_g, C_TINTA);
    lv_obj_set_pos(l, 44, 70);
    l = texto(pai, sub, f_corpo_p, C_FRACO);
    lv_obj_set_width(l, 560);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l, 44, 118);
    return l;
}

static void cria_pomodoro(lv_obj_t *t)
{
    // 1. escolher
    lv_obj_t *p = painel[P_ESCOLHER] = novo_painel(t);
    titulo(p, "Em que vamos trabalhar?", "Escolhe uma missão ou escreve algo rápido. Uma coisa por vez já serve.");
    lv_obj_t *col = caixa(p);
    lv_obj_set_pos(col, 44, 172);
    lv_obj_set_width(col, 570);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 14, 0);
    esc_lista = caixa(col);
    lv_obj_set_width(esc_lista, LV_PCT(100));
    lv_obj_set_flex_flow(esc_lista, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(esc_lista, 14, 0);

    lv_obj_t *linha = caixa(col);
    lv_obj_set_width(linha, LV_PCT(100));
    lv_obj_set_flex_flow(linha, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(linha, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(linha, 12, 0);
    lv_obj_t *campo = cartao(linha);
    lv_obj_set_style_bg_color(campo, C_FUNDO, 0);
    lv_obj_set_height(campo, 52);
    lv_obj_set_flex_grow(campo, 1);
    lv_obj_set_style_radius(campo, 12, 0);
    lv_obj_set_style_pad_hor(campo, 16, 0);
    lv_obj_add_flag(campo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(campo, ev_abre_digitar, LV_EVENT_CLICKED, NULL);
    esc_campo_txt = texto(campo, "", f_corpo, C_FRACO);
    lv_obj_align(esc_campo_txt, LV_ALIGN_LEFT_MID, 0, 0);
    botao(linha, "Usar esta", true, ev_usar_esta);

    // 2. configurar
    p = painel[P_CONFIGURAR] = novo_painel(t);
    titulo(p, "Configurar Pomodoro", "Ajusta uma vez e eu guardo. Dá para mudar quando o dia pedir outra coisa.");
    lv_obj_t *c = cartao(p);
    lv_obj_set_pos(c, 44, 172);
    lv_obj_set_width(c, 580);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 4, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = linha_config(c, AJUSTE[i].nome, AJUSTE[i].sub, false);
        botao_redondo(l, "-", ev_ajusta, (void *)(intptr_t)(i * 2));
        cfg_valor[i] = texto(l, "", f_negrito, C_TINTA);
        lv_obj_set_width(cfg_valor[i], 96);
        lv_obj_set_style_text_align(cfg_valor[i], LV_TEXT_ALIGN_CENTER, 0);
        botao_redondo(l, "+", ev_ajusta, (void *)(intptr_t)(i * 2 + 1));
    }
    lv_obj_t *l = linha_config(c, "Avançar automaticamente", "Começa a próxima etapa sozinha", true);
    cfg_sozinha = lv_switch_create(l);
    lv_obj_set_size(cfg_sozinha, 62, 34);
    lv_obj_set_style_bg_color(cfg_sozinha, C_LINHA, 0);
    lv_obj_set_style_bg_color(cfg_sozinha, C_OLIVA, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_t *bts = caixa(p);
    lv_obj_set_pos(bts, 44, 492);
    lv_obj_set_flex_flow(bts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bts, 12, 0);
    botao(bts, "Salvar", true, ev_cfg_salvar);
    botao(bts, "Cancelar", false, ev_cfg_cancelar);

    // 3. foco
    p = painel[P_FOCO] = novo_painel(t);
    col = caixa(p);
    lv_obj_set_size(col, 420, 520);
    lv_obj_set_pos(col, 20, 58);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 14, 0);

    foco_arco = lv_arc_create(col);
    lv_obj_set_size(foco_arco, 290, 290);
    lv_arc_set_rotation(foco_arco, 270);
    lv_arc_set_bg_angles(foco_arco, 0, 360);
    lv_obj_remove_style(foco_arco, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(foco_arco, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(foco_arco, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(foco_arco, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(foco_arco, C_LINHA, LV_PART_MAIN);
    lv_obj_set_style_arc_color(foco_arco, C_OLIVA, LV_PART_INDICATOR);
    foco_num = texto(foco_arco, "25:00", f_px_foco, C_TINTA);
    lv_obj_center(foco_num);
    foco_sub = texto(foco_arco, "", f_corpo_p, C_FRACO);
    lv_obj_align(foco_sub, LV_ALIGN_BOTTOM_MID, 0, -66);

    // Tocar na tarefa volta pra escolher outra (com o relógio parado).
    lv_obj_t *trab = caixa(col);
    lv_obj_set_flex_flow(trab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(trab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(trab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(trab, 10);
    lv_obj_add_event_cb(trab, ev_trocar_tarefa, LV_EVENT_CLICKED, NULL);
    texto(trab, "trabalhando em", f_corpo_p, C_FRACO);
    foco_tarefa = texto(trab, "", f_negrito, C_TINTA);
    lv_obj_set_style_max_width(foco_tarefa, 410, 0);
    lv_label_set_long_mode(foco_tarefa, LV_LABEL_LONG_DOT);

    bts = caixa(col);
    lv_obj_set_flex_flow(bts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bts, 12, 0);
    foco_btn_txt = lv_obj_get_child(botao(bts, "Começar", true, ev_foco_btn), 0);
    foco_parar = botao(bts, "Parar", false, ev_foco_parar);

    foco_blocos = texto(col, "", f_corpo_p, C_FRACO);

    // 4. pausa
    p = painel[P_PAUSA] = novo_painel(t);
    col = caixa(p);
    lv_obj_set_size(col, 600, 500);
    lv_obj_set_pos(col, 20, 70);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 10, 0);
    lv_obj_t *selo = caixa(col);
    lv_obj_set_style_bg_color(selo, C_CARTAO, 0);
    lv_obj_set_style_bg_opa(selo, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(selo, C_LINHA, 0);
    lv_obj_set_style_border_width(selo, 1, 0);
    lv_obj_set_style_radius(selo, 99, 0);
    lv_obj_set_style_pad_hor(selo, 16, 0);
    lv_obj_set_style_pad_ver(selo, 5, 0);
    pau_selo = texto(selo, "", f_corpo_p, C_OLIVA);
    pau_num = texto(col, "05:00", f_px_foco, C_TINTA);
    pau_nome = texto(col, "", f_corpo_g, C_TINTA);
    texto(col, "O trabalho fica aqui. Agora escolhe um cuidado pequeno.", f_corpo_p, C_FRACO);
    lv_obj_t *cuid = caixa(col);
    lv_obj_set_flex_flow(cuid, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cuid, 12, 0);
    lv_obj_set_style_margin_top(cuid, 16, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = pau_cuidado[i] = botao(cuid, CUIDADO[i], false, ev_cuidado);
        lv_obj_remove_event_cb(b, ev_cuidado);                  // botao() não passa dado
        lv_obj_add_event_cb(b, ev_cuidado, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_color(b, C_LINHA, 0);
    }
    bts = caixa(col);
    lv_obj_set_flex_flow(bts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bts, 12, 0);
    lv_obj_set_style_margin_top(bts, 16, 0);
    pau_btn_txt = lv_obj_get_child(botao(bts, "Começar pausa", true, ev_pausa_btn), 0);
    botao(bts, "Pular", false, ev_pular);

    // o lado da Lylu
    p_mesa = caixa(t);
    lv_obj_set_size(p_mesa, 420, 8);
    lv_obj_set_pos(p_mesa, 580, 548);
    lv_obj_set_style_bg_color(p_mesa, C_LINHA, 0);
    lv_obj_set_style_bg_opa(p_mesa, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p_mesa, 4, 0);

    p_balao = balao(t, 650, 64, 268);

    p_engrenagem = lv_button_create(t);
    lv_obj_set_size(p_engrenagem, 44, 44);
    lv_obj_set_pos(p_engrenagem, 968, 64);
    lv_obj_set_style_radius(p_engrenagem, 22, 0);
    lv_obj_set_style_bg_color(p_engrenagem, C_CARTAO, 0);
    lv_obj_set_style_border_color(p_engrenagem, C_LINHA, 0);
    lv_obj_set_style_border_width(p_engrenagem, 1, 0);
    lv_obj_set_style_shadow_width(p_engrenagem, 0, 0);
    lv_obj_set_ext_click_area(p_engrenagem, 8);
    lv_obj_t *g = texto(p_engrenagem, LV_SYMBOL_SETTINGS, &lv_font_montserrat_14, C_TINTA);
    lv_obj_center(g);
    lv_obj_add_event_cb(p_engrenagem, ev_engrenagem, LV_EVENT_CLICKED, NULL);
}

static void atualiza_pomodoro(void)
{
    for (int i = 0; i < 4; i++)
        if (i == (int)passo) lv_obj_remove_flag(painel[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(painel[i], LV_OBJ_FLAG_HIDDEN);
    bool engrenagem = passo == P_ESCOLHER || (passo == P_FOCO && !foco.rodando);
    if (engrenagem) lv_obj_remove_flag(p_engrenagem, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(p_engrenagem, LV_OBJ_FLAG_HIDDEN);
    if (passo == P_FOCO) lv_obj_remove_flag(p_mesa, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(p_mesa, LV_OBJ_FLAG_HIDDEN);

    switch (passo) {
    case P_ESCOLHER:
        atualiza_escolher();
        balao_fala(p_balao, "Não precisa escolher a tarefa perfeita.", "Escolhe uma possível.");
        break;
    case P_CONFIGURAR:
        atualiza_config();
        balao_fala(p_balao, "Vinte e cinco é padrão, não mandamento.", "O cérebro não assina contrato.");
        break;
    case P_FOCO:  atualiza_foco(); break;
    case P_PAUSA: atualiza_pausa(); break;
    }
}

// A cada segundo só os números mudam; refazer os cartões a cada tique piscaria.
static void tique_pomodoro(void)
{
    if (passo == P_FOCO) atualiza_foco();
    else if (passo == P_PAUSA) atualiza_pausa();
}

static anim_t anim_foco(void)
{
    if (passo == P_FOCO && foco.rodando) return A_CONCENTRADA;
    return A_FALA;          // a Lylu nova, conversando
}

// ---------------------------------------------------------------- TAREFAS
// A missão do dia em cima, o bônus embaixo, a Lylu do lado esquerdo olhando.
static lv_obj_t *tar_contador, *tar_feitas;

static int feitas_hoje(void)
{
    int n = missao_feita();
    for (int i = 0; i < (int)N_BONUS; i++) n += bonus[i].f;
    return n;
}

static void ev_tarefa(lv_event_t *e)
{
    tarefa_t *t = lv_event_get_user_data(e);
    t->f = !t->f;
    lv_async_call(atualizar_depois, NULL);
}

static void ev_cortar(lv_event_t *e) { cortada = true; lv_async_call(atualizar_depois, NULL); }

static void item(lv_obj_t *pai, tarefa_t *t, bool grande)
{
    lv_obj_t *linha = caixa(pai);
    lv_obj_set_width(linha, LV_PCT(100));
    lv_obj_set_flex_flow(linha, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(linha, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(linha, 14, 0);
    lv_obj_set_style_pad_ver(linha, grande ? 8 : 5, 0);
    lv_obj_add_flag(linha, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(linha, ev_tarefa, LV_EVENT_CLICKED, t);

    int lado = grande ? 26 : 22;
    lv_obj_t *cx = caixa(linha);
    lv_obj_set_size(cx, lado, lado);
    lv_obj_set_style_radius(cx, grande ? 7 : 6, 0);
    lv_obj_set_style_border_width(cx, 2, 0);
    lv_obj_t *v = texto(cx, LV_SYMBOL_OK, &lv_font_montserrat_14, C_ESCURO);
    lv_obj_center(v);

    lv_obj_t *l = texto(linha, t->t, grande ? f_negrito : f_corpo, C_TINTA);
    lv_obj_set_flex_grow(l, 1);

    lv_obj_set_style_border_color(cx, t->f ? C_OLIVA : C_FRACO, 0);
    lv_obj_set_style_bg_color(cx, C_OLIVA, 0);
    lv_obj_set_style_bg_opa(cx, t->f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (!t->f) lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(l, t->f ? C_FRACO : C_TINTA, 0);
    lv_obj_set_style_text_decor(l, t->f ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);
}

static lv_obj_t *cartao_lista(lv_obj_t *pai, const char *titulo, const char *pil, lv_color_t pil_cor, lv_obj_t **lista)
{
    lv_obj_t *c = cartao(pai);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *topo = caixa(c);
    lv_obj_set_width(topo, LV_PCT(100));
    lv_obj_set_flex_flow(topo, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topo, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    texto(topo, titulo, f_negrito, C_TINTA);
    pilula(topo, pil, pil_cor);
    lv_obj_t *l = *lista = caixa(c);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(l, 4, 0);
    return c;
}

static void cria_tarefas(lv_obj_t *t)
{
    cabeca(t, 420, "HOJE", "Missão do dia", NULL, 400);

    lv_obj_t *cont = caixa(t);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_align(cont, LV_ALIGN_TOP_RIGHT, -44, 100);
    tar_contador = texto(cont, "", f_negrito_m, C_OLIVA);
    texto(cont, "feito", f_corpo_p, C_FRACO);

    lv_obj_t *col = caixa(t);
    lv_obj_set_pos(col, 420, 172);
    lv_obj_set_width(col, 560);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 14, 0);

    lv_obj_t *m = cartao_lista(col, "Fez isso, o dia tá ganho.", "prioridade", C_AMBAR, &lista_missao);
    btn_cortar = lv_button_create(m);
    lv_obj_set_style_bg_opa(btn_cortar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_cortar, 0, 0);
    lv_obj_set_style_border_color(btn_cortar, C_LINHA, 0);
    lv_obj_set_style_border_width(btn_cortar, 1, 0);
    lv_obj_set_style_radius(btn_cortar, 10, 0);
    lv_obj_set_style_margin_top(btn_cortar, 6, 0);
    texto(btn_cortar, "Cortar a missão pra uma coisa só", f_corpo_p, C_TINTA);
    lv_obj_add_event_cb(btn_cortar, ev_cortar, LV_EVENT_CLICKED, NULL);

    caixa_bonus = cartao_lista(col, "Bônus, se der", "sem pressa", C_OLIVA, &lista_bonus);

    lv_obj_t *rod = caixa(col);
    lv_obj_set_width(rod, LV_PCT(100));
    lv_obj_set_flex_flow(rod, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rod, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    texto(rod, "O que sobrar vira dica amanhã.", f_corpo_p, C_FRACO);
    tar_feitas = texto(rod, "", f_negrito_p, C_OLIVA);

    tar_balao = balao(t, 30, 72, 290);
}

static void atualiza_tarefas(void)
{
    lv_obj_clean(lista_missao);
    for (int i = 0; i < n_missao(); i++) item(lista_missao, &missao[i], true);
    lv_obj_clean(lista_bonus);
    for (int i = 0; i < (int)N_BONUS; i++) item(lista_bonus, &bonus[i], false);

    if (dificil) lv_obj_add_flag(caixa_bonus, LV_OBJ_FLAG_HIDDEN);   // no dia difícil o bônus some
    else lv_obj_remove_flag(caixa_bonus, LV_OBJ_FLAG_HIDDEN);
    if (dificil && !cortada && n_missao() > 1) lv_obj_remove_flag(btn_cortar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(btn_cortar, LV_OBJ_FLAG_HIDDEN);

    int f = missao_feita(), n = n_missao();
    char s[40];
    snprintf(s, sizeof s, "%d de %d", f, n);
    lv_label_set_text(tar_contador, s);
    int h = feitas_hoje();
    if (h == 1) strcpy(s, "1 coisa feita hoje");
    else snprintf(s, sizeof s, "%d coisas feitas hoje", h);
    lv_label_set_text(tar_feitas, s);

    if (f == n) balao_fala(tar_balao, "Missão do dia batida!", "O resto é enfeite.");
    else if (dificil) balao_fala(tar_balao, cortada ? "Uma coisa só hoje." : "Hoje é só isso aqui.",
                                 cortada ? "Já é o dia inteiro." : "Sem pressa.");
    else if (f == 0) balao_fala(tar_balao, n == 1 ? "Uma coisinha e o dia tá ganho." : "Duas coisinhas e o dia tá ganho.",
                                "O resto é bônus.");
    else balao_fala(tar_balao, "Metade da missão já foi.", "Tô vendo, viu?");
}

// ---------------------------------------------------------------- RECADOS
// Um recado por vez: o que a Lylu guardou pra você não precisar guardar na
// cabeça. "Entendi" tira da fila; "Lembrar depois" manda pro fim dela.
// Por enquanto os recados moram aqui; o próximo passo é vir do n8n/Supabase.
typedef struct {
    const char *dia, *dia_maiusc, *hora, *titulo, *detalhe, *tipo, *fala, *fala_forte;
    bool visto;
} recado_t;

static recado_t recados[] = {
    { "amanhã", "AMANHÃ", "14:00", "Postar a placa nos Correios",
      "Levar o display com defeito para devolução.", "lembrete",
      "Mica, amanhã tem Correios.", "Eu te lembro na hora.", false },
};
#define N_RECADOS (sizeof(recados) / sizeof(recados[0]))

static int recado_atual;          // índice do que está na frente da fila
static bool recado_adiado;        // acabou de tocar em "Lembrar depois"
static lv_obj_t *rec_titulo, *rec_sub, *rec_cartao, *rec_quando, *rec_tipo, *rec_nome, *rec_detalhe, *rec_rodape, *rec_balao;

static int recados_pendentes(void)
{
    int n = 0;
    for (int i = 0; i < (int)N_RECADOS; i++) n += !recados[i].visto;
    return n;
}

// O próximo não visto, dando a volta na fila a partir de "de".
static int proximo_recado(int de)
{
    for (int k = 0; k < (int)N_RECADOS; k++) {
        int i = (de + k) % N_RECADOS;
        if (!recados[i].visto) return i;
    }
    return -1;
}

static void ev_recado_entendi(lv_event_t *e)
{
    recados[recado_atual].visto = true;
    recado_adiado = false;
    int p = proximo_recado(recado_atual + 1);
    if (p >= 0) recado_atual = p;
    atualizar();
}

static void ev_recado_depois(lv_event_t *e)
{
    recado_adiado = true;
    int p = proximo_recado(recado_atual + 1);
    if (p >= 0) recado_atual = p;
    atualizar();
}

static void cria_recados(lv_obj_t *t)
{
    lv_obj_t *tag = caixa(t);
    lv_obj_set_pos(tag, 44, 84);
    lv_obj_set_flex_flow(tag, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tag, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tag, 10, 0);
    lv_obj_t *ponto = caixa(tag);
    lv_obj_set_size(ponto, 6, 6);
    lv_obj_set_style_radius(ponto, 3, 0);
    lv_obj_set_style_bg_color(ponto, C_OLIVA, 0);
    lv_obj_set_style_bg_opa(ponto, LV_OPA_COVER, 0);
    texto(tag, "RECADO DA LYLU", f_negrito_p, C_OLIVA);

    rec_titulo = texto(t, "", f_negrito_g, C_TINTA);
    lv_obj_set_pos(rec_titulo, 44, 110);
    rec_sub = texto(t, "", f_corpo_p, C_FRACO);
    lv_obj_set_width(rec_sub, 560);
    lv_label_set_long_mode(rec_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(rec_sub, 44, 160);

    // Cartão e rodapé numa coluna: o rodapé desce sozinho quando o cartão cresce.
    lv_obj_t *col = caixa(t);
    lv_obj_set_pos(col, 44, 206);
    lv_obj_set_width(col, 560);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 18, 0);
    lv_obj_t *c = rec_cartao = cartao(col);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_style_pad_hor(c, 28, 0);
    lv_obj_set_style_pad_ver(c, 24, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 6, 0);

    lv_obj_t *topo = caixa(c);
    lv_obj_set_width(topo, LV_PCT(100));
    lv_obj_set_flex_flow(topo, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topo, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_bottom(topo, 14, 0);
    rec_quando = texto(topo, "", f_negrito_p, C_OLIVA);
    lv_obj_t *pilula = caixa(topo);
    lv_obj_set_style_bg_color(pilula, C_LINHA, 0);
    lv_obj_set_style_bg_opa(pilula, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pilula, 99, 0);
    lv_obj_set_style_pad_hor(pilula, 10, 0);
    lv_obj_set_style_pad_ver(pilula, 3, 0);
    rec_tipo = texto(pilula, "", f_corpo_p, C_OLIVA);

    rec_nome = texto(c, "", f_negrito_m, C_TINTA);
    lv_obj_set_width(rec_nome, LV_PCT(100));
    lv_label_set_long_mode(rec_nome, LV_LABEL_LONG_WRAP);
    rec_detalhe = texto(c, "", f_corpo, C_FRACO);
    lv_obj_set_width(rec_detalhe, LV_PCT(100));
    lv_label_set_long_mode(rec_detalhe, LV_LABEL_LONG_WRAP);

    lv_obj_t *bts = caixa(c);
    lv_obj_set_flex_flow(bts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bts, 12, 0);
    lv_obj_set_style_margin_top(bts, 20, 0);
    botao(bts, "Entendi", true, ev_recado_entendi);
    lv_obj_t *depois = botao(bts, "Lembrar depois", false, ev_recado_depois);
    lv_obj_set_style_border_color(depois, C_LINHA, 0);   // em cima do cartão, sem borda ele sumia
    lv_obj_set_style_border_width(depois, 1, 0);

    rec_rodape = texto(col, "Um recado por vez. O resto espera.", f_corpo_p, C_FRACO);

    rec_balao = balao(t, 652, 72, 290);
}

static void atualiza_recados(void)
{
    if (!recados_pendentes()) {
        lv_label_set_text(rec_titulo, "Nenhum recado agora");
        lv_label_set_text(rec_sub, "Quando aparecer alguma coisa, eu guardo aqui pra você.");
        lv_obj_add_flag(rec_cartao, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rec_rodape, LV_OBJ_FLAG_HIDDEN);
        balao_fala(rec_balao, "Tudo guardado.", "Pode descansar a cabeça.");
        return;
    }
    recado_t *r = &recados[recado_atual];
    char s[64];
    snprintf(s, sizeof s, "Tem uma coisa pra %s", r->dia);
    lv_label_set_text(rec_titulo, s);
    lv_label_set_text(rec_sub, "Eu guardei aqui pra você não precisar guardar na cabeça.");
    lv_obj_remove_flag(rec_cartao, LV_OBJ_FLAG_HIDDEN);
    snprintf(s, sizeof s, "%s · %s", r->dia_maiusc, r->hora);
    lv_label_set_text(rec_quando, s);
    lv_label_set_text(rec_tipo, r->tipo);
    lv_label_set_text(rec_nome, r->titulo);
    lv_label_set_text(rec_detalhe, r->detalhe);

    int n = recados_pendentes();
    if (n > 1) {
        snprintf(s, sizeof s, "Um recado por vez. Tem mais %d esperando.", n - 1);
        lv_label_set_text(rec_rodape, s);
    } else lv_label_set_text(rec_rodape, "Um recado por vez. O resto espera.");
    lv_obj_remove_flag(rec_rodape, LV_OBJ_FLAG_HIDDEN);

    if (recado_adiado) balao_fala(rec_balao, "Tá bom.", "Eu te lembro mais tarde.");
    else balao_fala(rec_balao, r->fala, r->fala_forte);
}

// ---------------------------------------------------------------- CASA (o cantinho da Lylu)
// Uma pausa sem culpa: três respiros curtos à esquerda, e o quarto dela à direita,
// onde ela anda e faz as coisinhas dela quando ninguém pede nada.
static struct { float x, alvo; anim_t fazendo; int64_t ate; } casa = { 540, 540, A_LENDO, 0 };
static const anim_t ATIVIDADES[] = { A_LENDO, A_CAFE, A_BOLHAS, A_BRINCANDO, A_BOCEJANDO, A_DORMINDO, A_AGUA, A_NINTENDO };
#define CASA_Y 250
#define CASA_X_MIN 440      // antes disso ela passava por cima do texto
#define CASA_X_MAX 660      // 1024 - 360 da largura dela
static lv_obj_t *casa_balao;
static int casa_respiro = -1;

static const char *RESPIRO[3] = { "Respirar", "Beber água", "Alongar" };
static const char *RESPIRO_FALA[3][2] = {
    { "Inspira contando até quatro…", "Solta devagar. Eu faço junto." },
    { "Um copo inteiro, tá?", "Eu bebo junto." },
    { "Estica os braços pro alto.", "Ombro longe da orelha." },
};
static const anim_t RESPIRO_ANIM[3] = { A_BOLHAS, A_AGUA, A_BOCEJANDO };

static lv_obj_t *retangulo(lv_obj_t *pai, int x, int y, int w, int h, uint32_t cor)
{
    lv_obj_t *o = caixa(pai);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(cor), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void ev_respiro(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    casa_respiro = i;
    casa.alvo = casa.x;                  // para de andar e faz junto
    casa.fazendo = RESPIRO_ANIM[i];
    casa.ate = agora_ms() + 20000;
    atualizar();
}

static void cria_casa(lv_obj_t *t)
{
    // o quarto
    lv_obj_t *jan = retangulo(t, 760, 76, 220, 160, 0x2b4a3e);
    lv_obj_set_style_radius(jan, 6, 0);
    lv_obj_t *ceu = retangulo(jan, 8, 8, 204, 144, 0x16323a);
    lv_obj_t *lua = retangulo(ceu, 132, 22, 38, 38, 0xe9e5c4);
    lv_obj_set_style_radius(lua, LV_RADIUS_CIRCLE, 0);
    retangulo(jan, 106, 8, 8, 144, 0x2b4a3e);
    retangulo(jan, 8, 76, 204, 8, 0x2b4a3e);

    lv_obj_t *tapete = retangulo(t, 470, 540, 360, 40, 0x1a3329);
    lv_obj_set_style_radius(tapete, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(tapete, lv_color_hex(0x2b4a3e), 0);
    lv_obj_set_style_border_width(tapete, 6, 0);

    lv_obj_t *vaso = retangulo(t, 930, 470, 52, 62, 0x5a4030);
    lv_obj_set_style_radius(vaso, 10, 0);
    const int folhas[3][3] = { { 912, 404, 0x4f6d33 }, { 948, 392, 0x5f7f3c }, { 928, 370, 0x6f8c44 } };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *f = retangulo(t, folhas[i][0], folhas[i][1], 50, 50, folhas[i][2]);
        lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
    }

    // o lado de ler
    cabeca(t, 44, "CANTINHO DA LYLU", "Uma pausa sem culpa",
           "Escolhe um respiro curto. Não precisa transformar descanso em outra tarefa.", 380);
    lv_obj_t *linha = caixa(t);
    lv_obj_set_pos(linha, 44, 214);
    lv_obj_set_flex_flow(linha, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(linha, 12, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = cartao(linha);
        lv_obj_set_size(c, 118, 104);
        lv_obj_set_style_pad_all(c, 14, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(c, C_OLIVA, LV_STATE_PRESSED);
        lv_obj_add_event_cb(c, ev_respiro, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        // os desenhinhos: um anel (respirar), uma gota (água), uma barra em pé (alongar)
        lv_obj_t *ic = caixa(c);
        lv_obj_set_pos(ic, 0, 0);
        lv_obj_set_size(ic, i == 2 ? 8 : 22, i == 2 ? 26 : 22);
        lv_obj_set_style_radius(ic, i == 2 ? 4 : LV_RADIUS_CIRCLE, 0);
        if (i == 0) { lv_obj_set_style_border_color(ic, C_OLIVA, 0); lv_obj_set_style_border_width(ic, 3, 0); }
        else { lv_obj_set_style_bg_color(ic, i == 1 ? C_CEU : C_OLIVA, 0); lv_obj_set_style_bg_opa(ic, LV_OPA_COVER, 0); }
        lv_obj_t *l = texto(c, RESPIRO[i], f_negrito_p, C_TINTA);
        lv_obj_set_width(l, 90);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    lv_obj_t *r = texto(t, "Sem cronômetro. Volta quando quiser.", f_corpo_p, C_FRACO);
    lv_obj_set_pos(r, 44, 336);

    casa_balao = balao(t, 470, 72, 250);
}

static void atualiza_casa(void)
{
    if (casa_respiro >= 0) balao_fala(casa_balao, RESPIRO_FALA[casa_respiro][0], RESPIRO_FALA[casa_respiro][1]);
    else balao_fala(casa_balao, "Faz um tempinho que você tá aí.", "Quer respirar comigo?");
}

static void passo_casa(lv_timer_t *tm)
{
    if (tela_atual != T_CASA || carinho_ate > agora_ms()) return;
    float d = casa.alvo - casa.x;
    if (d > 2 || d < -2) {
        float v = 3.0f;
        casa.x += (d > 0 ? 1 : -1) * (v < (d > 0 ? d : -d) ? v : (d > 0 ? d : -d));
        lv_obj_set_x(lylu, (int)casa.x);
        lv_obj_set_x(lylu_outra, (int)casa.x);
        lylu_mostra(casa.alvo > casa.x ? A_ANDA_DIR : A_ANDA_ESQ);
    } else if (agora_ms() > casa.ate) {
        if (casa_respiro >= 0) { casa_respiro = -1; atualiza_casa(); }   // o respiro acabou
        if (casa.ate && esp_random() % 10 < 6) {
            casa.alvo = CASA_X_MIN + esp_random() % (CASA_X_MAX - CASA_X_MIN);
            casa.fazendo = ATIVIDADES[esp_random() % (sizeof ATIVIDADES / sizeof ATIVIDADES[0])];
        }
        casa.ate = agora_ms() + 5000 + esp_random() % 4000;
        lylu_mostra(casa.fazendo);
    } else {
        lylu_mostra(casa.fazendo);
    }
}

// ---------------------------------------------------------------- RELÓGIO
static lv_obj_t *rel_saud, *rel_hora, *rel_data, *rel_cartao, *rel_c_hora, *rel_c_nome, *rel_c_sub, *rel_balao;

static const char *DIAS[] = { "domingo", "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira",
                              "sexta-feira", "sábado" };
static const char *DIAS_CURTO[] = { "DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB" };
static const char *MESES[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho",
                               "agosto", "setembro", "outubro", "novembro", "dezembro" };

// O primeiro recado ainda não visto, ou NULL.
static recado_t *proximo_pendente(void)
{
    int i = proximo_recado(0);
    return i < 0 ? NULL : &recados[i];
}

static void cria_relogio(lv_obj_t *t)
{
    lv_obj_t *cab = cabeca(t, 44, "", NULL, NULL, 560);
    rel_saud = lv_obj_get_child(lv_obj_get_child(cab, 0), 1);
    rel_hora = texto(t, "--:--", f_relogio, C_TINTA);
    lv_obj_set_pos(rel_hora, 38, 100);
    rel_data = texto(t, "", f_corpo_g, C_FRACO);
    lv_obj_set_pos(rel_data, 46, 262);

    lv_obj_t *c = rel_cartao = cartao(t);
    lv_obj_set_pos(c, 44, 330);
    lv_obj_set_width(c, 480);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 16, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 20, 0);
    rel_c_hora = texto(c, "", f_negrito_m, C_OLIVA);
    lv_obj_t *col = caixa(c);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    rel_c_nome = texto(col, "", f_negrito, C_TINTA);
    lv_obj_set_width(rel_c_nome, LV_PCT(100));
    lv_label_set_long_mode(rel_c_nome, LV_LABEL_LONG_DOT);
    rel_c_sub = texto(col, "", f_corpo_p, C_FRACO);

    rel_balao = balao(t, 652, 72, 290);
}

// Chamada todo segundo, mas só mexe no que mudou: redesenhar o relógio grande
// a cada segundo à toa pesava na tela.
static void atualiza_hora(void)
{
    static int antes_min = -1, antes_dia = -1;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_min == antes_min) return;
    antes_min = tm.tm_min;
    char s[64];
    snprintf(s, sizeof s, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(st_hora, s);
    lv_label_set_text(rel_hora, s);
    const char *oi = tm.tm_hour < 5 ? "BOA NOITE, MICA" : tm.tm_hour < 12 ? "BOM DIA, MICA"
                   : tm.tm_hour < 18 ? "BOA TARDE, MICA" : "BOA NOITE, MICA";
    lv_label_set_text(rel_saud, oi);
    if (tm.tm_mday != antes_dia) {
        antes_dia = tm.tm_mday;
        snprintf(s, sizeof s, "%s · %d de %s", DIAS[tm.tm_wday], tm.tm_mday, MESES[tm.tm_mon]);
        lv_label_set_text(rel_data, s);
    }
}

static void atualiza_relogio(void)
{
    recado_t *r = proximo_pendente();
    if (r) {
        lv_obj_remove_flag(rel_cartao, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(rel_c_hora, r->hora);
        lv_label_set_text(rel_c_nome, r->titulo);
        lv_label_set_text(rel_c_sub, r->dia);
    } else lv_obj_add_flag(rel_cartao, LV_OBJ_FLAG_HIDDEN);

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int h = tm.tm_hour;
    if (h >= 20 || h < 5) balao_fala(rel_balao, "Hoje já deu, viu?", r ? "Amanhã eu te lembro do resto." : "Descansa, que amanhã tem mais.");
    else if (h < 12) balao_fala(rel_balao, "Bom dia!", "Uma coisa de cada vez hoje.");
    else if (h < 18) balao_fala(rel_balao, "Metade do dia já foi.", "Tá indo bem, viu?");
    else balao_fala(rel_balao, "O dia tá acabando.", "Se der, fecha uma coisinha só.");
}

// ---------------------------------------------------------------- SEMANA
// Por enquanto a placa só sabe do hoje: os outros dias ficam vazios, sem dado
// inventado. Quando vier o histórico (Supabase), é só encher as colunas.
static lv_obj_t *sem_col[7], *sem_nome[7], *sem_num[7], *sem_barras[7], *sem_total, *sem_prox;

static void cria_semana(lv_obj_t *t)
{
    cabeca(t, 44, "SUA SEMANA", "Um dia de cada vez", NULL, 400);

    lv_obj_t *cont = caixa(t);
    lv_obj_set_pos(cont, 404, 104);
    lv_obj_set_width(cont, 200);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    sem_total = texto(cont, "", f_negrito, C_OLIVA);
    texto(cont, "feitas hoje", f_corpo_p, C_FRACO);

    lv_obj_t *dias = caixa(t);
    lv_obj_set_pos(dias, 44, 176);
    lv_obj_set_flex_flow(dias, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dias, 11, 0);
    for (int i = 0; i < 7; i++) {
        lv_obj_t *d = sem_col[i] = cartao(dias);
        lv_obj_set_size(d, 70, 170);
        lv_obj_set_style_pad_top(d, 12, 0);
        lv_obj_set_flex_flow(d, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(d, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(d, 2, 0);
        sem_nome[i] = texto(d, DIAS_CURTO[i], f_corpo_p, C_FRACO);
        sem_num[i] = texto(d, "", f_negrito, C_TINTA);
        lv_obj_t *b = sem_barras[i] = caixa(d);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(b, 5, 0);
        lv_obj_set_style_margin_top(b, 12, 0);
    }

    lv_obj_t *c = cartao(t);
    lv_obj_set_pos(c, 44, 364);
    lv_obj_set_width(c, 560);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    sem_prox = texto(c, "", f_corpo, C_TINTA);
    lv_obj_set_width(sem_prox, LV_PCT(100));
    lv_label_set_long_mode(sem_prox, LV_LABEL_LONG_DOT);

    lv_obj_t *nota = texto(t, "Por enquanto eu só guardo o hoje. Os outros dias vêm depois.", f_corpo_p, C_FRACO);
    lv_obj_set_pos(nota, 44, 432);

    lv_obj_t *b = balao(t, 652, 72, 290);
    balao_fala(b, "A semana não é placar, Mica.", "É só um mapa pra eu te acompanhar.");
}

static void atualiza_semana(void)
{
    time_t now = time(NULL);
    struct tm hoje;
    localtime_r(&now, &hoje);
    int feitas = feitas_hoje();
    char s[80];
    for (int i = 0; i < 7; i++) {
        struct tm d = hoje;
        d.tm_mday += i - hoje.tm_wday;       // de domingo a sábado desta semana
        mktime(&d);
        snprintf(s, sizeof s, "%02d", d.tm_mday);
        lv_label_set_text(sem_num[i], s);
        bool e_hoje = i == hoje.tm_wday;
        lv_obj_set_style_border_color(sem_col[i], e_hoje ? C_OLIVA : C_LINHA, 0);
        lv_obj_set_style_border_width(sem_col[i], e_hoje ? 2 : 1, 0);
        lv_obj_set_style_text_color(sem_num[i], i > hoje.tm_wday ? C_FRACO : C_TINTA, 0);
        lv_obj_clean(sem_barras[i]);
        for (int k = 0; e_hoje && k < feitas && k < 8; k++) {
            lv_obj_t *barra = caixa(sem_barras[i]);
            lv_obj_set_size(barra, 44, 6);
            lv_obj_set_style_radius(barra, 3, 0);
            lv_obj_set_style_bg_color(barra, C_OLIVA, 0);
            lv_obj_set_style_bg_opa(barra, LV_OPA_COVER, 0);
        }
    }
    if (feitas == 1) strcpy(s, "1 coisa");
    else snprintf(s, sizeof s, "%d coisas", feitas);
    lv_label_set_text(sem_total, s);

    recado_t *r = proximo_pendente();
    if (r) {
        snprintf(s, sizeof s, "%s, %s · %s", r->dia, r->hora, r->titulo);
        if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 'a' - 'A';   // "amanhã" -> "Amanhã"
        lv_label_set_text(sem_prox, s);
    } else lv_label_set_text(sem_prox, "Nada guardado pra frente. Semana leve.");
}

// ---------------------------------------------------------------- WI-FI
// Fica por cima de tudo (menos a barra de status) e só aparece quando a pessoa
// toca em "Wi-Fi" nos Ajustes — não é uma das seis telas, é um lugar aonde se vai.
//
// O plano antigo era portal cativo (docs/telas-da-lylu.md), porque digitar senha
// num teclado de 480x272 é sofrido. Nesta tela de 1024x600 a tecla fica com 90px:
// dá pra digitar aqui mesmo, sem precisar de celular.

#define WF_TOPO 44                          // a barra de status continua à vista
#define WF_ALTURA (TELA_H - WF_TOPO)

static lv_obj_t *wf_tela, *wf_status, *wf_lista, *wf_recado, *wf_procurar, *wf_esquecer;
static lv_obj_t *wf_balao, *wf_senha_titulo, *wf_campo, *wf_olho, *wf_teclado, *wf_balao_senha;
static teclado_t wf_tc;
static lv_obj_t *aj_wifi_sub;
static bool wf_aberta, wf_modo_senha;
static char wf_alvo[REDE_NOME_MAX];
static rede_achada_t wf_achadas[REDE_MAX_ACHADAS];
static int wf_n;

static void atualiza_wifi(void);

// ---- desenhinhos que a fonte não tem ----
static void cadeado(lv_obj_t *pai)
{
    lv_obj_t *c = caixa(pai);
    lv_obj_set_size(c, 16, 20);
    lv_obj_t *arco = caixa(c);              // o corpo, desenhado depois, tapa a metade de baixo
    lv_obj_set_size(arco, 12, 14);
    lv_obj_set_pos(arco, 2, 0);
    lv_obj_set_style_radius(arco, 6, 0);
    lv_obj_set_style_border_width(arco, 2, 0);
    lv_obj_set_style_border_color(arco, C_FRACO, 0);
    lv_obj_t *corpo = caixa(c);
    lv_obj_set_size(corpo, 16, 12);
    lv_obj_set_pos(corpo, 0, 8);
    lv_obj_set_style_radius(corpo, 3, 0);
    lv_obj_set_style_bg_color(corpo, C_FRACO, 0);
    lv_obj_set_style_bg_opa(corpo, LV_OPA_COVER, 0);
}

static void barrinhas(lv_obj_t *pai, int cheias)
{
    lv_obj_t *b = caixa(pai);
    lv_obj_set_size(b, 29, 20);
    for (int i = 0; i < 4; i++) {
        int h = 5 + i * 5;
        lv_obj_t *t = caixa(b);
        lv_obj_set_size(t, 5, h);
        lv_obj_set_pos(t, i * 8, 20 - h);
        lv_obj_set_style_radius(t, 2, 0);
        lv_obj_set_style_bg_color(t, i < cheias ? C_TINTA : C_LINHA, 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    }
}

// ---- entrar e sair ----
static void abre_senha(void)
{
    wf_modo_senha = true;
    lv_textarea_set_text(wf_campo, "");
    lv_textarea_set_password_mode(wf_campo, true);
    lv_label_set_text(lv_obj_get_child(wf_olho, 0), "mostrar");
    lv_obj_add_state(wf_campo, LV_STATE_FOCUSED);
    teclado_mapa(&wf_tc, 0);
}

static void ev_wifi_abre(lv_event_t *e)
{
    wf_aberta = true;
    wf_modo_senha = false;
    lv_obj_remove_flag(wf_tela, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(caixa_pontos, LV_OBJ_FLAG_HIDDEN);   // aqui não se arrasta pro lado
    rede_procurar();
    atualizar();
}

static void ev_wifi_fecha(lv_event_t *e)
{
    if (wf_modo_senha) { wf_modo_senha = false; atualizar(); return; }   // volta pra lista
    wf_aberta = false;
    lv_obj_add_flag(wf_tela, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(caixa_pontos, LV_OBJ_FLAG_HIDDEN);
    atualizar();
}

static void ev_wifi_procurar(lv_event_t *e) { rede_procurar(); atualizar(); }

static void ev_wifi_esquecer(lv_event_t *e) { rede_esquecer(); rede_procurar(); atualizar(); }

static void ev_wifi_escolhe(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= wf_n) return;
    strlcpy(wf_alvo, wf_achadas[i].nome, sizeof wf_alvo);
    if (wf_achadas[i].com_senha) abre_senha();
    else rede_conectar(wf_alvo, "");
    lv_async_call(atualizar_depois, NULL);   // a linha tocada some junto com a lista
}

static void ev_wifi_olho(lv_event_t *e)
{
    bool escondida = lv_textarea_get_password_mode(wf_campo);
    lv_textarea_set_password_mode(wf_campo, !escondida);
    lv_label_set_text(lv_obj_get_child(wf_olho, 0), escondida ? "esconder" : "mostrar");
}

static void wifi_conecta(void)
{
    rede_conectar(wf_alvo, lv_textarea_get_text(wf_campo));
    // O teclado sai da frente na hora. Entrar na rede leva alguns segundos, e
    // um balãozinho atrás do teclado é aviso fraco demais: some o teclado, e a
    // resposta passa a ser a linha de cima, grande, junto com a cara dela.
    wf_modo_senha = false;
    atualizar();
}

static void wifi_cancela(void) { ev_wifi_fecha(NULL); }

// ---- montagem ----
static void linha_rede(const rede_achada_t *r, int i, bool conectada)
{
    lv_obj_t *l = caixa(wf_lista);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_bg_color(l, C_CARTAO, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, 14, 0);
    lv_obj_set_style_pad_hor(l, 20, 0);
    lv_obj_set_style_pad_ver(l, 15, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(l, 14, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(l, ev_wifi_escolhe, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    if (conectada) {
        lv_obj_set_style_outline_color(l, C_OLIVA, 0);
        lv_obj_set_style_outline_width(l, 2, 0);
    }

    lv_obj_t *nome = texto(l, r->nome, f_corpo, C_TINTA);
    lv_obj_set_flex_grow(nome, 1);
    lv_label_set_long_mode(nome, LV_LABEL_LONG_DOT);
    if (conectada) texto(l, "conectada", f_corpo_p, C_OLIVA);
    if (r->com_senha) cadeado(l);
    barrinhas(l, r->barras);
}

static const char *wf_fala(rede_estado_t e)
{
    if (wf_modo_senha)
        return e == REDE_SENHA_ERRADA ? "Não bateu. Acontece — tenta de novo, sem pressa."
             : e == REDE_CONECTANDO   ? "Tentando entrar…"
             : "Digita aí. Eu guardo, pra você não ter que lembrar de novo.";
    switch (e) {
    case REDE_SEM_RADIO:    return "O rádio não acordou hoje. Eu funciono sem ele, viu?";
    case REDE_PROCURANDO:   return "Deixa eu ver quem tá por perto…";
    case REDE_CONECTANDO:   return "Tentando entrar…";
    case REDE_CONECTADA:    return "Entrei! Agora eu pego a hora certa sozinha.";
    case REDE_SENHA_ERRADA: return "A senha não bateu. Toca na rede de novo pra tentar outra?";
    case REDE_SUMIU:        return "Não achei essa rede aqui. Ela tá ligada?";
    default:                return "Escolhe uma rede aí. Eu tô só olhando.";
    }
}

static anim_t wf_anim(rede_estado_t e)
{
    if (e == REDE_CONECTADA && !wf_modo_senha) return A_COMEMORANDO;
    if (e == REDE_SEM_RADIO || e == REDE_SENHA_ERRADA || e == REDE_SUMIU) return A_CUIDADORA;
    return A_PENSANDO;      // curiosa, esperando a conexão
}

static void atualiza_wifi(void)
{
    rede_estado_t e = rede_estado();
    if (wf_modo_senha && e == REDE_CONECTADA) wf_modo_senha = false;   // deu certo: volta pra lista

    char s[96];
    switch (e) {
    case REDE_SEM_RADIO:    strcpy(s, "o rádio não respondeu"); break;
    case REDE_PROCURANDO:   strcpy(s, "procurando…"); break;
    case REDE_CONECTANDO:   snprintf(s, sizeof s, "entrando em \"%s\"…", rede_nome()); break;
    case REDE_CONECTADA:    snprintf(s, sizeof s, "conectada  ·  %s", rede_ip()); break;
    case REDE_SENHA_ERRADA: strcpy(s, "a senha não bateu"); break;
    case REDE_SUMIU:        snprintf(s, sizeof s, "\"%s\" não apareceu", rede_nome()); break;
    default:                strcpy(s, "desconectada"); break;
    }
    lv_label_set_text(wf_status, s);
    lv_obj_set_style_text_color(wf_status, e == REDE_CONECTADA ? C_OLIVA :
                                           e == REDE_SENHA_ERRADA ? C_AMBAR : C_FRACO, 0);

    // ---- lista ou senha ----
    lv_obj_t *da_lista[] = { wf_lista, wf_recado, wf_procurar, wf_esquecer, wf_balao, wf_status };
    lv_obj_t *da_senha[] = { wf_senha_titulo, wf_campo, wf_olho, wf_teclado, wf_balao_senha };
    for (size_t i = 0; i < sizeof da_lista / sizeof *da_lista; i++)
        if (wf_modo_senha) lv_obj_add_flag(da_lista[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(da_lista[i], LV_OBJ_FLAG_HIDDEN);
    for (size_t i = 0; i < sizeof da_senha / sizeof *da_senha; i++)
        if (wf_modo_senha) lv_obj_remove_flag(da_senha[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(da_senha[i], LV_OBJ_FLAG_HIDDEN);

    if (wf_modo_senha) {
        snprintf(s, sizeof s, "Senha de \"%s\"", wf_alvo);
        lv_label_set_text(wf_senha_titulo, s);
        balao_texto(wf_balao_senha, wf_fala(e));
        lylu_em(wf_tela, 656, 10);
        // Os 360x360 dela (transparentes, mas tocáveis por causa do carinho) cobrem
        // o botão "mostrar": o toque ia pra ela e a senha nunca aparecia.
        lv_obj_remove_flag(lylu, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(lylu_outra, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_foreground(wf_teclado);     // ela fica atrás do teclado, espiando
    } else {
        wf_n = rede_copia_lista(wf_achadas, REDE_MAX_ACHADAS);
        lv_obj_clean(wf_lista);
        for (int i = 0; i < wf_n; i++)
            linha_rede(&wf_achadas[i], i, e == REDE_CONECTADA && !strcmp(wf_achadas[i].nome, rede_nome()));

        if (wf_n) lv_obj_add_flag(wf_recado, LV_OBJ_FLAG_HIDDEN);
        else lv_label_set_text(wf_recado, e == REDE_PROCURANDO ? "Procurando redes por perto…"
                                        : e == REDE_SEM_RADIO  ? "Sem rádio: essa placa não achou o chip de Wi-Fi."
                                        : "Nenhuma rede por aqui. Toca em \"Procurar de novo\".");
        if (rede_tem_salva()) lv_obj_remove_flag(wf_esquecer, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(wf_esquecer, LV_OBJ_FLAG_HIDDEN);
        balao_texto(wf_balao, wf_fala(e));
        lylu_em(wf_tela, 648, 176);
    }
    lylu_mostra(wf_anim(e));
}

// A linha dos Ajustes conta o estado sem precisar entrar na tela.
static void atualiza_linha_wifi(void)
{
    char s[96];
    switch (rede_estado()) {
    case REDE_CONECTADA:    snprintf(s, sizeof s, "%s  ·  %s", rede_nome(), rede_ip()); break;
    case REDE_SEM_RADIO:    strcpy(s, "O rádio não respondeu — ela segue sem internet"); break;
    case REDE_SENHA_ERRADA: snprintf(s, sizeof s, "A senha de \"%s\" não bateu", rede_nome()); break;
    case REDE_SUMIU:        snprintf(s, sizeof s, "Esperando \"%s\" aparecer", rede_nome()); break;
    case REDE_CONECTANDO:   snprintf(s, sizeof s, "Entrando em \"%s\"…", rede_nome()); break;
    default:                strcpy(s, "Toque para escolher uma rede"); break;
    }
    lv_label_set_text(aj_wifi_sub, s);
}

static void cria_wifi(void)
{
    wf_tela = caixa(lv_layer_top());
    lv_obj_set_size(wf_tela, TELA_W, WF_ALTURA);
    lv_obj_set_pos(wf_tela, 0, WF_TOPO);
    lv_obj_set_style_bg_color(wf_tela, C_FUNDO, 0);
    lv_obj_set_style_bg_opa(wf_tela, LV_OPA_COVER, 0);
    lv_obj_add_flag(wf_tela, LV_OBJ_FLAG_CLICKABLE);    // segura o arrastar do tileview
    lv_obj_add_flag(wf_tela, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *voltar = botao(wf_tela, "Voltar", false, ev_wifi_fecha);
    lv_obj_set_pos(voltar, 34, 12);
    lv_obj_t *tit = texto(wf_tela, "Wi-Fi", f_px_m, C_TINTA);
    lv_obj_set_pos(tit, 196, 18);
    // Ao lado do título, não na direita: lá ela passava por baixo do balão da Lylu.
    lv_obj_set_pos(sinal_cria(wf_tela), 330, 33);
    wf_status = texto(wf_tela, "", f_corpo, C_FRACO);
    lv_obj_set_width(wf_status, 290);
    lv_label_set_long_mode(wf_status, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(wf_status, 372, 30);

    // ---- lista ----
    wf_lista = caixa(wf_tela);
    lv_obj_set_size(wf_lista, 600, 380);
    lv_obj_set_pos(wf_lista, 34, 80);
    lv_obj_add_flag(wf_lista, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wf_lista, LV_DIR_VER);
    lv_obj_set_flex_flow(wf_lista, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wf_lista, 10, 0);

    wf_recado = texto(wf_tela, "", f_corpo, C_FRACO);
    lv_obj_set_pos(wf_recado, 34, 96);

    wf_procurar = botao(wf_tela, "Procurar de novo", false, ev_wifi_procurar);
    lv_obj_set_pos(wf_procurar, 34, 478);
    wf_esquecer = botao(wf_tela, "Esquecer esta rede", false, ev_wifi_esquecer);
    lv_obj_set_pos(wf_esquecer, 294, 478);

    wf_balao = balao(wf_tela, 674, 26, 240);

    // ---- senha ----
    wf_senha_titulo = texto(wf_tela, "", f_corpo_g, C_TINTA);
    lv_obj_set_pos(wf_senha_titulo, 34, 76);

    wf_campo = lv_textarea_create(wf_tela);
    lv_textarea_set_one_line(wf_campo, true);   // antes do tamanho: ele deixa a altura solta
    lv_obj_set_size(wf_campo, 552, 64);
    lv_obj_set_pos(wf_campo, 34, 122);
    lv_textarea_set_password_mode(wf_campo, true);
    lv_textarea_set_password_bullet(wf_campo, "*");   // as fontes daqui não têm o bolinha
    lv_textarea_set_placeholder_text(wf_campo, "a senha da rede");
    lv_obj_set_style_bg_color(wf_campo, C_CARTAO, 0);
    lv_obj_set_style_border_color(wf_campo, C_LINHA, 0);
    lv_obj_set_style_border_width(wf_campo, 2, 0);
    lv_obj_set_style_radius(wf_campo, 12, 0);
    lv_obj_set_style_text_color(wf_campo, C_TINTA, 0);
    lv_obj_set_style_text_font(wf_campo, f_corpo_g, 0);

    wf_olho = botao(wf_tela, "mostrar", false, ev_wifi_olho);
    lv_obj_set_pos(wf_olho, 606, 130);

    wf_balao_senha = balao(wf_tela, 34, 204, 500);

    wf_tc.campo = wf_campo;
    wf_tc.ok = wifi_conecta;
    wf_tc.cancela = wifi_cancela;
    teclado_cria(&wf_tc, wf_tela, 288, "Conectar");
    wf_teclado = wf_tc.bm;
}

// A rede avisa da thread de eventos do ESP-IDF, não da thread da tela. Esperar
// a trava da tela aqui (lvgl_port_lock espera para sempre) segurava a thread de
// eventos do Wi-Fi inteira enquanto a tela desenhava — e com telas pesadas o
// DHCP não fechava: associava e o IP nunca chegava. Agora ela só levanta uma
// bandeira, e um timer da própria tela olha a bandeira.
static volatile bool rede_mudou;
static void ev_rede(void) { rede_mudou = true; }

static void olha_rede(lv_timer_t *t)
{
    if (!rede_mudou) return;
    rede_mudou = false;
    atualizar();
}

// ---------------------------------------------------------------- MICROFONE
// Também mora dentro dos Ajustes, como o Wi-Fi. É teste de hardware e presença
// ao mesmo tempo: a barra prova que o som entra, e ela reagir prova por quê isso
// importa — a Lylu percebe quando falam com ela.

#define MF_BARRAS 14

static lv_obj_t *mf_tela, *mf_status, *mf_balao, *mf_barra[MF_BARRAS], *aj_mic_sub;
static bool mf_aberta, mf_ouvindo;

static void ev_mic_abre(lv_event_t *e)
{
    mf_aberta = true;
    lv_obj_remove_flag(mf_tela, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(caixa_pontos, LV_OBJ_FLAG_HIDDEN);
    atualizar();
}

static void ev_mic_fecha(lv_event_t *e)
{
    mf_aberta = false;
    lv_obj_add_flag(mf_tela, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(caixa_pontos, LV_OBJ_FLAG_HIDDEN);
    atualizar();
}

static void atualiza_microfone(void)
{
    int n = audio_nivel();
    // Uma folga entre subir e descer, senão ela fica trocando de cara a cada sílaba.
    if (n > 35) mf_ouvindo = true;
    else if (n < 12) mf_ouvindo = false;
    if (!audio_escutando()) mf_ouvindo = false;

    int acesos = n * MF_BARRAS / 100;
    for (int i = 0; i < MF_BARRAS; i++) {
        bool aceso = i < acesos;
        lv_color_t c = !aceso ? C_LINHA : i < MF_BARRAS - 4 ? C_OLIVA : C_AMBAR;
        lv_obj_set_style_bg_color(mf_barra[i], c, 0);
    }

    lv_label_set_text(mf_status, audio_escutando() ? (mf_ouvindo ? "te ouvindo" : "escutando…")
                                                   : "o microfone não subiu");
    lv_obj_set_style_text_color(mf_status, mf_ouvindo ? C_AMBAR : C_FRACO, 0);
    balao_texto(mf_balao, !audio_escutando() ? "Não consegui abrir os ouvidos dessa vez."
                        : mf_ouvindo         ? "Tô te ouvindo!"
                                             : "Fala alguma coisa. Eu tô aqui.");
    lylu_em(mf_tela, 650, 150);
    lylu_mostra(!audio_escutando() ? A_CUIDADORA : mf_ouvindo ? A_RINDO : A_PENSANDO);
}

// A barra precisa de mão mais leve que o resto: atualizar a tela inteira 16x por
// segundo seria desperdício, então só ela se refaz nesse ritmo.
static void passo_microfone(lv_timer_t *t)
{
    if (mf_aberta) atualiza_microfone();
}

static void atualiza_linha_mic(void)
{
    lv_label_set_text(aj_mic_sub, audio_escutando() ? "Toque para ver se ela te ouve"
                                                    : "Não subiu — ela segue surda");
}

static void cria_microfone(void)
{
    mf_tela = caixa(lv_layer_top());
    lv_obj_set_size(mf_tela, TELA_W, WF_ALTURA);
    lv_obj_set_pos(mf_tela, 0, WF_TOPO);
    lv_obj_set_style_bg_color(mf_tela, C_FUNDO, 0);
    lv_obj_set_style_bg_opa(mf_tela, LV_OPA_COVER, 0);
    lv_obj_add_flag(mf_tela, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mf_tela, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *voltar = botao(mf_tela, "Voltar", false, ev_mic_fecha);
    lv_obj_set_pos(voltar, 34, 12);
    lv_obj_t *tit = texto(mf_tela, "Microfone", f_px_m, C_TINTA);
    lv_obj_set_pos(tit, 196, 18);
    mf_status = texto(mf_tela, "", f_corpo, C_FRACO);
    lv_obj_set_pos(mf_status, 420, 30);

    // A barra: blocos que acendem da esquerda para a direita.
    lv_obj_t *caixa_barra = caixa(mf_tela);
    lv_obj_set_pos(caixa_barra, 50, 210);
    lv_obj_set_flex_flow(caixa_barra, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(caixa_barra, 8, 0);
    for (int i = 0; i < MF_BARRAS; i++) {
        mf_barra[i] = caixa(caixa_barra);
        lv_obj_set_size(mf_barra[i], 34, 110);
        lv_obj_set_style_radius(mf_barra[i], 8, 0);
        lv_obj_set_style_bg_color(mf_barra[i], C_LINHA, 0);
        lv_obj_set_style_bg_opa(mf_barra[i], LV_OPA_COVER, 0);
    }

    lv_obj_t *dica = texto(mf_tela, "O microfone é da própria placa. Fala perto dela.", f_corpo_p, C_FRACO);
    lv_obj_set_pos(dica, 50, 350);

    mf_balao = balao(mf_tela, 640, 20, 250);
}

// ---------------------------------------------------------------- AJUSTES
// Tudo numa lista só, com risquinho entre as linhas, e embaixo três cartõezinhos
// com o estado da internet de verdade (nada de "online" inventado).
static lv_obj_t *aj_brilho_v, *aj_info[3];

static lv_obj_t *linha_ajuste(lv_obj_t *pai, const char *nome, const char *sub)
{
    lv_obj_t *l = caixa(pai);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_ver(l, 9, 0);
    lv_obj_set_style_border_side(l, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(l, 1, 0);
    lv_obj_set_style_border_color(l, C_LINHA, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(l, 12, 0);
    lv_obj_t *txt = caixa(l);
    lv_obj_set_flex_grow(txt, 1);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    texto(txt, nome, f_negrito_p, C_TINTA);
    if (sub) {
        lv_obj_t *s = texto(txt, sub, f_corpo_p, C_FRACO);
        lv_obj_set_width(s, 380);
        lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    }
    return l;
}

static lv_obj_t *chave(lv_obj_t *pai, bool ligada)
{
    lv_obj_t *sw = lv_switch_create(pai);
    lv_obj_set_size(sw, 56, 30);
    lv_obj_set_style_bg_color(sw, C_LINHA, 0);
    lv_obj_set_style_bg_color(sw, C_OLIVA, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (ligada) lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
}

static void ev_dificil(lv_event_t *e)
{
    dificil = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (!dificil) cortada = false;
    atualizar();
}

static void ev_brilho(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    placa_brilho(v);
    char s[8];
    snprintf(s, sizeof s, "%d%%", v);
    lv_label_set_text(aj_brilho_v, s);
}

static void cria_ajustes(lv_obj_t *t)
{
    cabeca(t, 44, "CONFIGURAÇÕES", "Do seu jeito", NULL, 560);

    lv_obj_t *lista = cartao(t);
    lv_obj_set_pos(lista, 44, 168);
    lv_obj_set_width(lista, 560);
    lv_obj_set_style_pad_hor(lista, 20, 0);
    lv_obj_set_style_pad_ver(lista, 2, 0);
    lv_obj_set_flex_flow(lista, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *lw = linha_ajuste(lista, "Wi-Fi", "");
    aj_wifi_sub = lv_obj_get_child(lv_obj_get_child(lw, 0), 1);
    sinal_cria(lw);
    texto(lw, ">", f_px_p, C_FRACO);
    lv_obj_add_flag(lw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lw, ev_wifi_abre, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lm = linha_ajuste(lista, "Microfone", "");
    aj_mic_sub = lv_obj_get_child(lv_obj_get_child(lm, 0), 1);
    texto(lm, ">", f_px_p, C_FRACO);
    lv_obj_add_flag(lm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lm, ev_mic_abre, LV_EVENT_CLICKED, NULL);

    aj_dificil = chave(linha_ajuste(lista, "Modo dia difícil", "Só a missão aparece, sem cutucadas"), false);
    lv_obj_add_event_cb(aj_dificil, ev_dificil, LV_EVENT_VALUE_CHANGED, NULL);
    chave(linha_ajuste(lista, "Silêncio no foco", "Nenhum som enquanto o timer roda"), true);

    lv_obj_t *lb = linha_ajuste(lista, "Brilho da tela", "Arrasta pra deixar confortável");
    lv_obj_set_style_border_width(lb, 0, 0);            // a última, sem risquinho
    lv_obj_t *s = lv_slider_create(lb);
    lv_obj_set_width(s, 150);
    lv_slider_set_range(s, 10, 100);
    lv_slider_set_value(s, 95, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, C_LINHA, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, C_OLIVA, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, C_TINTA, LV_PART_KNOB);
    lv_obj_add_event_cb(s, ev_brilho, LV_EVENT_VALUE_CHANGED, NULL);
    aj_brilho_v = texto(lb, "95%", f_negrito_p, C_TINTA);
    lv_obj_set_width(aj_brilho_v, 52);
    lv_obj_set_style_text_align(aj_brilho_v, LV_TEXT_ALIGN_RIGHT, 0);

    static const char *INFO[3] = { "Internet", "Rede", "Endereço" };
    lv_obj_t *linha = caixa(t);
    lv_obj_set_pos(linha, 44, 470);
    lv_obj_set_flex_flow(linha, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(linha, 10, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = cartao(linha);
        lv_obj_set_width(c, 180);
        lv_obj_set_style_pad_hor(c, 14, 0);
        lv_obj_set_style_pad_ver(c, 10, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        texto(c, INFO[i], f_corpo_p, C_FRACO);
        aj_info[i] = texto(c, "", f_negrito_p, C_TINTA);
        lv_obj_set_width(aj_info[i], LV_PCT(100));
        lv_label_set_long_mode(aj_info[i], LV_LABEL_LONG_DOT);
    }

    lv_obj_t *b = balao(t, 652, 72, 290);
    balao_fala(b, "Se alguma coisa incomodar, a gente muda.", "A tela é sua.");
}

static void atualiza_ajustes(void)
{
    bool on = rede_estado() == REDE_CONECTADA;
    lv_label_set_text(aj_info[0], on ? "conectada" : rede_estado() == REDE_CONECTANDO ? "entrando…" : "sem internet");
    lv_obj_set_style_text_color(aj_info[0], on ? C_OLIVA : C_AMBAR, 0);
    lv_label_set_text(aj_info[1], rede_nome()[0] ? rede_nome() : "nenhuma");
    lv_label_set_text(aj_info[2], on ? rede_ip() : "—");
}

// ---------------------------------------------------------------- humor e atualização geral
static const char *humor(lv_color_t *cor)
{
    if (dificil) { *cor = C_CEU; return "cuidadora"; }
    if (tela_atual == T_FOCO) {
        switch (passo) {
        case P_CONFIGURAR: *cor = C_CEU;   return "curiosa";
        case P_FOCO:       *cor = C_OLIVA; return "concentrada";
        case P_PAUSA:      *cor = C_CEU;   return "cuidadora";
        default: break;
        }
    }
    if (missao_completa()) { *cor = C_AMBAR; return "comemorando"; }
    if (tela_atual == T_RECADOS) { *cor = C_CEU; return "cuidadora"; }
    if (tela_atual == T_RELOGIO) { *cor = C_LILAS; return "tranquila"; }
    if (tela_atual == T_CASA)    { *cor = C_LILAS; return "descansando"; }
    if (tela_atual == T_AJUSTES) { *cor = C_CEU; return "atenta"; }
    *cor = C_OLIVA; return "animada";
}

static void atualizar(void)
{
    char s[16];
    snprintf(s, sizeof s, "%d/%d", missao_feita(), n_missao());
    lv_label_set_text(st_missao, s);
    lv_color_t cor;
    lv_label_set_text(lv_obj_get_child(st_humor, 1), humor(&cor));
    lv_obj_set_style_bg_color(st_humor_ponto, cor, 0);
    if (dificil) lv_obj_remove_flag(st_gentil, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(st_gentil, LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_has_state(aj_dificil, LV_STATE_CHECKED) != dificil) {
        if (dificil) lv_obj_add_state(aj_dificil, LV_STATE_CHECKED);
        else lv_obj_remove_state(aj_dificil, LV_STATE_CHECKED);
    }
    for (int i = 0; i < N_TELAS; i++) {
        lv_obj_set_width(pontos[i], i == tela_atual ? 26 : 10);
        lv_obj_set_style_bg_color(pontos[i], i == tela_atual ? C_TINTA : C_LINHA, 0);
    }

    atualiza_tarefas();
    atualiza_recados();
    atualiza_pomodoro();
    atualiza_linha_wifi();
    atualiza_linha_mic();
    // Estas só se refazem quando estão à vista: ao chegar nelas, a troca de tela
    // chama atualizar() de novo.
    switch (tela_atual) {
    case T_SEMANA:  atualiza_semana(); break;
    case T_RELOGIO: atualiza_relogio(); break;
    case T_CASA:    atualiza_casa(); break;
    case T_AJUSTES: atualiza_ajustes(); break;
    }

    if (wf_aberta) { atualiza_wifi(); return; }
    if (mf_aberta) { atualiza_microfone(); return; }

    switch (tela_atual) {
    case T_FOCO:    lylu_na_tela(T_FOCO, 640, 196); lylu_mostra(anim_foco()); break;
    case T_RECADOS: lylu_na_tela(T_RECADOS, 672, 178); lylu_mostra(A_FALA); break;
    case T_TAREFAS: lylu_na_tela(T_TAREFAS, 20, 190); lylu_mostra(A_FALA); break;
    case T_CASA:    lylu_na_tela(T_CASA, (int)casa.x, CASA_Y); lylu_mostra(casa.fazendo); break;
    case T_RELOGIO: lylu_na_tela(T_RELOGIO, 672, 178); lylu_mostra(A_FALA); break;
    case T_SEMANA:  lylu_na_tela(T_SEMANA, 672, 178); lylu_mostra(A_FALA); break;
    case T_AJUSTES: lylu_na_tela(T_AJUSTES, 672, 178); lylu_mostra(A_FALA); break;
    }
}

static void ev_troca_tela(lv_event_t *e)
{
    lv_obj_t *ativa = lv_tileview_get_tile_active(tv);
    for (int i = 0; i < N_TELAS; i++) if (tiles[i] == ativa) tela_atual = i;
    recado_adiado = false;   // o "tá bom, te lembro depois" vale só pra quem acabou de tocar
    casa_respiro = -1;
    atualizar();
}

// Rede de segurança: se por qualquer motivo a Lylu da frente ficou sem GIF ou
// escondida, conta no log o estado em que ela estava e a põe de volta.
static void vigia_lylu(void)
{
    lv_gif_t *g = (lv_gif_t *)lylu;
    bool sem_gif = !g->gif, escondida = lv_obj_has_flag(lylu, LV_OBJ_FLAG_HIDDEN);
    if (!sem_gif && !escondida) return;
    ESP_LOGW(TAG, "Lylu sumiu: anim %d, sem_gif %d, escondida %d, opa %d, x %d, y %d",
             (int)lylu_anim, sem_gif, escondida, (int)lv_obj_get_style_image_opa(lylu, 0),
             (int)lv_obj_get_x(lylu), (int)lv_obj_get_y(lylu));
    loga_memoria("vigia");
    lv_anim_delete(lylu, opacidade);
    guarda_a_que_saiu();
    lylu_anim = A_TOTAL;     // força reabrir o GIF do zero, sem passagem
    atualizar();
}

static void tique(lv_timer_t *t)
{
    atualiza_hora();
    vigia_lylu();
    static int seg;
    if (++seg % 300 == 0) loga_memoria("a cada 5 min");
    if (foco.rodando && --foco.resto <= 0) fim_do_foco();
    else if (pausa.rodando && --pausa.resto <= 0) fim_da_pausa();
    else if (tela_atual == T_FOCO) tique_pomodoro();
}

// ---------------------------------------------------------------- hora inicial
// Sem internet ainda: o relógio começa na hora em que o programa foi compilado.
static void acerta_hora_inicial(void)
{
    setenv("TZ", "<-03>3", 1);   // Brasília; sem isso o NTP traria a hora de Londres
    tzset();
    static const char *M = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mes[4] = { 0 };
    struct tm tm = { 0 };
    sscanf(__DATE__, "%3s %d %d", mes, &tm.tm_mday, &tm.tm_year);
    sscanf(__TIME__, "%d:%d:%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_mon = (strstr(M, mes) - M) / 3;
    tm.tm_year -= 1900;
    struct timeval tv0 = { .tv_sec = mktime(&tm) };
    settimeofday(&tv0, NULL);
}

// ---------------------------------------------------------------- início
void app_main(void)
{
    // Ela reinicia sozinha às vezes, sem pânico no log (a porta USB cai junto e
    // leva a mensagem). O motivo fica guardado no chip e só dá pra ler aqui.
    static const char *MOTIVO[] = { "desconhecido", "ligou na tomada", "pino EN/RESET", "esp_restart",
                                    "PANICO (erro no programa)", "watchdog de interrupcao",
                                    "watchdog de tarefa", "outro watchdog", "saiu do deep sleep",
                                    "QUEDA DE TENSAO (brownout)", "SDIO", "USB", "JTAG", "eFuse",
                                    "falha de energia", "CPU travada" };
    esp_reset_reason_t r = esp_reset_reason();
    ESP_LOGW(TAG, "ligou por: %s (%d)", r < sizeof MOTIVO / sizeof *MOTIVO ? MOTIVO[r] : "?", (int)r);

    acerta_hora_inicial();
    placa_iniciar();
    carrega_gifs();

    placa_trava();
    carrega_fontes();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_FUNDO, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < N_TELAS; i++) {
        lv_dir_t d = i == 0 ? LV_DIR_RIGHT : i == N_TELAS - 1 ? LV_DIR_LEFT : LV_DIR_HOR;
        tiles[i] = lv_tileview_add_tile(tv, i, 0, d);
        lv_obj_remove_flag(tiles[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_event_cb(tv, ev_troca_tela, LV_EVENT_VALUE_CHANGED, NULL);

    cria_status();
    cria_pontos();
    cria_pomodoro(tiles[T_FOCO]);
    cria_tarefas(tiles[T_TAREFAS]);
    cria_recados(tiles[T_RECADOS]);
    cria_casa(tiles[T_CASA]);
    cria_relogio(tiles[T_RELOGIO]);
    cria_semana(tiles[T_SEMANA]);
    cria_ajustes(tiles[T_AJUSTES]);
    cria_wifi();
    cria_microfone();
    cria_digitar();

    lylu = lv_gif_create(tiles[T_FOCO]);
    lylu_outra = lv_gif_create(tiles[T_FOCO]);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *g = i ? lylu_outra : lylu;
        lv_obj_add_flag(g, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(g, ev_carinho, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_add_flag(lylu_outra, LV_OBJ_FLAG_HIDDEN);

    atualiza_hora();
    atualizar();
    lv_timer_create(tique, 1000, NULL);
    lv_timer_create(passo_casa, 33, NULL);
    lv_timer_create(passo_microfone, 60, NULL);
    lv_timer_create(sinal_pinta, 350, NULL);
    lv_timer_create(olha_rede, 200, NULL);
    placa_destrava();

    audio_iniciar();         // ES8311: se não responder, ela segue surda e nada quebra
    rede_iniciar(ev_rede);   // demora uns segundos e pode não achar o C6; a tela já está de pé
    // Os tempos do pomodoro moram na NVS, e quem abre a NVS é a rede. Abrir
    // antes dela, no começo do app_main, derrubava o DHCP: associava na rede e o
    // IP nunca chegava (três boots seguidos; só mudar isto de lugar resolveu).
    if (placa_trava()) { pomo_carrega(); atualizar(); placa_destrava(); }

    ESP_LOGI(TAG, "Lylu pronta");
}
