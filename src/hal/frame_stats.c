#include "frame_stats.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    bool active, render_pending;
    uint64_t frequency, begin, render_start, flush_start, last;
    uint64_t render_ticks, flush_ticks, invalid_pixels;
    double intervals[1024];
    unsigned frames, count;
} stats;

static double milliseconds(uint64_t ticks)
{
    return 1000.0 * (double)ticks / (double)stats.frequency;
}

static void display_event(lv_event_t *event)
{
    if(!stats.active) return;
    uint64_t now = SDL_GetPerformanceCounter();
    switch(lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START:
            stats.render_start = now;
            stats.render_pending = true;
            break;
        case LV_EVENT_RENDER_READY:
            /* Ignore READY without a matching START so a stale timestamp cannot skew the scope. */
            if(stats.render_pending) stats.render_ticks += now - stats.render_start;
            stats.render_pending = false;
            if(stats.last && stats.count < 1024)
                stats.intervals[stats.count++] = milliseconds(now - stats.last);
            stats.last = now;
            ++stats.frames;
            break;
        case LV_EVENT_FLUSH_START: stats.flush_start = now; break;
        case LV_EVENT_FLUSH_FINISH: stats.flush_ticks += now - stats.flush_start; break;
        case LV_EVENT_INVALIDATE_AREA: {
            const lv_area_t *area = lv_event_get_param(event);
            if(area && area->x2 >= area->x1 && area->y2 >= area->y1)
                stats.invalid_pixels += (uint64_t)(area->x2 - area->x1 + 1) *
                                        (uint64_t)(area->y2 - area->y1 + 1);
            break;
        }
        default: break;
    }
}

static int compare(const void *a, const void *b)
{
    double left = *(const double *)a, right = *(const double *)b;
    return (left > right) - (left < right);
}

void frame_stats_begin(lv_display_t *display)
{
    memset(&stats, 0, sizeof(stats));
    stats.frequency = SDL_GetPerformanceFrequency();
    stats.begin = SDL_GetPerformanceCounter();
    stats.active = stats.frequency != 0;
    lv_display_add_event_cb(display, display_event, LV_EVENT_ALL, NULL);
}

void frame_stats_report(void)
{
    if(!stats.active) return;
    stats.active = false;
    qsort(stats.intervals, stats.count, sizeof(stats.intervals[0]), compare);
    double p95 = stats.count ? stats.intervals[(stats.count * 95 + 99) / 100 - 1] : 0;
    double maximum = stats.count ? stats.intervals[stats.count - 1] : 0;
    printf("Boot entry: %.0f ms, %u renders, %u sampled intervals, P95 %.2f ms, max %.2f ms\n",
           milliseconds(SDL_GetPerformanceCounter() - stats.begin), stats.frames, stats.count, p95, maximum);
    printf("Render scope %.2f ms (includes flush); flush %.2f ms (includes SDL present/VSYNC). "
           "Pre-merge invalidations: %llu px (overlaps counted).\n",
           milliseconds(stats.render_ticks), milliseconds(stats.flush_ticks),
           (unsigned long long)stats.invalid_pixels);
    puts("Entry intervals include intentional holds and H's ~20.5 ms decode cadence "
         "(66 ms source steps at the 1000/3220 playback); "
         "these are diagnostics, not continuous-motion or physical-panel FPS acceptance.");
}
