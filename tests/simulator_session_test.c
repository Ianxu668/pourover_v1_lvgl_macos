#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "simulator_session.h"

static void test_mode1_timer_and_tare(void)
{
    sim_session_t session;
    sim_key_result_t result;
    sim_session_init(&session);
    session.net_decigrams = 180;

    result = sim_session_key(&session, SIM_KEY_LEFT, false);
    assert(result.page_changed);
    assert(session.page == SIM_M1_BREW);
    assert(session.elapsed_seconds == 0);

    result = sim_session_key(&session, SIM_KEY_RIGHT, false);
    assert(!result.page_changed && result.data_changed);
    assert(session.page == SIM_M1_BREW);
    assert(session.net_decigrams == 0);

    session.net_decigrams = 500;
    result = sim_session_key(&session, SIM_KEY_LEFT, false);
    assert(session.page == SIM_M1_RESULT);

    result = sim_session_key(&session, SIM_KEY_RIGHT, false);
    assert(session.page == SIM_M1_READY);
    assert(session.net_decigrams == 0);
}

static void test_mode_swipe_and_settings_back(void)
{
    sim_session_t session;
    sim_session_init(&session);

    assert(sim_session_key(&session, SIM_SWIPE_NEXT, false).page_changed);
    assert(session.page == SIM_M2_READY);
    assert(sim_session_key(&session, SIM_SWIPE_NEXT, false).page_changed);
    assert(session.page == SIM_SETTINGS);
    assert(session.return_ready == SIM_M2_READY);

    assert(!sim_session_key(&session, SIM_KEY_RIGHT, false).data_changed);
    assert(session.page == SIM_SETTINGS);

    assert(sim_session_key(&session, SIM_KEY_LEFT, false).page_changed);
    assert(session.page == SIM_M2_READY);

    assert(sim_session_key(&session, SIM_KEY_LEFT, true).page_changed == false);
    assert(session.page == SIM_M2_READY);
}

static void test_swipe_ignored_while_brewing(void)
{
    sim_session_t session;
    sim_session_init(&session);
    assert(sim_session_key(&session, SIM_KEY_LEFT, false).page_changed);
    assert(!sim_session_key(&session, SIM_SWIPE_NEXT, false).page_changed);
    assert(session.page == SIM_M1_BREW);
    sim_session_tick_brew(&session);
    assert(session.elapsed_seconds == 1);
}

int main(void)
{
    test_mode1_timer_and_tare();
    test_mode_swipe_and_settings_back();
    test_swipe_ignored_while_brewing();
    puts("simulator session: PASS");
    return 0;
}
