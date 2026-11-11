#include <stdio.h>
#include <string.h>
#include "simulator_product_internal.h"

bool m2_autostart = true;
bool m2_show_remain;

static po_m2_ready_view_t m2_ready_mirror;
static bool m2_ready_mirror_valid;
static po_m2_brew_view_t m2_brew_mirror;
static bool m2_brew_mirror_valid;
static po_m2_result_view_t m2_result_mirror;
static bool m2_result_mirror_valid;

/* Simulator demo catalog: the pick page and history sheet frames are drawn
 * with these three recipes and cups. Segment plans mirror the design frames
 * (C04-segments and the result data table). */
static const po_m2_recipe_t m2_recipes[] = {
    {.name = "配方 A", .segments = 5, .total_grams = 288, .seconds = 185,
     .plan = {{60, 0, "闷蒸", 45}, {60, 0, "中心注水", 35}, {60, 0, "中心注水", 35},
              {60, 0, "中心注水", 35}, {48, 0, "绕圈注水", 35}}},
    {.name = "配方 B", .segments = 4, .total_grams = 251, .seconds = 154,
     .plan = {{60, 0, "闷蒸", 49}, {60, 0, "中心注水", 35}, {60, 0, "绕圈注水", 35},
              {71, 0, "中心注水", 35}}},
    {.name = "预置 · 浅烘", .segments = 4, .total_grams = 300, .seconds = 200, .preset = true,
     .plan = {{75, 0, "闷蒸", 50}, {75, 0, "中心注水", 50}, {75, 0, "绕圈注水", 50},
              {75, 0, "中心注水", 50}}},
};

static const po_m2_cup_t m2_cups[] = {
    {.year = 26, .month = 10, .day = 28, .cup_index = 2, .seconds = 185,
     .weight_decigrams = 2880, .dose_decigrams = 180},
    {.year = 26, .month = 10, .day = 27, .cup_index = 1, .seconds = 192,
     .weight_decigrams = 2860, .dose_decigrams = 180},
    {.year = 26, .month = 10, .day = 26, .cup_index = 4, .seconds = 185,
     .weight_decigrams = 2400, .dose_decigrams = 150},
};

static void fill_m2_ready(po_m2_ready_view_t *view)
{
    *view = m2_ready_mirror;
    view->autostart = m2_autostart;
    view->show_remain = m2_show_remain;
    view->net_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
}

static void fill_m2_brew(po_m2_brew_view_t *view)
{
    *view = m2_brew_mirror;
    view->show_remain = m2_show_remain && view->target_grams > 0 && !view->simple;
    int poured_grams = ready_view.unit == PO_READY_UNIT_OZ
                           ? (int)((int64_t)product_session.net_decigrams * 283495 / 1000000)
                           : product_session.net_decigrams / 10;
    view->remain_grams = view->target_grams - poured_grams;
    if(view->remain_grams < 0) view->remain_grams = 0;
    view->elapsed_seconds = product_session.elapsed_seconds;
    view->net_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = host_brew_count;
    view->samples = host_brew_count ? host_brew : NULL;
}

static void fill_m2_result(po_m2_result_view_t *view)
{
    *view = m2_result_mirror;
    view->elapsed_seconds = (int)product_session.elapsed_seconds;
    view->water_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = 0;
    view->samples = NULL;
}

static bool m2_ready_editor_open(const po_m2_ready_view_t *view)
{
    /* Full-screen editors keep their draft in the page; a set would reset the
       parent surface and close them, so dynamic updates wait. */
    return view->surface != PO_M2_READY_MAIN && view->surface != PO_M2_READY_PARAMS;
}

