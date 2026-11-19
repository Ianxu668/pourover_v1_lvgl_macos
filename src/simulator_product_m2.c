#include <stdio.h>
#include <string.h>
#include "simulator_product_internal.h"

bool m2_autostart = true;
bool m2_show_remain;
static int m2_tare_step;

static po_m2_ready_view_t m2_ready_mirror;
static bool m2_ready_mirror_valid;
static bool m2_reload_recipes;
static po_m2_brew_view_t m2_brew_mirror;
static bool m2_brew_mirror_valid;
static po_m2_result_view_t m2_result_mirror;
static bool m2_result_mirror_valid;
static po_m2_segment_t m2_plan[PO_M2_SEGMENT_MAX];
static unsigned m2_plan_count;

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

/* Reference profiles for the simulator's three demo cups. */
static const po_m2_segment_t m2_cup_plans[][4] = {
    {{.target_grams = 60, .seconds = 45}, {.target_grams = 68, .seconds = 45},
     {.target_grams = 72, .seconds = 45}, {.target_grams = 88, .seconds = 50}},
    {{.target_grams = 65, .seconds = 50}, {.target_grams = 70, .seconds = 45},
     {.target_grams = 73, .seconds = 45}, {.target_grams = 78, .seconds = 52}},
    {{.target_grams = 48, .seconds = 43}, {.target_grams = 60, .seconds = 44},
     {.target_grams = 62, .seconds = 45}, {.target_grams = 70, .seconds = 53}},
};

static void fill_m2_ready(po_m2_ready_view_t *view)
{
    *view = m2_ready_mirror;
    view->autostart = m2_autostart;
    view->show_remain = m2_show_remain;
    view->net_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    /* The demo autotare window belongs to recipe/history brewing only. */
    if(view->source == PO_M2_SOURCE_FREE) {
        view->autotare_decigrams = -1;
        view->container_id = 0;
    }
}

static void fill_m2_brew(po_m2_brew_view_t *view)
{
    *view = m2_brew_mirror;
    if(m2_ready_mirror_valid) {
        view->source = m2_ready_mirror.source;
        if(view->source != PO_M2_SOURCE_FREE)
            view->target_grams = m2_ready_mirror.target_grams;
        view->segment_count = m2_plan_count;
        memcpy(view->segments, m2_plan, m2_plan_count * sizeof m2_plan[0]);
    }
    view->battery_percent = ready_view.battery_percent;
    view->charging = ready_view.charging;
    view->connected = ready_view.connected;
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

/* Result statistics use gram hundredths; display masses may be ounce hundredths. */
static int64_t m2_centi_grams(int64_t value, bool ounces)
{
    return ounces ? value * 28349523125LL / 1000000000LL : value * 10;
}

static void fill_m2_result(po_m2_result_view_t *view)
{
    *view = m2_result_mirror;
    if(m2_ready_mirror_valid) {
        view->source = m2_ready_mirror.source;
        if(view->source != PO_M2_SOURCE_FREE)
            view->target_grams = m2_ready_mirror.target_grams;
        view->segment_count = m2_plan_count;
        memcpy(view->segments, m2_plan, m2_plan_count * sizeof m2_plan[0]);
        memcpy(view->recipe_name, m2_ready_mirror.recipe_name, sizeof view->recipe_name);
    }
    view->elapsed_seconds = (int)product_session.elapsed_seconds;
    view->water_decigrams = product_session.net_decigrams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = host_brew_count;
    view->samples = host_brew_count ? host_brew : NULL;
    /* Host-derived summary and data-sheet snapshots; 0 keeps a field hidden.
       One total: the curve caption and the summary read the same number. */
    view->dose_origin = m2_ready_mirror.dose_origin;
    view->container_id = m2_ready_mirror.container_id;
    view->total_input_decigrams = view->water_decigrams +
                                  view->bypass_pre_decigrams + view->bypass_post_decigrams;
    view->total_decigrams = view->total_input_decigrams;
    view->total_ratio_tenths = view->dose_decigrams > 0 && view->total_input_decigrams > 0
                                   ? (int)(m2_centi_grams(view->total_input_decigrams, view->ounces) /
                                           view->dose_decigrams) : 0;
    view->target_time_seconds = -1;
    if(host_brew_count > 0) {
        int64_t sum = 0;
        int peak = 0;
        int64_t target = view->target_grams > 0 ? (int64_t)view->target_grams * 100 : 0;
        for(unsigned i = 0; i < host_brew_count; ++i) {
            if(host_brew[i].flow_decigrams > peak) peak = host_brew[i].flow_decigrams;
            sum += host_brew[i].flow_decigrams;
            if(target > 0 && view->target_time_seconds < 0 &&
               m2_centi_grams(host_brew[i].weight_decigrams, view->ounces) >= target)
                view->target_time_seconds = (int)i;
        }
        view->avg_flow_centi = (int)(m2_centi_grams(sum, view->ounces) / host_brew_count);
        view->peak_flow_centi = (int)m2_centi_grams(peak, view->ounces);
    }
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
        product_view_dirty = true;
        break;
    case PO_M2_READY_REMAIN:
        m2_show_remain = event->value != 0;
        product_view_dirty = true;
        break;
    case PO_M2_READY_ACT_DOSE:
        m2_ready_mirror.dose_decigrams = event->value;
        m2_ready_mirror.dose_origin = PO_M2_DOSE_MANUAL;
        m2_ready_mirror.detect_popup = false;
        product_view_dirty = true;
        break;
    case PO_M2_READY_ACT_RATIO:
        m2_ready_mirror.ratio_tenths = event->value;
        product_view_dirty = true;
        break;
    case PO_M2_READY_ACT_BYPASS:
        m2_ready_mirror.bypass_decigrams = event->value;
        product_view_dirty = true;
        break;
    case PO_M2_READY_SOURCE:
        m2_ready_mirror.source = (po_m2_source_t)event->value;
        m2_ready_mirror.recipe_name[0] = '\0';
        m2_ready_mirror.recipe_segments = 0;
        m2_ready_mirror.preview_seconds = 0;
        m2_ready_mirror.target_grams = po_ui_m2_ready_baseline.target_grams;
        m2_plan_count = 0;
        product_view_dirty = true;
        break;
    case PO_M2_READY_RECIPE_RETRY:
        m2_reload_recipes = true;
        product_view_dirty = true;
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
            m2_plan_count = (unsigned)recipe->segments;
            memcpy(m2_plan, recipe->plan, m2_plan_count * sizeof m2_plan[0]);
            product_view_dirty = true;
        }
        break;
    case PO_M2_READY_CUP:
        if(event->value < 0 || (unsigned)event->value >= sizeof m2_cups / sizeof m2_cups[0])
            break;
        {
            const po_m2_cup_t *cup = &m2_cups[event->value];
            m2_plan_count = sizeof m2_cup_plans[0] / sizeof m2_cup_plans[0][0];
            memcpy(m2_plan, m2_cup_plans[event->value], sizeof m2_cup_plans[0]);
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
        /* Demo hook for the design autotare window: one whole snapshot per
           press — matched names its container, unmatched has none, third
           press clears the window. */
        if(m2_ready_mirror.source != PO_M2_SOURCE_FREE) {
            if(m2_tare_step == 0) {
                m2_ready_mirror.autotare_decigrams = 2004;
                m2_ready_mirror.container_id = 1;
            }
            else if(m2_tare_step == 1) {
                m2_ready_mirror.autotare_decigrams = 140;
                m2_ready_mirror.container_id = 0;
            }
            else {
                m2_ready_mirror.autotare_decigrams = -1;
                m2_ready_mirror.container_id = 0;
            }
            m2_tare_step = (m2_tare_step + 1) % 3;
        }
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
        product_view_dirty = true;
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
        m2_result_mirror.surface = PO_M2_RESULT_MAIN;
        product_view_dirty = true;
        break;
    case PO_M2_RESULT_BYPASS_CANCEL:
        m2_result_mirror.bypass_live_decigrams = 0;
        product_view_dirty = true;
        break;
    default:
        break;
    }
}

