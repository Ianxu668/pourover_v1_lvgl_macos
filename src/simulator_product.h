#ifndef SIMULATOR_PRODUCT_H
#define SIMULATOR_PRODUCT_H

#include <stdint.h>
#include "simulator_options.h"

void simulator_product_init(void);
po_ready_view_t *simulator_product_ready_view(void);
void simulator_product_ready_restore_params(void);
bool simulator_product_ready_debug(uint32_t *note_until);
bool simulator_product_start(const simulator_options_t *options, const po_ui_locale_t *locale,
                             uint32_t *note_until);
bool simulator_product_started(void);
sim_page_t simulator_product_page(void);
void simulator_product_poll(const po_ui_locale_t *locale, uint32_t *note_until);

#endif