static void on_m2_ready_event(const po_m2_ready_event_t *event, void *context)
{
    (void)context;
    m2_ready_mirror.surface = event->surface;
    switch(event->action) {
    case PO_M2_READY_AUTOSTART:
        m2_autostart = event->value != 0;
        break;
    case PO_M2_READY_REMAIN:
        m2_show_remain = event->value != 0;
        break;
    case PO_M2_READY_ACT_DOSE:
        m2_ready_mirror.dose_decigrams = event->value;
        m2_ready_mirror.dose_origin = PO_M2_DOSE_MANUAL;
        m2_ready_mirror.detect_popup = false;
        break;
    case PO_M2_READY_ACT_RATIO:
        m2_ready_mirror.ratio_tenths = event->value;
        break;
    case PO_M2_READY_ACT_BYPASS:
        m2_ready_mirror.bypass_decigrams = event->value;
        break;
    case PO_M2_READY_SOURCE:
        m2_ready_mirror.source = (po_m2_source_t)event->value;
        break;
    case PO_M2_READY_RECIPE:
        if(event->value < 0 || (unsigned)event->value >= sizeof m2_recipes / sizeof m2_recipes[0])
            break;
        {
            const po_m2_recipe_t *recipe = &m2_recipes[event->value];
            m2_ready_mirror.surface = PO_M2_READY_MAIN;
            m2_ready_mirror.source = PO_M2_SOURCE_RECIPE;
            snprintf(m2_ready_mirror.recipe_name, sizeof m2_ready_mirror.recipe_name, "%s",
                     recipe->name);
            m2_ready_mirror.recipe_segments = recipe->segments;
            m2_ready_mirror.preview_seconds = recipe->seconds;
            m2_ready_mirror.target_grams = recipe->total_grams;
            product_view_dirty = true;
        }
        break;
    case PO_M2_READY_CUP:
        if(event->value < 0 || (unsigned)event->value >= sizeof m2_cups / sizeof m2_cups[0])
            break;
        {
            const po_m2_cup_t *cup = &m2_cups[event->value];
            m2_ready_mirror.surface = PO_M2_READY_MAIN;
            m2_ready_mirror.source = PO_M2_SOURCE_HISTORY;
            m2_ready_mirror.recipe_name[0] = '\0';
            m2_ready_mirror.recipe_segments = 0;
            m2_ready_mirror.preview_seconds = cup->seconds;
            m2_ready_mirror.dose_origin = PO_M2_DOSE_CARRIED;
            m2_ready_mirror.dose_decigrams = cup->dose_decigrams;
            m2_ready_mirror.ratio_tenths = cup->dose_decigrams > 0
                                               ? cup->weight_decigrams * 10 / cup->dose_decigrams
                                               : 0;
            m2_ready_mirror.target_grams = cup->weight_decigrams / 10;
            product_view_dirty = true;
        }
        break;
    case PO_M2_READY_TARE:
        product_session.net_decigrams = 0;
        product_view_dirty = true;
        break;
    default:
        break;
    }
}

static void on_m2_brew_event(const po_m2_brew_event_t *event, void *context)
{
    (void)context;
    m2_brew_mirror.surface = event->surface;
    switch(event->action) {
    case PO_M2_BREW_ACT_DOSE:
        m2_brew_mirror.dose_decigrams = event->value;
        break;
    case PO_M2_BREW_CONTINUE:
        m2_brew_mirror.surface = PO_M2_BREW_MAIN;
        m2_brew_mirror.duration_left = -1;
        product_view_dirty = true;
        break;
    case PO_M2_BREW_FINISH:
        product_inject(SIM_KEY_LEFT);
        break;
    default:
        break;
    }
}

static void on_m2_result_event(const po_m2_result_event_t *event, void *context)
{
    (void)context;
    m2_result_mirror.surface = event->surface;
    switch(event->action) {
    case PO_M2_RESULT_SAVE_RECIPE:
        /* Save as new recipe: close the menu and remain on the result. */
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        m2_result_mirror.save_open = false;
        product_view_dirty = true;
        break;
    case PO_M2_RESULT_SAVE_YES:
    case PO_M2_RESULT_SAVE_NO:
        /* Long-brew history choice: close the prompt and stay on the result. */
        m2_result_mirror.save_open = false;
        m2_result_mirror.save_seconds_left = -1;
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        product_view_dirty = true;
        break;
    case PO_M2_RESULT_CONTINUE:
        /* 返回继续 resumes the running brew. */
        m2_result_mirror.save_open = false;
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        product_pending_page = SIM_M2_BREW;
        product_pending_page_valid = true;
        break;
    case PO_M2_RESULT_BYPASS_COMMIT:
        m2_result_mirror.bypass_post_decigrams = event->value;
        m2_result_mirror.bypass_live_decigrams = 0;
        m2_result_mirror.total_decigrams = product_session.net_decigrams +
                                           m2_result_mirror.bypass_pre_decigrams +
                                           m2_result_mirror.bypass_post_decigrams;
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        product_view_dirty = true;
        break;
    case PO_M2_RESULT_BYPASS_CANCEL:
        m2_result_mirror.bypass_live_decigrams = 0;
        break;
    default:
        break;
    }
}

