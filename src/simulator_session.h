#ifndef SIMULATOR_SESSION_H
#define SIMULATOR_SESSION_H

#include <stdbool.h>

/* Host-side stand-in for the two hardware keys and the ready-page swipe.
 * The product UI does not own this. Swipe is Tab in the simulator because
 * the product enters the next mode by a horizontal swipe. */

typedef enum {
    SIM_M1_READY = 0,
    SIM_M1_BREW,
    SIM_M1_RESULT,
    SIM_M2_READY,
    SIM_M2_BREW,
    SIM_M2_RESULT,
    SIM_SETTINGS
} sim_page_t;

typedef enum {
    SIM_KEY_LEFT = 0,
    SIM_KEY_RIGHT,
    SIM_SWIPE_NEXT
} sim_key_t;

typedef struct {
    sim_page_t page;
    sim_page_t return_ready;
    int net_decigrams;
    unsigned elapsed_seconds;
} sim_session_t;

typedef struct {
    bool page_changed;
    bool data_changed;
} sim_key_result_t;

void sim_session_init(sim_session_t *session);
/* ui_consumed is true when the open product page already handled the key. */
sim_key_result_t sim_session_key(sim_session_t *session, sim_key_t key, bool ui_consumed);
void sim_session_add_weight(sim_session_t *session, int delta_decigrams);
void sim_session_tick_brew(sim_session_t *session);

#endif
