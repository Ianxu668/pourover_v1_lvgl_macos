#include "simulator_capture.h"
#include "lvgl.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool simulator_capture(const char *path)
{
    lv_draw_buf_t *snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if(snapshot == NULL) {
        fprintf(stderr, "Could not capture LVGL snapshot.\n");
        return false;
    }
    size_t width = snapshot->header.w, height = snapshot->header.h, stride = snapshot->header.stride;
    bool valid = snapshot->header.cf == LV_COLOR_FORMAT_RGB888 && snapshot->data != NULL &&
                 width > 0 && height > 0 && width <= SIZE_MAX / 3 && stride >= width * 3;
    if(!valid) {
        fprintf(stderr, "LVGL snapshot has unsupported format or dimensions.\n");
        lv_draw_buf_destroy(snapshot);
        return false;
    }
    FILE *file = fopen(path, "wb");
    if(file == NULL) {
        fprintf(stderr, "Could not open screenshot '%s': %s\n", path, strerror(errno));
        lv_draw_buf_destroy(snapshot);
        return false;
    }
    bool ok = fprintf(file, "P6\n%zu %zu\n255\n", width, height) > 0;
    for(size_t row = 0; ok && row < height; ++row) {
        const uint8_t *pixels = snapshot->data + row * stride;
        for(size_t x = 0; ok && x < width; ++x) {
            const uint8_t rgb[] = {pixels[x * 3 + 2], pixels[x * 3 + 1], pixels[x * 3]};
            ok = fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb);
        }
    }
    if(fclose(file) != 0) ok = false;
    lv_draw_buf_destroy(snapshot);
    if(!ok) fprintf(stderr, "Could not write screenshot '%s'.\n", path);
    return ok;
}
