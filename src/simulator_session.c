#include "simulator_session.h"

void sim_session_init(sim_session_t *session)
{
    session->page = SIM_M1_READY;
    session->return_ready = SIM_M1_READY;
    session->net_decigrams = 180;
    session->elapsed_seconds = 0;
}

static sim_page_t mode_ready(sim_page_t page)
{
    if(page == SIM_M2_BREW || page == SIM_M2_RESULT || page == SIM_M2_READY) return SIM_M2_READY;
    return SIM_M1_READY;
}

static sim_key_result_t changed_page(void)
{
    sim_key_result_t result = {true, true};
    return result;
}

static sim_key_result_t changed_data(void)
{
    sim_key_result_t result = {false, true};
    return result;
}

static sim_key_result_t unchanged(void)
{
    sim_key_result_t result = {false, false};
    return result;
}

sim_key_result_t sim_session_key(sim_session_t *session, sim_key_t key, bool ui_consumed)
{
    if(ui_consumed) return unchanged();
    if(key == SIM_SWIPE_NEXT) {
        if(session->page == SIM_M1_READY) {
            session->page = SIM_M2_READY;
            return changed_page();
        }
        if(session->page == SIM_M2_READY) {
            session->return_ready = SIM_M2_READY;
            session->page = SIM_SETTINGS;
            return changed_page();
        }
        if(session->page == SIM_SETTINGS) {
            session->page = SIM_M1_READY;
            session->return_ready = SIM_M1_READY;
            return changed_page();
        }
        return unchanged();
    }
    if(key == SIM_KEY_RIGHT) {
        if(session->page == SIM_SETTINGS) return unchanged();
        session->net_decigrams = 0;
        if(session->page == SIM_M1_RESULT || session->page == SIM_M2_RESULT) {
            session->elapsed_seconds = 0;
            session->page = mode_ready(session->page);
            return changed_page();
        }
        return changed_data();
    }
    if(session->page == SIM_M1_READY || session->page == SIM_M2_READY) {
        session->elapsed_seconds = 0;
        session->page = session->page == SIM_M1_READY ? SIM_M1_BREW : SIM_M2_BREW;
        return changed_page();
    }
    if(session->page == SIM_M1_BREW || session->page == SIM_M2_BREW) {
        session->page = session->page == SIM_M1_BREW ? SIM_M1_RESULT : SIM_M2_RESULT;
        return changed_page();
    }
    if(session->page == SIM_M1_RESULT || session->page == SIM_M2_RESULT) {
        session->elapsed_seconds = 0;
        session->page = mode_ready(session->page);
        return changed_page();
    }
    session->page = session->return_ready;
    return changed_page();
}

void sim_session_add_weight(sim_session_t *session, int delta_decigrams)
{
    int next = session->net_decigrams + delta_decigrams;
    if(next > 99999) next = 99999;
    if(next < -99999) next = -99999;
    session->net_decigrams = next;
}

void sim_session_tick_brew(sim_session_t *session)
{
    if(session->page == SIM_M1_BREW || session->page == SIM_M2_BREW)
        session->elapsed_seconds++;
}
