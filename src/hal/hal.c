#include "hal.h"
#include <SDL.h>
#include <stdio.h>

static void configure_presentation(lv_display_t *display)
{
  SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
  SDL_Window *window = lv_sdl_window_get_window(display);
  if(!renderer || !window) {
    fprintf(stderr, "SDL presentation handles unavailable; VSYNC not configured.\n");
    return;
  }
#if SDL_VERSION_ATLEAST(2, 0, 18)
  if(SDL_RenderSetVSync(renderer, 1) == 0) puts("SDL VSYNC requested successfully (actual cadence requires measurement).");
  else fprintf(stderr, "SDL VSYNC unavailable: %s\n", SDL_GetError());
#else
  puts("SDL headers predate 2.0.18; explicit VSYNC is unavailable.");
#endif
  int width = 0, height = 0, drawable_width = 0, drawable_height = 0;
  SDL_GetWindowSize(window, &width, &height);
  if(SDL_GetRendererOutputSize(renderer, &drawable_width, &drawable_height) != 0)
    fprintf(stderr, "Could not query renderer output size: %s\n", SDL_GetError());
  SDL_DisplayMode mode;
  int index = SDL_GetWindowDisplayIndex(window);
  if(index >= 0 && SDL_GetCurrentDisplayMode(index, &mode) == 0)
    printf("Display %dx%d @ %d Hz; window %dx%d; drawable %dx%d; logical UI 960x376\n",
           mode.w, mode.h, mode.refresh_rate, width, height, drawable_width, drawable_height);
  else fprintf(stderr, "Could not query display mode: %s\n", SDL_GetError());
}


lv_display_t * sdl_hal_init(int32_t w, int32_t h)
{

  lv_group_set_default(lv_group_create());

  lv_display_t * disp = lv_sdl_window_create(w, h);
  if(!disp) return NULL;
  configure_presentation(disp);

  lv_indev_t * mouse = lv_sdl_mouse_create();
  lv_indev_set_group(mouse, lv_group_get_default());
  lv_indev_set_display(mouse, disp);
  lv_display_set_default(disp);
  /*Declare the image file.*/
  LV_IMAGE_DECLARE(mouse_cursor_icon); 
  lv_obj_t * cursor_obj;
  /*Create an image object for the cursor */
  cursor_obj = lv_image_create(lv_screen_active()); 
  /*Set the image source*/
  lv_image_set_src(cursor_obj, &mouse_cursor_icon);           
  /*Connect the image  object to the driver*/
  lv_indev_set_cursor(mouse, cursor_obj);             

  lv_indev_t * mousewheel = lv_sdl_mousewheel_create();
  lv_indev_set_display(mousewheel, disp);
  lv_indev_set_group(mousewheel, lv_group_get_default());

  lv_indev_t * kb = lv_sdl_keyboard_create();
  lv_indev_set_display(kb, disp);
  lv_indev_set_group(kb, lv_group_get_default());

  return disp;
}
