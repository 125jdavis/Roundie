#include "splash_screen.h"

#include "navigation.h"

static void on_splash_gif_first_draw(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    navigation_sync_splash_countdown(app);
}

lv_obj_t *create_splash_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *gif = lv_gif_create(screen);
    lv_gif_set_src(gif, &supra_light_sweep_v3_gif);
    lv_obj_add_event_cb(gif, on_splash_gif_first_draw, LV_EVENT_DRAW_POST, app);
    lv_obj_center(gif);

    return screen;
}