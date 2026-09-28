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
#include "gif/lv_gif_private.h"   // copia corrigida do GIF da LVGL (ver gif/gifdec.c)
#include "audio.h"
#include "placa.h"
#include "rede.h"

static const char *TAG = "lylu";

// ---------------------------------------------------------------- cores
#define C_FUNDO   lv_color_hex(0x1d1b26)
#define C_CARTAO  lv_color_hex(0x292636)
#define C_LINHA   lv_color_hex(0x3b3752)
#define C_TINTA   lv_color_hex(0xf1eef8)
#define C_FRACO   lv_color_hex(0xa39db8)
#define C_OLIVA   lv_color_hex(0xb8c55c)
#define C_AMBAR   lv_color_hex(0xf4b860)
#define C_CEU     lv_color_hex(0x8ec5ff)
#define C_LILAS   lv_color_hex(0xb9a6ff)
#define C_ESCURO  lv_color_hex(0x1f2310)

// ---------------------------------------------------------------- arquivos embutidos
#define EMB(nome) extern const uint8_t nome##_start[] asm("_binary_" #nome "_start"); \
                  extern const uint8_t nome##_end[]   asm("_binary_" #nome "_end");
EMB(concentrada_frente_gif) EMB(comemorando_v2_gif) EMB(pensando_gif) EMB(tomando_cafe_frente_gif)
EMB(apontando_lista_gif) EMB(cuidadora_v2_gif) EMB(rindo_carinho_frente_gif)
EMB(andando_direita_v2_gif) EMB(andando_esquerda_v2_gif) EMB(lendo_frente_gif)
EMB(soprando_bolhas_frente_gif) EMB(brincando_frente_gif) EMB(bocejando_frente_gif)
EMB(dormindo_frente_gif) EMB(tomando_agua_frente_gif) EMB(jogando_nintendo_frente_gif)
EMB(corpo_ttf) EMB(corpo_negrito_ttf) EMB(pixel_ttf)

typedef enum {
    A_CONCENTRADA, A_COMEMORANDO, A_PENSANDO, A_CAFE, A_APONTANDO, A_CUIDADORA, A_RINDO,
    A_ANDA_DIR, A_ANDA_ESQ, A_LENDO, A_BOLHAS, A_BRINCANDO, A_BOCEJANDO, A_DORMINDO,
    A_AGUA, A_NINTENDO, A_TOTAL
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
}

// ---------------------------------------------------------------- fontes
static lv_font_t *f_corpo, *f_corpo_p, *f_corpo_g, *f_negrito, *f_px_p, *f_px_m, *f_px_g, *f_px_relogio, *f_px_foco;

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
    f_px_p    = ttf(pixel_ttf_start, pixel_ttf_end, 20);
    f_px_m    = ttf(pixel_ttf_start, pixel_ttf_end, 38);
    f_px_g    = ttf(pixel_ttf_start, pixel_ttf_end, 60);
    f_px_foco = ttf(pixel_ttf_start, pixel_ttf_end, 84);
    f_px_relogio = ttf(pixel_ttf_start, pixel_ttf_end, 210);
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

#define FOCO_TOTAL (25 * 60)
static struct { bool rodando, acabou; int resto, blocos; } foco = { false, false, FOCO_TOTAL, 1 };

