/**
 * @file lv_gif_private.h
 *
 */

#ifndef LYLU_LV_GIF_PRIVATE_H
#define LYLU_LV_GIF_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "src/widgets/image/lv_image_private.h"
#include "lv_gif.h"

#if 1 /* copia da Lylu: o GIF da LVGL fica desligado */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *      TYPEDEFS
 **********************/

struct lv_gif_t {
    lv_image_t img;
    gd_GIF * gif;
    lv_timer_t * timer;
    lv_image_dsc_t imgdsc;
    uint32_t last_call;
};


/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_GIF */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LYLU_LV_GIF_PRIVATE_H*/
