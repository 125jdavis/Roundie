#include "candbg_screen.h"

#include "can_bus.h"

static void tick_candbg(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;

    uint32_t frames = app->can.frames_since_last_sample;
    app->can.frames_since_last_sample = 0;
    app->can.fps_display = (float)frames * 5.0f;

    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f frames/s", app->can.fps_display);
    lv_label_set_text(app->candbg.fps_label, buf);

    twai_status_info_t status;
    if (app->can.twai_ready && twai_get_status_info(&status) == ESP_OK) {
        const char *state_str = "UNKNOWN";
        switch (status.state) {
            case TWAI_STATE_STOPPED: state_str = "STOPPED"; break;
            case TWAI_STATE_RUNNING: state_str = "RUNNING"; break;
            case TWAI_STATE_BUS_OFF: state_str = "BUS-OFF"; break;
            case TWAI_STATE_RECOVERING: state_str = "RECOVERING"; break;
        }

        snprintf(buf, sizeof(buf), "State: %s @%s [%s]",
                 state_str,
                 can_rate_profile_text(app->can.rate_profile),
                 can_database_text(app->can.can_database));
        lv_label_set_text(app->candbg.state_label, buf);

        snprintf(buf, sizeof(buf), "RX err: %lu  TX err: %lu\nRX miss: %lu  Bus err: %lu",
                 (unsigned long)status.rx_error_counter,
                 (unsigned long)status.tx_error_counter,
                 (unsigned long)status.rx_missed_count,
                 (unsigned long)status.bus_error_count);
        lv_label_set_text(app->candbg.ids_label, buf);
    } else {
        lv_label_set_text(app->candbg.state_label, "TWAI not ready");
        lv_label_set_text(app->candbg.ids_label, "");
    }
}

lv_obj_t *create_candbg_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->candbg.screen = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf5a623), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_width(title, 360);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, AppConfig::CX - 180, 90);
    lv_label_set_text(title, "CAN BUS DEBUG");

    app->candbg.fps_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->candbg.fps_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->candbg.fps_label, &lv_font_montserrat_48, 0);
    lv_obj_set_width(app->candbg.fps_label, 360);
    lv_obj_set_style_text_align(app->candbg.fps_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->candbg.fps_label, AppConfig::CX - 180, 115);
    lv_label_set_text(app->candbg.fps_label, "-- frames/s");

    app->candbg.state_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->candbg.state_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(app->candbg.state_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(app->candbg.state_label, 360);
    lv_obj_set_style_text_align(app->candbg.state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->candbg.state_label, AppConfig::CX - 180, 210);
    lv_label_set_text(app->candbg.state_label, "State: --");

    app->candbg.ids_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->candbg.ids_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(app->candbg.ids_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(app->candbg.ids_label, 360);
    lv_obj_set_style_text_align(app->candbg.ids_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->candbg.ids_label, AppConfig::CX - 180, 240);
    lv_label_set_text(app->candbg.ids_label, "");

    app->candbg.timer = lv_timer_create(tick_candbg, 200, app);
    return screen;
}