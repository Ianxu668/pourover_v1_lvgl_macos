#ifndef FRAME_STATS_H
#define FRAME_STATS_H
#include "lvgl.h"

/* Host-only, bounded entry-animation diagnostic; not a panel FPS measurement. */
void frame_stats_begin(lv_display_t *display);
void frame_stats_report(void);
#endif
