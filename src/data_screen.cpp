#include "data_screen.h"

static lv_obj_t *make_data_name_label(lv_obj_t *screen, const char *text, int y) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_hex(0x7ab8f5), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(label, 360);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, AppConfig::CX - 180, y);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_data_val_label(lv_obj_t *screen, const char *init_text, int y) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(label, 360);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, AppConfig::CX - 180, y);
    lv_label_set_text(label, init_text);
    return label;
}

static void tick_data(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    char buf[24];

    if (app->can.ht.last_0x360_ms > 0 && (now - app->can.ht.last_0x360_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%u RPM", (unsigned)app->can.ht.rpm);
    } else {
        snprintf(buf, sizeof(buf), "-- RPM");
    }
    lv_label_set_text(app->data.rpm_val_label, buf);

    if (app->can.ht.last_0x360_ms > 0 && (now - app->can.ht.last_0x360_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%.1f %%", app->can.ht.tps_pct);
    } else {
        snprintf(buf, sizeof(buf), "--.-  %%");
    }
    lv_label_set_text(app->data.tps_val_label, buf);

    if (app->can.ht.last_0x3E0_ms > 0 && (now - app->can.ht.last_0x3E0_ms) < 3000) {
        snprintf(buf, sizeof(buf), "%.0f C", app->can.ht.coolant_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "-- C");
    }
    lv_label_set_text(app->data.coolant_val_label, buf);

    if (app->can.ht.last_0x370_ms > 0 && (now - app->can.ht.last_0x370_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%.0f km/h", app->can.ht.wheel_speed_kph);
    } else {
        snprintf(buf, sizeof(buf), "-- km/h");
    }
    lv_label_set_text(app->data.speed_val_label, buf);

    if (app->can.ht.last_0x361_ms > 0 && (now - app->can.ht.last_0x361_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%.0f kPa", app->can.ht.fuel_press_kpa);
    } else {
        snprintf(buf, sizeof(buf), "-- kPa");
    }
    lv_label_set_text(app->data.fuelpsi_val_label, buf);
}

lv_obj_t *create_data_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->data.screen = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    const int row_stride = 75;
    const int name_y0 = 60;

    make_data_name_label(screen, "RPM", name_y0 + 0 * row_stride);
    app->data.rpm_val_label = make_data_val_label(screen, "--", name_y0 + 0 * row_stride + 20);

    make_data_name_label(screen, "THROTTLE", name_y0 + 1 * row_stride);
    app->data.tps_val_label = make_data_val_label(screen, "--", name_y0 + 1 * row_stride + 20);

    make_data_name_label(screen, "COOLANT TEMP", name_y0 + 2 * row_stride);
    app->data.coolant_val_label = make_data_val_label(screen, "--", name_y0 + 2 * row_stride + 20);

    make_data_name_label(screen, "SPEED", name_y0 + 3 * row_stride);
    app->data.speed_val_label = make_data_val_label(screen, "--", name_y0 + 3 * row_stride + 20);

    make_data_name_label(screen, "FUEL PRESSURE", name_y0 + 4 * row_stride);
    app->data.fuelpsi_val_label = make_data_val_label(screen, "--", name_y0 + 4 * row_stride + 20);

    app->data.timer = lv_timer_create(tick_data, 200, app);
    return screen;
}