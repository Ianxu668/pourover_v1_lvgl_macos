/* Software-only portability tests, not a firmware lv_conf.h.
 * Deliberately minimal: only settings the simulator/core builds must pin are listed here.
 * Every macro omitted below intentionally falls back to this fixed LVGL fork's own default;
 * do not treat this file as a firmware configuration template or fill in defaults by hand. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (4 * 1024 * 1024)
#define LV_DEF_REFR_PERIOD 16
#define LV_USE_FLOAT 0
#define LV_USE_LOG 0
#define LV_USE_OBJ_NAME 1
#define LV_USE_SNAPSHOT 1
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB888 1
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4
#define LV_USE_IMAGE 1
#define LV_USE_BUTTON 1
#define LV_USE_LINE 1
#define LV_USE_SDL 0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_THORVG 0
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_LOTTIE 0
#define LV_USE_SVG 0
#define LV_USE_GIF 0
#define LV_USE_FFMPEG 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_TRANSLATION 1
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 0
#ifdef PO_UI_RESOURCE_PROTOTYPE
#define LV_USE_FS_MEMFS 1
#define LV_FS_MEMFS_LETTER 'M'
#else
#define LV_USE_FS_MEMFS 0
#define LV_FS_MEMFS_LETTER 0
#endif
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#endif
