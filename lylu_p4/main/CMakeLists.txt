file(GLOB GIFS "${CMAKE_CURRENT_SOURCE_DIR}/gifs/*.gif")
file(GLOB FONTES "${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.ttf")
idf_component_register(
    SRCS "main.c" "placa.c" "rede.c" "audio.c"
    INCLUDE_DIRS "."
    EMBED_FILES ${GIFS} ${FONTES}
)
