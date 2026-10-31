# Quatro ações da conversa (frente, escuta, pensamento e lembrete).
# As animações antigas por tela continuam fora.
file(GLOB GIFS "${CMAKE_CURRENT_SOURCE_DIR}/gifs/*.gif")
file(GLOB FONTES "${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.ttf")
# Os avisos do pomodoro na voz da Francisca (ADPCM, feitos pelo Hermes): tocam no zero, sem internet.
file(GLOB SONS "${CMAKE_CURRENT_SOURCE_DIR}/sons/*.adpcm")
idf_component_register(
    SRCS "main.c" "placa.c" "rede.c" "audio.c" "ponte.c" "adpcm.c" "gif/lv_gif.c" "gif/gifdec.c"
    INCLUDE_DIRS "."
    EMBED_FILES ${GIFS} ${FONTES} ${SONS}
)
