#ifndef SIMULATOR_PRODUCT_INTERNAL_H
#define SIMULATOR_PRODUCT_INTERNAL_H

#include <stdint.h>
#include "pourover_ui.h"
#include "simulator_product.h"
#include "simulator_session.h"

/* State and page entry points shared by the simulator_product_*.c modules.
 * The public contract stays in simulator_product.h; everything here is
 * internal to the host simulator. */

/* One sample per second from t=0 through 30:00. Matches PO_M2_SAMPLE_MAX. */
#define HOST_BREW_CAP 1801

/* Session core and dispatch state (simulator_product.c). */
extern sim_session_t product_session;
extern bool product_view_dirty;
extern bool product_pending_page_valid;
extern sim_page_t product_pending_page;
void product_inject(sim_key_t key);

/* Mode 1 ready page (simulator_product_ready.c). */
extern po_ready_view_t ready_view;
extern bool ready_curve;
bool ready_present(const po_ui_locale_t *locale, uint32_t *note_until);
bool ready_start(uint32_t *note_until);
bool ready_refresh(void);
void ready_weight_changed(uint32_t *note_until);
void ready_reset(void);

/* Host brew sampling and mode 1 brew/result pages (simulator_product_brew.c). */
extern brew_sample_t host_brew[HOST_BREW_CAP];
extern unsigned host_brew_count;
void host_brew_begin(void);
void host_brew_sample(void);
void host_brew_follow(void);
/* Mode-1 brew replays one canned pour. The ready curve switch still picks the graphic. */
void m1_brew_pour_arm(void);
void m1_brew_pour_cancel(void);
void m1_brew_pour_step(void);
bool m1_brew_pour_running(void);
bool m1_brew_pour_poll(uint32_t now_ms);
bool m1_brew_present(void);
bool m1_brew_refresh(void);
bool m1_result_present(void);
bool m1_result_refresh(void);
void brew_reset(void);

/* Mode 2 pages (simulator_product_m2.c). */
bool m2_ready_present(void);
bool m2_ready_refresh(void);
bool m2_brew_present(void);
bool m2_brew_refresh(void);
bool m2_result_present(void);
bool m2_result_refresh(void);
void m2_start(const simulator_options_t *options);
void m2_reset(void);

/* Settings page (simulator_product_settings.c). */
bool settings_present(const po_ui_locale_t *locale);
void settings_start(const simulator_options_t *options, const po_ui_locale_t *locale);
void settings_open_home(void);
bool settings_service(void);
void settings_reset(void);

#endif
