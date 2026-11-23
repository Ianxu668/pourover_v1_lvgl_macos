#include <string.h>
#include "simulator_product_internal.h"

brew_sample_t host_brew[HOST_BREW_CAP];
unsigned host_brew_count;
static int host_brew_origin;
static int host_brew_last;

/* Mode-1 pour input updates every 250 ms; curve history remains one sample
 * per second. Physical input can exceed the UI's 8 g/s visual saturation. */
#define M1_POUR_STEP_MS 250u
static const int m1_pour_dg_s[] = {
    0, 80, 0, 220, 20, 150, 0,
    300, 300, 300, 300, 300, 300, 300, 300,
    0, 300, 40, 180, 0, 250, 10,
    300, 300, 300, 300, 300, 300, 300, 300,
    0, 80, 0, 220, 20, 150, 0,
    300, 300, 300, 300, 300, 300, 300, 300
};
static unsigned m1_pour_at;
static uint32_t m1_pour_due;
static bool m1_pour_on;
static int m1_pour_rate;
static result_sample_t m1_result_samples[HOST_BREW_CAP];

void m1_brew_pour_arm(void)
{
    m1_pour_rate = 0;
    m1_pour_on = true;
    m1_pour_at = 0;
    m1_pour_due = 0;
}

void m1_brew_pour_cancel(void)
{
    m1_pour_rate = 0;
    m1_pour_on = false;
    m1_pour_at = 0;
    m1_pour_due = 0;
}

bool m1_brew_pour_running(void)
{
    return m1_pour_on;
}

void m1_brew_pour_step(void)
{
    int rate;
    int gain;
    int next;
    if(!m1_pour_on) return;
    if(m1_pour_at >= sizeof m1_pour_dg_s / sizeof m1_pour_dg_s[0]) {
        m1_pour_on = false;
        return;
    }
    rate = m1_pour_dg_s[m1_pour_at++];
    m1_pour_rate = rate;
    gain = (int)((int64_t)rate * M1_POUR_STEP_MS / 1000);
    next = product_session.net_decigrams + gain;
    if(next > 99999) next = 99999;
    if(next < -99999) next = -99999;
    product_session.net_decigrams = next;
    if(host_brew_count == 0) {
        host_brew[0].weight_decigrams = host_brew_origin;
        host_brew[0].flow_decigrams = 0;
        host_brew_count = 1;
    }
}

bool m1_brew_pour_poll(uint32_t now_ms)
{
    if(!m1_pour_on) return false;
    if(now_ms < m1_pour_due) return false;
    m1_pour_due = now_ms + M1_POUR_STEP_MS;
    m1_brew_pour_step();
    return true;
}

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
    if(product_session.page == SIM_M1_BREW && m1_pour_on && m1_pour_at)
        flow = m1_pour_rate;
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

void host_brew_follow(bool tared)
{
    int weight = product_session.net_decigrams;
    int previous;
    int flow;
    if(tared) {
        host_brew_origin = weight;
        host_brew_last = weight;
    }
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

static int m1_poured_water(void)
{
    int water = product_session.net_decigrams - host_brew_origin;
    return water > 0 ? water : 0;
}

static void fill_m1_brew(brew_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->elapsed_seconds = product_session.elapsed_seconds;
    view->net_decigrams = product_session.net_decigrams;
    view->flow_decigrams = m1_pour_on && m1_pour_at ? m1_pour_rate
        : host_brew_count ? host_brew[host_brew_count - 1u].flow_decigrams : 0;
    view->show_curve = ready_curve;
    view->unlocked = true;
    view->target_enabled = ready_view.show_target;
    view->target_grams = ready_view.target_grams;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    int64_t water = m1_poured_water();
    if(view->target_enabled && view->target_grams > 0 && water > 0) {
        int64_t progress = view->ounces
            ? water * 28349523125LL / ((int64_t)view->target_grams * 100000000LL)
            : water * 100 / view->target_grams;
        view->progress_permille = progress > 1000 ? 1000u : (unsigned)progress;
    }
    view->target_sheet = m1_brew_target_sheet;
    view->sample_count = host_brew_count;
    view->samples = host_brew_count ? host_brew : NULL;
}

static void fill_m1_result(result_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->elapsed_seconds = (int)product_session.elapsed_seconds;
    view->total_water_decigrams = m1_poured_water();
    view->target_grams = ready_view.show_target ? ready_view.target_grams : 0;
    view->ounces = ready_view.unit == PO_READY_UNIT_OZ;
    view->save_open = m1_result_save_open;
    view->save_seconds_left = m1_result_save_open ? 30 : -1;
    view->sample_count = host_brew_count;
    view->samples = host_brew_count ? m1_result_samples : NULL;
    for(unsigned i = 0; i < host_brew_count; ++i) {
        m1_result_samples[i] = (result_sample_t){host_brew[i].weight_decigrams,
                                               host_brew[i].flow_decigrams};
        if(host_brew[i].flow_decigrams > view->peak_flow_decigrams)
            view->peak_flow_decigrams = host_brew[i].flow_decigrams;
    }
    if(view->elapsed_seconds > 0 && view->total_water_decigrams > 0)
        view->avg_flow_decigrams = view->total_water_decigrams / view->elapsed_seconds;
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
    m1_brew_pour_arm();
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

/* The shared result-idle tracker asks whether a save ask is holding the page. */
bool m1_result_save_open_state(void)
{
    return m1_result_save_open;
}

void brew_reset(void)
{
    host_brew_count = 0;
    host_brew_origin = 0;
    host_brew_last = 0;
    m1_brew_target_sheet = false;
    m1_result_save_open = false;
    m1_brew_pour_cancel();
}
