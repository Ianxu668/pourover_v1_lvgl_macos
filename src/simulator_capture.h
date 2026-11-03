#ifndef SIMULATOR_CAPTURE_H
#define SIMULATOR_CAPTURE_H
#include <stdbool.h>

/* Save the unscaled LVGL logical canvas; caller refreshes the desired frame first. */
bool simulator_capture(const char *path);
#endif