bool m2_ready_present(void)
{
    po_m2_ready_view_t view;
    if(!m2_ready_mirror_valid) {
        m2_ready_mirror = po_ui_m2_ready_baseline;
        m2_ready_mirror_valid = true;
    }
    fill_m2_ready(&view);
    if(!po_ui_show_m2_ready(&view)) return false;
    if(!po_ui_m2_ready_set_recipes(m2_recipes, sizeof m2_recipes / sizeof m2_recipes[0]))
        return false;
    if(!po_ui_m2_ready_set_cups(m2_cups, sizeof m2_cups / sizeof m2_cups[0])) return false;
    po_ui_m2_ready_set_handlers(on_m2_ready_event, NULL);
    return true;
}

bool m2_ready_refresh(void)
{
    po_m2_ready_view_t view;
    fill_m2_ready(&view);
    if(m2_ready_editor_open(&view)) return true;
    return po_ui_m2_ready_set(&view);
}

bool m2_brew_present(void)
{
    po_m2_brew_view_t view;
    host_brew_begin();
    if(!m2_brew_mirror_valid) {
        m2_brew_mirror = po_ui_m2_brew_baseline;
        m2_brew_mirror_valid = true;
    }
    /* Fresh page instance: transient surface/dialog state resets, dose and
       recipe preferences stay. */
    m2_brew_mirror.surface = PO_M2_BREW_MAIN;
    m2_brew_mirror.duration_left = -1;
    m2_brew_mirror.note = PO_M2_NOTE_NONE;
    m2_brew_mirror.editor = PO_M2_EDITOR_WHEEL;
    fill_m2_brew(&view);
    if(!po_ui_show_m2_brew(&view)) return false;
    po_ui_m2_brew_set_handlers(on_m2_brew_event, NULL);
    return true;
}

bool m2_brew_refresh(void)
{
    po_m2_brew_view_t view;
    fill_m2_brew(&view);
    return po_ui_m2_brew_set(&view);
}

bool m2_result_present(void)
{
    po_m2_result_view_t view;
    if(!m2_result_mirror_valid) {
        m2_result_mirror = po_ui_m2_result_baseline;
        m2_result_mirror_valid = true;
    }
    m2_result_mirror.surface = PO_M2_RESULT_MAIN;
    m2_result_mirror.save_open = false;
    m2_result_mirror.save_seconds_left = -1;
    m2_result_mirror.return_seconds_left = -1;
    m2_result_mirror.bypass_live_decigrams = 0;
    m2_result_mirror.editor = PO_M2_EDITOR_WHEEL;
    fill_m2_result(&view);
    if(!po_ui_show_m2_result(&view)) return false;
    po_ui_m2_result_set_handlers(on_m2_result_event, NULL);
    return true;
}

bool m2_result_refresh(void)
{
    po_m2_result_view_t view;
    fill_m2_result(&view);
    return po_ui_m2_result_set(&view);
}

void m2_start(const simulator_options_t *options)
{
    if(!m2_ready_mirror_valid) {
        m2_ready_mirror = po_ui_m2_ready_baseline;
        m2_ready_mirror_valid = true;
    }
    if(options->m2_surface_set) m2_ready_mirror.surface = options->m2_surface;
    if(options->m2_tab_set) m2_ready_mirror.pick_tab = options->m2_tab;
    if(options->m2_editor_set) m2_ready_mirror.editor = options->m2_editor;
}

void m2_reset(void)
{
    m2_autostart = true;
    m2_show_remain = false;
    memset(&m2_ready_mirror, 0, sizeof m2_ready_mirror);
    m2_ready_mirror_valid = false;
    memset(&m2_brew_mirror, 0, sizeof m2_brew_mirror);
    m2_brew_mirror_valid = false;
    memset(&m2_result_mirror, 0, sizeof m2_result_mirror);
    m2_result_mirror_valid = false;
}
