#include <stdio.h>

#include "po_ui_freetype_readouts.h"
#include "simulator_fonts.h"

#ifndef SIMULATOR_FREETYPE_TEXT_FONT
#error "SIMULATOR_FREETYPE_TEXT_FONT must point to the product FreeType font"
#endif

bool simulator_fonts_init(void)
{
    if(!po_ui_set_freetype_readout_fonts(NULL, SIMULATOR_FREETYPE_TEXT_FONT)) {
        fprintf(stderr, "Could not configure FreeType font: %s\n", SIMULATOR_FREETYPE_TEXT_FONT);
        return false;
    }
    return true;
}