enum { T_FOCO, T_TAREFAS, T_CASA, T_RELOGIO, T_SEMANA, T_AJUSTES, N_TELAS };
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
static lv_obj_t *foco_arco, *foco_num, *foco_sub, *foco_tarefa, *foco_btn, *foco_btn_txt, *foco_parar, *foco_blocos, *foco_balao;
static lv_obj_t *tar_balao, *lista_missao, *lista_bonus, *caixa_bonus, *btn_cortar;
static lv_obj_t *rel_hora, *rel_seg, *rel_data;
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
    lv_obj_set_style_bg_color(b, C_TINTA, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_pad_ver(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = texto(b, "", f_corpo, lv_color_hex(0x1d1b26));
    lv_obj_set_width(l, largura);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return b;
}

static void balao_texto(lv_obj_t *b, const char *s)
{
    if (!s || !*s) { lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lv_obj_get_child(b, 0), s);
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

// ---------------------------------------------------------------- FOCO
static void ev_foco_btn(lv_event_t *e)
{
    if (foco.acabou) { foco.resto = FOCO_TOTAL; foco.acabou = false; }
    foco.rodando = !foco.rodando;
    atualizar();
}

static void ev_foco_parar(lv_event_t *e)
{
    foco.rodando = false; foco.acabou = false; foco.resto = FOCO_TOTAL;
    atualizar();
}

static void cria_foco(lv_obj_t *t)
{
    lv_obj_t *col = caixa(t);
    lv_obj_set_size(col, 470, 520);
    lv_obj_set_pos(col, 20, 64);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 18, 0);

    foco_arco = lv_arc_create(col);
    lv_obj_set_size(foco_arco, 300, 300);
    lv_arc_set_rotation(foco_arco, 270);
    lv_arc_set_bg_angles(foco_arco, 0, 360);
    lv_arc_set_range(foco_arco, 0, FOCO_TOTAL);
    lv_obj_remove_style(foco_arco, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(foco_arco, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(foco_arco, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(foco_arco, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(foco_arco, C_LINHA, LV_PART_MAIN);
    lv_obj_set_style_arc_color(foco_arco, C_OLIVA, LV_PART_INDICATOR);
    foco_num = texto(foco_arco, "25:00", f_px_foco, C_TINTA);
    lv_obj_center(foco_num);
    foco_sub = texto(foco_arco, "", f_corpo_p, C_FRACO);
    lv_obj_align(foco_sub, LV_ALIGN_BOTTOM_MID, 0, -70);

    lv_obj_t *trab = caixa(col);
    lv_obj_set_flex_flow(trab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(trab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    texto(trab, "trabalhando em", f_corpo_p, C_FRACO);
    foco_tarefa = texto(trab, "", f_negrito, C_TINTA);

    lv_obj_t *bts = caixa(col);
    lv_obj_set_flex_flow(bts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bts, 12, 0);
    foco_btn = botao(bts, "Começar", true, ev_foco_btn);
    foco_btn_txt = lv_obj_get_child(foco_btn, 0);
    foco_parar = botao(bts, "Parar", false, ev_foco_parar);

    foco_blocos = texto(col, "", f_corpo_p, C_FRACO);

    lv_obj_t *mesa = caixa(t);
    lv_obj_set_size(mesa, 460, 8);
    lv_obj_set_pos(mesa, 520, 500);
    lv_obj_set_style_bg_color(mesa, lv_color_hex(0x3a3348), 0);
    lv_obj_set_style_bg_opa(mesa, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mesa, 4, 0);

    foco_balao = balao(t, 560, 64, 300);
}

static void atualiza_foco(void)
{
    char s[64];
    snprintf(s, sizeof s, "%02d:%02d", foco.resto / 60, foco.resto % 60);
    lv_label_set_text(foco_num, s);
    lv_arc_set_value(foco_arco, FOCO_TOTAL - foco.resto);

    bool comecado = foco.resto < FOCO_TOTAL && !foco.acabou;
    lv_label_set_text(foco_btn_txt, foco.rodando ? "Pausar" : comecado ? "Continuar" : "Começar");
    if (foco.rodando || comecado) lv_obj_remove_flag(foco_parar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(foco_parar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(foco_sub, foco.acabou ? "bloco concluído" : foco.rodando ? "foco" : comecado ? "pausado" : "pronta quando você estiver");

    const char *pend = "O que você quiser";
    for (int i = 0; i < n_missao(); i++) if (!missao[i].f) { pend = missao[i].t; break; }
    lv_label_set_text(foco_tarefa, pend);

    if (foco.blocos) snprintf(s, sizeof s, "%d %s de foco hoje.", foco.blocos, foco.blocos == 1 ? "bloco" : "blocos");
    else snprintf(s, sizeof s, "Nenhum bloco de foco ainda hoje. Sem pressa.");
    lv_label_set_text(foco_blocos, s);

    if (foco.acabou) balao_texto(foco_balao, "25 minutos! Levanta, estica, bebe uma água.");
    else if (foco.rodando) balao_texto(foco_balao, NULL);   // quieta: divide a mesa com o seu foco
    else if (comecado) balao_texto(foco_balao, "Pausa é pausa. Tô aqui quando voltar.");
    else balao_texto(foco_balao, dificil ? "Se quiser, dez minutinhos já valem." : "Começar é a parte difícil. Eu começo junto.");
}

static anim_t anim_foco(void)
{
    if (foco.acabou) return A_COMEMORANDO;
    if (foco.rodando) return A_CONCENTRADA;
    if (foco.resto < FOCO_TOTAL) return A_CAFE;
    return A_PENSANDO;
}

// ---------------------------------------------------------------- TAREFAS
// A lista é recriada inteira, inclusive o item tocado: redesenha só depois que o toque termina.
static void atualizar_depois(void *p) { atualizar(); }

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
    lv_obj_set_style_pad_ver(linha, grande ? 9 : 6, 0);
    lv_obj_add_flag(linha, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(linha, ev_tarefa, LV_EVENT_CLICKED, t);

    int lado = grande ? 28 : 22;
    lv_obj_t *cx = caixa(linha);
    lv_obj_set_size(cx, lado, lado);
    lv_obj_set_style_radius(cx, grande ? 8 : 6, 0);
    lv_obj_set_style_border_width(cx, grande ? 3 : 2, 0);
    lv_obj_t *v = texto(cx, LV_SYMBOL_OK, &lv_font_montserrat_14, C_ESCURO);
    lv_obj_center(v);

    lv_obj_t *l = texto(linha, t->t, grande ? f_negrito : f_corpo, C_TINTA);
    lv_obj_set_flex_grow(l, 1);

    lv_color_t c = grande ? C_TINTA : lv_color_hex(0xcfcadf);
    lv_obj_set_style_border_color(cx, t->f ? C_OLIVA : c, 0);
    lv_obj_set_style_bg_color(cx, C_OLIVA, 0);
    lv_obj_set_style_bg_opa(cx, t->f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (!t->f) lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(l, t->f ? C_FRACO : c, 0);
    lv_obj_set_style_text_decor(l, t->f ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);
}

static void cria_tarefas(lv_obj_t *t)
{
    lv_obj_t *col = caixa(t);
    lv_obj_set_size(col, 574, 500);
    lv_obj_set_pos(col, 410, 64);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 22, 0);

    lv_obj_t *m = caixa(col);
    lv_obj_set_width(m, LV_PCT(100));
    lv_obj_set_style_bg_color(m, lv_color_hex(0x2f2a26), 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(m, C_AMBAR, 0);
    lv_obj_set_style_border_width(m, 2, 0);
    lv_obj_set_style_radius(m, 16, 0);
    lv_obj_set_style_pad_hor(m, 20, 0);
    lv_obj_set_style_pad_ver(m, 18, 0);
    lv_obj_set_flex_flow(m, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(m, 2, 0);
    texto(m, "Missão do dia", f_px_m, C_AMBAR);
    texto(m, "Fez isso aqui, o dia tá ganho.", f_corpo_p, lv_color_hex(0xcdb79a));
    lista_missao = caixa(m);
    lv_obj_set_width(lista_missao, LV_PCT(100));
    lv_obj_set_flex_flow(lista_missao, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(lista_missao, 8, 0);

    btn_cortar = lv_button_create(m);
    lv_obj_set_style_bg_opa(btn_cortar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_cortar, 0, 0);
    lv_obj_set_style_border_color(btn_cortar, lv_color_hex(0x8a7556), 0);
    lv_obj_set_style_border_width(btn_cortar, 1, 0);
    lv_obj_set_style_radius(btn_cortar, 10, 0);
    texto(btn_cortar, "Cortar a missão pra uma coisa só", f_corpo_p, lv_color_hex(0xe4cfae));
    lv_obj_add_event_cb(btn_cortar, ev_cortar, LV_EVENT_CLICKED, NULL);

    caixa_bonus = caixa(col);
    lv_obj_set_width(caixa_bonus, LV_PCT(100));
    lv_obj_set_flex_flow(caixa_bonus, LV_FLEX_FLOW_COLUMN);
    texto(caixa_bonus, "Bônus, se der", f_px_p, C_FRACO);
    lista_bonus = caixa(caixa_bonus);
    lv_obj_set_width(lista_bonus, LV_PCT(100));
    lv_obj_set_flex_flow(lista_bonus, LV_FLEX_FLOW_COLUMN);

    tar_balao = balao(t, 40, 64, 300);
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
    const char *fala;
    if (f == n) fala = "Missão do dia batida! O resto é enfeite.";
    else if (dificil) fala = cortada ? "Uma coisa só hoje. Já é o dia inteiro." : "Hoje é só isso aqui. Sem pressa.";
    else if (f == 0) fala = n == 1 ? "Uma coisinha e o dia tá ganho. O resto é bônus." : "Duas coisinhas e o dia tá ganho. O resto é bônus.";
    else fala = "Metade da missão já foi. Tô vendo, viu?";
    balao_texto(tar_balao, fala);
}

// ---------------------------------------------------------------- CASA
static struct { float x, alvo; anim_t fazendo; int64_t ate; } casa = { 360, 360, A_LENDO, 0 };
static const anim_t ATIVIDADES[] = { A_LENDO, A_CAFE, A_BOLHAS, A_BRINCANDO, A_BOCEJANDO, A_DORMINDO, A_AGUA, A_NINTENDO };
#define CASA_Y 250

static lv_obj_t *retangulo(lv_obj_t *pai, int x, int y, int w, int h, uint32_t cor)
{
    lv_obj_t *o = caixa(pai);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(cor), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void cria_casa(lv_obj_t *t)
{
    lv_obj_t *parede = retangulo(t, 0, 0, TELA_W, 450, 0x262235);
    lv_obj_set_style_bg_grad_color(parede, lv_color_hex(0x2c2740), 0);
    lv_obj_set_style_bg_grad_dir(parede, LV_GRAD_DIR_VER, 0);
    for (int i = 0; i < 8; i++) retangulo(t, i * 128, 450, 128, 150, (i % 2) ? 0x352b28 : 0x3a2f2c);
    retangulo(t, 0, 450, TELA_W, 6, 0x4a3c36);

    lv_obj_t *jan = retangulo(t, 110, 90, 190, 150, 0x4b4460);
    lv_obj_t *ceu = retangulo(jan, 10, 10, 170, 130, 0x46598a);
    lv_obj_set_style_bg_grad_color(ceu, lv_color_hex(0x5a6f9a), 0);
    lv_obj_set_style_bg_grad_dir(ceu, LV_GRAD_DIR_VER, 0);
    retangulo(jan, 92, 10, 6, 130, 0x4b4460);
    retangulo(jan, 10, 72, 170, 6, 0x4b4460);

    lv_obj_t *quadro = retangulo(t, 734, 110, 120, 90, 0x5f6a2a);
    lv_obj_set_style_border_color(quadro, lv_color_hex(0x6d5a45), 0);
    lv_obj_set_style_border_width(quadro, 8, 0);

    lv_obj_t *tapete = retangulo(t, 330, 520, 380, 44, 0x6b4a6e);
    lv_obj_set_style_radius(tapete, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(tapete, lv_color_hex(0x7d5a80), 0);
    lv_obj_set_style_border_width(tapete, 8, 0);

    lv_obj_t *vaso = retangulo(t, 898, 400, 56, 70, 0x7a4b35);
    lv_obj_set_style_radius(vaso, 10, 0);
    const int folhas[3][3] = { { 880, 330, 0x5f7a36 }, { 916, 318, 0x6f8c3e }, { 896, 296, 0x7c9a44 } };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *f = retangulo(t, folhas[i][0], folhas[i][1], 52, 52, folhas[i][2]);
        lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
    }
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
        if (casa.ate && esp_random() % 10 < 6) {
            casa.alvo = 20 + esp_random() % 620;
            casa.fazendo = ATIVIDADES[esp_random() % (sizeof ATIVIDADES / sizeof ATIVIDADES[0])];
        }
        casa.ate = agora_ms() + 5000 + esp_random() % 4000;
        lylu_mostra(casa.fazendo);
    } else {
        lylu_mostra(casa.fazendo);
    }
}

// ---------------------------------------------------------------- RELÓGIO
static void cria_relogio(lv_obj_t *t)
{
    lv_obj_t *col = caixa(t);
    lv_obj_set_pos(col, 60, 100);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_t *linha = caixa(col);
    lv_obj_set_flex_flow(linha, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(linha, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(linha, 12, 0);
    rel_hora = texto(linha, "--:--", f_px_relogio, C_TINTA);
    rel_seg = texto(linha, "00", f_px_g, C_FRACO);
    lv_obj_set_style_pad_bottom(rel_seg, 34, 0);
    rel_data = texto(col, "", f_corpo_g, C_FRACO);
    lv_obj_t *prox = texto(col, "15:00  ·  Reunião com a gráfica", f_corpo, C_AMBAR);
    lv_obj_set_style_pad_top(prox, 12, 0);
}

static const char *DIAS[] = { "domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado" };
static const char *MESES[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho",
                               "agosto", "setembro", "outubro", "novembro", "dezembro" };

static void atualiza_hora(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char s[64];
    snprintf(s, sizeof s, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(st_hora, s);
    lv_label_set_text(rel_hora, s);
    snprintf(s, sizeof s, "%02d", tm.tm_sec);
    lv_label_set_text(rel_seg, s);
    snprintf(s, sizeof s, "%s, %d de %s", DIAS[tm.tm_wday], tm.tm_mday, MESES[tm.tm_mon]);
    lv_label_set_text(rel_data, s);
}

// ---------------------------------------------------------------- SEMANA
static void cria_semana(lv_obj_t *t)
{
    lv_obj_t *col = caixa(t);
    lv_obj_set_pos(col, 40, 64);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 6, 0);
    texto(col, "4 missões batidas", f_px_m, C_TINTA);
    texto(col, "A semana conta dias ganhos, não tarefas que sobraram.", f_corpo, C_FRACO);

    static const struct { const char *nome; char tipo; } SEM[] = {
        { "seg", 'c' }, { "ter", 'm' }, { "qua", 'c' }, { "qui", 's' }, { "sex", 'm' }, { "sáb", 'h' }, { "dom", 'f' } };
    lv_obj_t *dias = caixa(t);
    lv_obj_set_pos(dias, 40, 190);
    lv_obj_set_flex_flow(dias, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dias, 12, 0);
    for (int i = 0; i < 7; i++) {
        lv_obj_t *d = caixa(dias);
        lv_obj_set_size(d, 76, 116);
        lv_obj_set_style_bg_color(d, C_CARTAO, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(d, 14, 0);
        lv_obj_set_flex_flow(d, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(d, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(d, 12, 0);
        if (SEM[i].tipo == 'h') { lv_obj_set_style_outline_color(d, C_TINTA, 0); lv_obj_set_style_outline_width(d, 2, 0); }
        texto(d, SEM[i].nome, f_px_p, C_FRACO);
        lv_obj_t *mk = caixa(d);
        lv_obj_set_size(mk, 46, 46);
        lv_obj_set_style_radius(mk, LV_RADIUS_CIRCLE, 0);
        const char *simb = "";
        switch (SEM[i].tipo) {
        case 'c': lv_obj_set_style_bg_color(mk, C_OLIVA, 0); lv_obj_set_style_bg_opa(mk, LV_OPA_COVER, 0); simb = "*"; break;
        case 'm': lv_obj_set_style_bg_color(mk, C_AMBAR, 0); lv_obj_set_style_bg_opa(mk, LV_OPA_COVER, 0); simb = LV_SYMBOL_OK; break;
        case 's': lv_obj_set_style_border_color(mk, lv_color_hex(0x4f6d8f), 0); lv_obj_set_style_border_width(mk, 3, 0); simb = "~"; break;
        default:  lv_obj_set_style_border_color(mk, C_LINHA, 0); lv_obj_set_style_border_width(mk, 2, 0); break;
        }
        lv_obj_t *sl = texto(mk, simb, SEM[i].tipo == 'm' ? &lv_font_montserrat_14 : f_px_p, SEM[i].tipo == 's' ? C_CEU : C_ESCURO);
        lv_obj_center(sl);
    }

    lv_obj_t *leg = caixa(t);
    lv_obj_set_pos(leg, 40, 330);
    lv_obj_set_width(leg, 600);
    lv_obj_set_flex_flow(leg, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(leg, 22, 0);
    lv_obj_set_style_pad_row(leg, 8, 0);
    texto(leg, "* dia cheio", f_corpo_p, C_OLIVA);
    texto(leg, LV_SYMBOL_OK, &lv_font_montserrat_14, C_AMBAR);
    lv_obj_t *lm = texto(leg, "missão cumprida", f_corpo_p, C_AMBAR);
    lv_obj_set_style_margin_left(lm, -16, 0);
    texto(leg, "~ dia de sobrevivência (não conta contra)", f_corpo_p, C_CEU);

    texto(t, "dados de exemplo", f_corpo_p, lv_color_hex(0x6f6988));
    lv_obj_set_pos(lv_obj_get_child(t, -1), 40, 530);

    lv_obj_t *b = balao(t, 690, 74, 260);
    balao_texto(b, "Quinta foi pesada e tá tudo bem. O resto da semana foi seu.");
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
static lv_obj_t *aj_wifi_sub;
static bool wf_aberta, wf_modo_senha;
static char wf_alvo[REDE_NOME_MAX];
static rede_achada_t wf_achadas[REDE_MAX_ACHADAS];
static int wf_n;

static void atualiza_wifi(void);

// ---- o teclado ----
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

static void teclas_mapa(const char **mapa)
{
    lv_buttonmatrix_set_map(wf_teclado, mapa);
    lv_buttonmatrix_set_ctrl_map(wf_teclado, TECLAS_LARGURA);   // o set_map zera as larguras
}

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
    teclas_mapa(TECLAS_MIN);
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

static void ev_tecla(lv_event_t *e)
{
    uint32_t i = lv_buttonmatrix_get_selected_button(wf_teclado);
    const char *t = lv_buttonmatrix_get_button_text(wf_teclado, i);
    if (!t) return;
    if      (!strcmp(t, "ABC"))      teclas_mapa(TECLAS_MAI);
    else if (!strcmp(t, "abc"))      teclas_mapa(TECLAS_MIN);
    else if (!strcmp(t, "#+="))      teclas_mapa(TECLAS_SIMB);
    else if (!strcmp(t, "Apagar"))   lv_textarea_delete_char(wf_campo);
    else if (!strcmp(t, "Cancelar")) ev_wifi_fecha(NULL);
    else if (!strcmp(t, "Conectar")) {
        rede_conectar(wf_alvo, lv_textarea_get_text(wf_campo));
        // O teclado sai da frente na hora. Entrar na rede leva alguns segundos, e
        // um balãozinho atrás do teclado é aviso fraco demais: some o teclado, e a
        // resposta passa a ser a linha de cima, grande, junto com a cara dela.
        wf_modo_senha = false;
        atualizar();
    }
    else lv_textarea_add_text(wf_campo, t);
}

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

    wf_teclado = lv_buttonmatrix_create(wf_tela);
    lv_obj_set_size(wf_teclado, TELA_W, 268);
    lv_obj_set_pos(wf_teclado, 0, 288);
    lv_buttonmatrix_set_one_checked(wf_teclado, false);
    lv_obj_set_style_bg_opa(wf_teclado, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wf_teclado, 0, 0);
    lv_obj_set_style_pad_all(wf_teclado, 8, 0);
    lv_obj_set_style_pad_row(wf_teclado, 8, 0);
    lv_obj_set_style_pad_column(wf_teclado, 8, 0);
    lv_obj_set_style_bg_color(wf_teclado, C_CARTAO, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(wf_teclado, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(wf_teclado, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(wf_teclado, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(wf_teclado, C_TINTA, LV_PART_ITEMS);
    lv_obj_set_style_text_font(wf_teclado, f_corpo, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(wf_teclado, C_LINHA, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(wf_teclado, C_OLIVA, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(wf_teclado, C_ESCURO, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(wf_teclado, ev_tecla, LV_EVENT_VALUE_CHANGED, NULL);
    teclas_mapa(TECLAS_MIN);
}

// A rede avisa da thread de eventos do ESP-IDF, não da thread da tela.
static void ev_rede(void)
{
    if (!placa_trava()) return;
    lv_async_call(atualizar_depois, NULL);   // fora do evento: a tela se refaz inteira
    placa_destrava();
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
static lv_obj_t *linha_ajuste(lv_obj_t *pai, const char *nome, const char *sub)
{
    lv_obj_t *l = caixa(pai);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_bg_color(l, C_CARTAO, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, 14, 0);
    lv_obj_set_style_pad_hor(l, 20, 0);
    lv_obj_set_style_pad_ver(l, 16, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *txt = caixa(l);
    lv_obj_set_flex_grow(txt, 1);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    texto(txt, nome, f_corpo, C_TINTA);
    if (sub) texto(txt, sub, f_corpo_p, C_FRACO);
    return l;
}

static lv_obj_t *chave(lv_obj_t *pai, bool ligada)
{
    lv_obj_t *sw = lv_switch_create(pai);
    lv_obj_set_size(sw, 62, 34);
    lv_obj_set_style_bg_color(sw, C_LINHA, 0);
    lv_obj_set_style_bg_color(sw, C_CEU, LV_PART_INDICATOR | LV_STATE_CHECKED);
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
    placa_brilho(lv_slider_get_value(lv_event_get_target(e)));
}

static void cria_ajustes(lv_obj_t *t)
{
    lv_obj_t *col = caixa(t);
    lv_obj_set_pos(col, 40, 64);
    lv_obj_set_width(col, 600);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 12, 0);

    lv_obj_t *lw = linha_ajuste(col, "Wi-Fi", "");
    aj_wifi_sub = lv_obj_get_child(lv_obj_get_child(lw, 0), 1);
    lv_obj_set_style_margin_right(sinal_cria(lw), 18, 0);
    texto(lw, ">", f_px_p, C_FRACO);
    lv_obj_add_flag(lw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lw, ev_wifi_abre, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lm = linha_ajuste(col, "Microfone", "");
    aj_mic_sub = lv_obj_get_child(lv_obj_get_child(lm, 0), 1);
    texto(lm, ">", f_px_p, C_FRACO);
    lv_obj_add_flag(lm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lm, ev_mic_abre, LV_EVENT_CLICKED, NULL);

    aj_dificil = chave(linha_ajuste(col, "Dia difícil", "Só a missão aparece, sem cutucadas"), false);
    lv_obj_add_event_cb(aj_dificil, ev_dificil, LV_EVENT_VALUE_CHANGED, NULL);
    chave(linha_ajuste(col, "Silêncio no foco", "Nenhum som enquanto o timer roda"), true);
    lv_obj_t *s = lv_slider_create(linha_ajuste(col, "Brilho", NULL));
    lv_obj_set_width(s, 200);
    lv_slider_set_range(s, 10, 100);
    lv_slider_set_value(s, 95, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, C_OLIVA, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, C_TINTA, LV_PART_KNOB);
    lv_obj_add_event_cb(s, ev_brilho, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *b = balao(t, 700, 80, 240);
    balao_texto(b, "Mexe à vontade. Eu tô só olhando.");
}

// ---------------------------------------------------------------- humor e atualização geral
static const char *humor(lv_color_t *cor)
{
    if (dificil) { *cor = C_CEU; return "cuidadora"; }
    if (missao_completa()) { *cor = C_AMBAR; return "comemorando"; }
    if (tela_atual == T_CASA || tela_atual == T_RELOGIO) { *cor = C_LILAS; return "brincalhona"; }
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
    atualiza_foco();
    atualiza_linha_wifi();
    atualiza_linha_mic();

    if (wf_aberta) { atualiza_wifi(); return; }
    if (mf_aberta) { atualiza_microfone(); return; }

    switch (tela_atual) {
    case T_FOCO:    lylu_na_tela(T_FOCO, 560, 140); lylu_mostra(anim_foco()); break;
    case T_TAREFAS: lylu_na_tela(T_TAREFAS, 20, 200);
                    lylu_mostra(missao_completa() ? A_COMEMORANDO : dificil ? A_CUIDADORA : A_APONTANDO); break;
    case T_CASA:    lylu_na_tela(T_CASA, (int)casa.x, CASA_Y); lylu_mostra(casa.fazendo); break;
    case T_RELOGIO: lylu_na_tela(T_RELOGIO, 640, 170);
                    lylu_mostra((agora_ms() / 12000) % 2 ? A_NINTENDO : A_BRINCANDO); break;
    case T_SEMANA:  lylu_na_tela(T_SEMANA, 660, 200); lylu_mostra(A_COMEMORANDO); break;
    case T_AJUSTES: lylu_na_tela(T_AJUSTES, 660, 200); lylu_mostra(A_PENSANDO); break;
    }
}

static void ev_troca_tela(lv_event_t *e)
{
    lv_obj_t *ativa = lv_tileview_get_tile_active(tv);
    for (int i = 0; i < N_TELAS; i++) if (tiles[i] == ativa) tela_atual = i;
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
    if (foco.rodando && --foco.resto <= 0) {
        foco.rodando = false; foco.acabou = true; foco.resto = 0; foco.blocos++;
        atualizar();
    } else if (tela_atual == T_FOCO) {
        atualiza_foco();
    }
    if (tela_atual == T_RELOGIO && carinho_ate < agora_ms())
        lylu_mostra((agora_ms() / 12000) % 2 ? A_NINTENDO : A_BRINCANDO);
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
    cria_foco(tiles[T_FOCO]);
    cria_tarefas(tiles[T_TAREFAS]);
    cria_casa(tiles[T_CASA]);
    cria_relogio(tiles[T_RELOGIO]);
    cria_semana(tiles[T_SEMANA]);
    cria_ajustes(tiles[T_AJUSTES]);
    cria_wifi();
    cria_microfone();

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
    placa_destrava();

    audio_iniciar();         // ES8311: se não responder, ela segue surda e nada quebra
    rede_iniciar(ev_rede);   // demora uns segundos e pode não achar o C6; a tela já está de pé

    ESP_LOGI(TAG, "Lylu pronta");
}
