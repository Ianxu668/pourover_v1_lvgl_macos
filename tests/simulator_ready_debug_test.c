#define _DEFAULT_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "../src/simulator_product.c"

/* Tare keys run on a synthetic keyboard; release is a second call with the key up. */
static bool press_key(uint16_t scancode)
{
    static uint8_t keys[SDL_NUM_SCANCODES];
    memset(keys, 0, sizeof(keys));
    keys[scancode] = 1;
    uint32_t until = 0;
    bool changed = ready_apply(keys, SDL_NUM_SCANCODES, &until);
    memset(keys, 0, sizeof(keys));
    ready_apply(keys, SDL_NUM_SCANCODES, &until);
    return changed;
}

int main(void)
{
    simulator_product_init();
    ready_view.container = PO_READY_CONTAINER_NONE;
    ready_view.net_decigrams = 180;
    assert(press_key(SDL_SCANCODE_A));
    assert(ready_view.container == PO_READY_CONTAINER_MATCHED);
    assert(ready_view.container_id == 1);
    assert(ready_view.tare_decigrams == 2004);
    assert(ready_view.net_decigrams == 0);
    assert(ready_view.note == PO_READY_NOTE_NONE);

    ready_view.container = PO_READY_CONTAINER_NONE;
    ready_view.net_decigrams = 55;
    assert(press_key(SDL_SCANCODE_N));
    assert(ready_view.container == PO_READY_CONTAINER_UNMATCHED);
    assert(ready_view.net_decigrams == 0);
    assert(ready_view.note == PO_READY_NOTE_NONE);

    assert(press_key(SDL_SCANCODE_T));
    assert(ready_view.net_decigrams == 0);
    assert(ready_view.container == PO_READY_CONTAINER_NONE);
    assert(ready_view.note == PO_READY_NOTE_TARED);
    puts("simulator ready debug: PASS (A/N tare zero the net weight)");
    return 0;
}
