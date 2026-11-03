#include "simulator_start.h"
#include <stdbool.h>
#include <stdio.h>
#include <SDL.h>
#include "lvgl/lvgl.h"

static void start_clicked(lv_event_t *event)
{
    bool *started = lv_event_get_user_data(event);
    *started = true;
}

void simulator_wait_for_start(void)
{
    bool started = false;
    lv_obj_t *panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

    lv_obj_t *button = lv_button_create(panel);
    lv_obj_set_size(button, 200, 64);
    lv_obj_center(button);
    lv_obj_add_event_cb(button, start_clicked, LV_EVENT_CLICKED, &started);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "Start Boot");
    lv_obj_center(label);
    lv_obj_t *hint = lv_label_create(panel);
    lv_label_set_text(hint, "PC debug only - click to start");
    lv_obj_set_style_text_color(hint, lv_color_white(), 0);
    lv_obj_align_to(hint, button, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    puts("PC debug: waiting for Start Boot.");
    while(!started) {
        uint32_t sleep_time = lv_timer_handler();
        if(sleep_time == LV_NO_TIMER_READY) sleep_time = LV_DEF_REFR_PERIOD;
        if(!started) SDL_Delay(sleep_time);
    }
    lv_obj_delete(panel);
}

#ifdef SIMULATOR_START_TEST
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include "pourover_ui.h"

static unsigned checks;

static void check_start(lv_timer_t *timer)
{
    lv_obj_t *panel = lv_obj_get_child(lv_layer_top(), 0);
    assert(panel != NULL);
    lv_obj_t *button = lv_obj_get_child(panel, 0);
    assert(lv_obj_check_type(button, &lv_button_class));
    assert(strcmp(lv_label_get_text(lv_obj_get_child(button, 0)), "Start Boot") == 0);
    assert(po_ui_boot_get_state() == PO_BOOT_NONE);
    if(++checks < 3) lv_obj_send_event(button, LV_EVENT_PRESSED, NULL);
    else {
        lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
        lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
        lv_timer_delete(timer);
    }
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

int main(void)
{
    lv_init();
    lv_tick_set_cb(SDL_GetTicks);
    lv_display_t *display = lv_display_create(960, 376);
    static uint8_t pixels[960 * 16 * 4];
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_timer_create(check_start, 30, NULL);
    simulator_wait_for_start();
    assert(checks == 3);
    assert(lv_obj_get_child_count(lv_layer_top()) == 0);
    po_ui_show_boot();
    assert(po_ui_boot_get_state() == PO_BOOT_ENTERING);
    po_ui_destroy();
    lv_deinit();
    puts("simulator start: PASS (wait, click, repeated click, cleanup)");
    return 0;
}
#endif
