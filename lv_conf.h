#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_UNSCII_16 1
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

/*====================
   MEMORY SETTINGS
 *====================*/

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (128U * 1024U)     // Increased to 128KB for better performance
#define LV_MEM_ADR 0
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC malloc
#define LV_MEM_POOL_FREE free

/*====================
   HAL SETTINGS
 *====================*/

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_DPI_DEF 130

/*====================
 * FEATURE CONFIGURATION
 *====================*/

#define LV_USE_ANIMATION 1
#define LV_USE_SHADOW 0              // Changed to 0 to save resources
#define LV_SHADOW_CACHE_SIZE 0
#define LV_USE_BLEND_MODES 0
#define LV_USE_OPA_SCALE 0
#define LV_USE_IMG_TRANSFORM 0
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_IMG_CF_INDEXED 0
#define LV_IMG_CF_ALPHA 0
#define LV_USE_FILESYSTEM 0
#define LV_USE_GPU 0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_GPU_DMA2D_CMSIS_INCLUDE
#define LV_USE_PRINTF 1

/*-------------
 * Drawing
 *-----------*/

/*Enable complex draw engine*/
#define LV_DRAW_COMPLEX 1

/*====================
 * FONT USAGE
 *====================*/

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*===================
 *  LV_OBJ SETTINGS
 *==================*/

#define LV_USE_OBJ_REALIGN 1

/*==================
 *  WIDGET USAGE
 *================*/

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1    // Need this for brightness control
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       0    // Changed to 0, not using lines
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1    // Need this for volume control
#define LV_USE_SWITCH     1    // Need this for sound enable/disable
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      0

/*==================
 * EXTRA COMPONENTS
 *================*/

#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0    // Changed to 0, not using charts currently
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   1
#define LV_USE_LED        0
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     1    // Need this for dialogs
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        0

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/