bool m2_ready_present(void)
{
    po_m2_ready_view_t view;
    /* Returning to Ready ends the previous brew's confirmed post-bypass. */
    m2_result_mirror.bypass_post_decigrams = 0;
    if(!m2_ready_mirror_valid) {
        m2_ready_mirror = po_ui_m2_ready_baseline;
        m2_ready_mirror_valid = true;
    }
    fill_m2_ready(&view);
    if(!po_ui_show_m2_ready(&view)) return false;
    if(!po_ui_m2_ready_set_recipes(m2_recipes, sizeof m2_recipes / sizeof m2_recipes[0]))
        return false;
    m2_reload_recipes = false;
    if(!po_ui_m2_ready_set_cups(m2_cups, sizeof m2_cups / sizeof m2_cups[0])) return false;
    po_ui_m2_ready_set_handlers(on_m2_ready_event, NULL);
    return true;
}

bool m2_ready_refresh(void)
{
    po_m2_ready_view_t view;
    if(m2_reload_recipes) {
        if(!po_ui_m2_ready_set_recipes(m2_recipes, sizeof m2_recipes / sizeof m2_recipes[0])) {
            product_view_dirty = true;
            return false;
        }
        m2_reload_recipes = false;
    }
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
    /* Snapshot once on entry. fill_m2_result must not recopy the ready bypass. */
    m2_result_mirror.bypass_pre_decigrams = m2_ready_mirror.bypass_decigrams;
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
    m2_tare_step = 0;
    m2_plan_count = 0;
    m2_reload_recipes = false;
    memset(&m2_ready_mirror, 0, sizeof m2_ready_mirror);
    m2_ready_mirror_valid = false;
    memset(&m2_brew_mirror, 0, sizeof m2_brew_mirror);
    m2_brew_mirror_valid = false;
    memset(&m2_result_mirror, 0, sizeof m2_result_mirror);
    m2_result_mirror_valid = false;
}
