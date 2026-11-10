#include <string.h>
#include "simulator_product_internal.h"

brew_sample_t host_brew[HOST_BREW_CAP];
unsigned host_brew_count;
static int host_brew_origin;
static int host_brew_last;

void host_brew_begin(void)
{
    host_brew_count = 0;
    host_brew_origin = product_session.net_decigrams;
    host_brew_last = host_brew_origin;
}

void host_brew_sample(void)
{
    int weight = product_session.net_decigrams;
    int flow = weight - host_brew_last;
    if(flow < 0) flow = 0;
    if(host_brew_count == 0) {
        host_brew[0].weight_decigrams = host_brew_origin;
        host_brew[0].flow_decigrams = 0;
        host_brew_count = 1;
    }
    host_brew_last = weight;
    if(host_brew_count >= HOST_BREW_CAP) return;
    host_brew[host_brew_count].weight_decigrams = weight;
    host_brew[host_brew_count].flow_decigrams = flow;
    host_brew_count++;
}

void host_brew_follow(void)
{
    int weight = product_session.net_decigrams;
    int previous;
    int flow;
    if(host_brew_count == 0) return;
    previous = host_brew_count > 1 ? host_brew[host_brew_count - 2u].weight_decigrams : weight;
    flow = weight - previous;
    if(flow < 0) flow = 0;
    host_brew[host_brew_count - 1u].weight_decigrams = weight;
    host_brew[host_brew_count - 1u].flow_decigrams = flow;
    host_brew_last = weight;
}

static bool m1_brew_target_sheet;
static bool m1_result_save_open;

static void fill_m1_brew(brew_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->elapsed_seconds = product_session.elapsed_seconds;
    view->net_decigrams = product_session.net_decigrams;
    view->flow_decigrams = host_brew_count ? host_brew[host_brew_count - 1u].flow_decigrams : 0;
    view->show_curve = ready_curve;
    view->unlocked = true;
    view->target_enabled = ready_view.show_target;
    view->target_grams = ready_view.target_grams;
    view->target_sheet = m1_brew_target_sheet;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->sample_count = host_brew_count;
    view->samples = host_brew_count ? host_brew : NULL;
}

static void fill_m1_result(result_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->elapsed_seconds = (int)product_session.elapsed_seconds;
    view->total_water_decigrams = product_session.net_decigrams;
    view->target_grams = ready_view.show_target ? ready_view.target_grams : 0;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->save_open = m1_result_save_open;
    view->save_seconds_left = m1_result_save_open ? 30 : -1;
}

static void on_m1_brew_target(void *context)
{
    (void)context;
    m1_brew_target_sheet = true;
    product_view_dirty = true;
}

static void on_m1_brew_target_toggle(void *context)
{
    (void)context;
    ready_view.show_target = !ready_view.show_target;
    product_view_dirty = true;
}

static void on_m1_result_save(void *context)
{
    (void)context;
    /* Only close a host-opened prompt; ordinary results never show one and the
       result stays on screen. */
    m1_result_save_open = false;
    product_view_dirty = true;
}

static void on_m1_result_discard(void *context)
{
    (void)context;
    m1_result_save_open = false;
    product_view_dirty = true;
}

bool m1_brew_present(void)
{
    brew_view_t view;
    host_brew_begin();
    m1_brew_target_sheet = false;
    fill_m1_brew(&view);
    if(!po_ui_show_brew(&view)) return false;
    po_ui_brew_set_handlers(on_m1_brew_target, on_m1_brew_target_toggle, NULL);
    return true;
}

bool m1_brew_refresh(void)
{
    brew_view_t view;
    fill_m1_brew(&view);
    return po_ui_brew_set(&view);
}

bool m1_result_present(void)
{
    result_view_t view;
    m1_result_save_open = false;
    fill_m1_result(&view);
    if(!po_ui_show_result(&view)) return false;
    po_ui_result_set_handlers(on_m1_result_save, on_m1_result_discard, NULL);
    return true;
}

bool m1_result_refresh(void)
{
    result_view_t view;
    fill_m1_result(&view);
    return po_ui_result_set(&view);
}

void brew_reset(void)
{
    host_brew_count = 0;
    host_brew_origin = 0;
    host_brew_last = 0;
    m1_brew_target_sheet = false;
    m1_result_save_open = false;
}
