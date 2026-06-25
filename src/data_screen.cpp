#include "data_screen.h"

#include <math.h>
#include <string.h>

static constexpr uint32_t DATA_FAST_TIMEOUT_MS = 1200;
static constexpr uint32_t DATA_MED_TIMEOUT_MS = 2500;
static constexpr uint32_t DATA_SLOW_TIMEOUT_MS = 3000;
static constexpr float FE_ARC_MAX_KM_PER_L = 20.0f;
static constexpr int VALUE_NUDGE_Y = 3;
static constexpr int UNIT_GAP_PX = 3;
static constexpr float DEMO_RPM_MIN = 750.0f;
static constexpr float DEMO_RPM_MAX = 6500.0f;
static constexpr float DEMO_SPEED_MIN = 0.0f;
static constexpr float DEMO_SPEED_MAX = 220.0f;
static constexpr float DEMO_TPS_MIN = 0.0f;
static constexpr float DEMO_TPS_MAX = 100.0f;
static constexpr float DEMO_COOLANT_MIN = 75.0f;
static constexpr float DEMO_COOLANT_MAX = 108.0f;
static constexpr float DEMO_IAT_MIN = 10.0f;
static constexpr float DEMO_IAT_MAX = 70.0f;
static constexpr float DEMO_AMBIENT_MIN = -5.0f;
static constexpr float DEMO_AMBIENT_MAX = 50.0f;
static constexpr float DEMO_FUEL_PRESS_MIN = 220.0f;
static constexpr float DEMO_FUEL_PRESS_MAX = 460.0f;
static constexpr float DEMO_BAP_MIN = 90.0f;
static constexpr float DEMO_BAP_MAX = 110.0f;
static constexpr float DEMO_BATT_MIN = 11.0f;
static constexpr float DEMO_BATT_MAX = 16.0f;
static constexpr float DEMO_ETHANOL_MIN = 0.0f;
static constexpr float DEMO_ETHANOL_MAX = 100.0f;
static constexpr float DEMO_FUEL_FLOW_MIN = 0.0f;
static constexpr float DEMO_FUEL_FLOW_MAX = 1500.0f;

static constexpr float DEMO_RPM_RATE_MAX = 300.0f;
static constexpr float DEMO_SPEED_RATE_MAX = 30.0f;
static constexpr float DEMO_TPS_RATE_MAX = 100.0f;
static constexpr float DEMO_TEMP_RATE_MAX = 1.0f;
static constexpr float DEMO_ETHANOL_RATE_MAX = 1.0f;
static constexpr float DEMO_BATT_RATE_MAX = 1.0f;
static constexpr float DEMO_FUEL_FLOW_RATE_MAX = 300.0f;

static constexpr uint32_t DEMO_GEAR_SHIFT_MS = 3000;

static bool is_recent(uint32_t now, uint32_t ts, uint32_t timeout_ms) {
    return ts > 0 && (now - ts) < timeout_ms;
}

static float rate_limit_toward(float current, float target, float max_rate_per_sec, float dt_s) {
    if (dt_s <= 0.0f || max_rate_per_sec <= 0.0f) return target;

    float max_delta = max_rate_per_sec * dt_s;
    float delta = target - current;
    if (delta > max_delta) return current + max_delta;
    if (delta < -max_delta) return current - max_delta;
    return target;
}

static float pressure_rate_limit(float min_value, float max_value) {
    return (max_value - min_value) * 0.5f;
}

static void reset_data_demo_trip(AppContext *app) {
    app->data.s4_distance_km = 0.0f;
    app->data.s4_fuel_used_l = 0.0f;
    app->data.s4_inst_fe_visual = 0.0f;
    app->data.s4_last_integrate_ms = 0;
    app->data.s4_visual_initialized = false;
}

void data_toggle_demo(AppContext *app, uint32_t now_ms) {
    DataScreenState &data = app->data;
    data.demo_mode = !data.demo_mode;
    data.demo_start_ms = now_ms;
    data.demo_last_step_ms = now_ms;
    data.demo_last_shift_ms = now_ms;
    data.demo_shift_up = true;

    data.demo_rpm = 900.0f;
    data.demo_tps_pct = 8.0f;
    data.demo_speed_kph = 0.0f;
    data.demo_gear = 0;
    data.demo_coolant_c = 88.0f;
    data.demo_iat_c = 30.0f;
    data.demo_ambient_c = 24.0f;
    data.demo_ethanol_pct = 70.0f;
    data.demo_fuel_press_kpa = 320.0f;
    data.demo_bap_kpa = 99.0f;
    data.demo_batt_v = 13.8f;
    data.demo_fuel_flow_cc_min = 180.0f;

    reset_data_demo_trip(app);
}

static void update_data_demo_state(AppContext *app, uint32_t now_ms) {
    DataScreenState &data = app->data;
    if (!data.demo_mode) return;

    if (data.demo_last_step_ms == 0) {
        data.demo_last_step_ms = now_ms;
        return;
    }

    uint32_t dt_ms = now_ms - data.demo_last_step_ms;
    if (dt_ms == 0) return;
    if (dt_ms > 250) dt_ms = 250;
    data.demo_last_step_ms = now_ms;

    float dt_s = (float)dt_ms * 0.001f;
    float t = (float)(now_ms - data.demo_start_ms) * 0.001f;

    float rpm_target = 850.0f + (0.5f + 0.5f * sinf(t * 0.75f)) * 4300.0f;
    float speed_target = (0.5f + 0.5f * sinf(t * 0.42f - 0.6f)) * 165.0f;
    float tps_target = 4.0f + (0.5f + 0.5f * sinf(t * 1.25f + 0.8f)) * 76.0f;
    float coolant_target = 88.0f + 5.0f * sinf(t * 0.06f);
    float iat_target = 32.0f + 7.0f * sinf(t * 0.09f + 1.1f);
    float ambient_target = 24.0f + 4.0f * sinf(t * 0.04f - 0.5f);
    float ethanol_target = 70.0f + 4.0f * sinf(t * 0.025f + 2.0f);
    float fuel_press_target = 300.0f + (0.7f * tps_target) + 22.0f * sinf(t * 0.85f);
    float bap_target = 99.0f + 1.8f * sinf(t * 0.16f);
    float batt_target = 13.9f + 0.35f * sinf(t * 0.20f);

    data.demo_rpm = rate_limit_toward(data.demo_rpm, rpm_target, DEMO_RPM_RATE_MAX, dt_s);
    data.demo_speed_kph = rate_limit_toward(data.demo_speed_kph, speed_target, DEMO_SPEED_RATE_MAX, dt_s);
    data.demo_tps_pct = rate_limit_toward(data.demo_tps_pct, tps_target, DEMO_TPS_RATE_MAX, dt_s);
    data.demo_coolant_c = rate_limit_toward(data.demo_coolant_c, coolant_target, DEMO_TEMP_RATE_MAX, dt_s);
    data.demo_iat_c = rate_limit_toward(data.demo_iat_c, iat_target, DEMO_TEMP_RATE_MAX, dt_s);
    data.demo_ambient_c = rate_limit_toward(data.demo_ambient_c, ambient_target, DEMO_TEMP_RATE_MAX, dt_s);
    data.demo_ethanol_pct = rate_limit_toward(data.demo_ethanol_pct, ethanol_target, DEMO_ETHANOL_RATE_MAX, dt_s);
    data.demo_fuel_press_kpa = rate_limit_toward(
        data.demo_fuel_press_kpa,
        fuel_press_target,
        pressure_rate_limit(DEMO_FUEL_PRESS_MIN, DEMO_FUEL_PRESS_MAX),
        dt_s);
    data.demo_bap_kpa = rate_limit_toward(
        data.demo_bap_kpa,
        bap_target,
        pressure_rate_limit(DEMO_BAP_MIN, DEMO_BAP_MAX),
        dt_s);
    data.demo_batt_v = rate_limit_toward(data.demo_batt_v, batt_target, DEMO_BATT_RATE_MAX, dt_s);

    float fuel_flow_target = 120.0f + (0.06f * data.demo_rpm) + (4.0f * data.demo_tps_pct);
    data.demo_fuel_flow_cc_min = rate_limit_toward(
        data.demo_fuel_flow_cc_min,
        fuel_flow_target,
        DEMO_FUEL_FLOW_RATE_MAX,
        dt_s);

    data.demo_rpm = clampf(data.demo_rpm, DEMO_RPM_MIN, DEMO_RPM_MAX);
    data.demo_speed_kph = clampf(data.demo_speed_kph, DEMO_SPEED_MIN, DEMO_SPEED_MAX);
    data.demo_tps_pct = clampf(data.demo_tps_pct, DEMO_TPS_MIN, DEMO_TPS_MAX);
    data.demo_coolant_c = clampf(data.demo_coolant_c, DEMO_COOLANT_MIN, DEMO_COOLANT_MAX);
    data.demo_iat_c = clampf(data.demo_iat_c, DEMO_IAT_MIN, DEMO_IAT_MAX);
    data.demo_ambient_c = clampf(data.demo_ambient_c, DEMO_AMBIENT_MIN, DEMO_AMBIENT_MAX);
    data.demo_ethanol_pct = clampf(data.demo_ethanol_pct, DEMO_ETHANOL_MIN, DEMO_ETHANOL_MAX);
    data.demo_fuel_press_kpa = clampf(data.demo_fuel_press_kpa, DEMO_FUEL_PRESS_MIN, DEMO_FUEL_PRESS_MAX);
    data.demo_bap_kpa = clampf(data.demo_bap_kpa, DEMO_BAP_MIN, DEMO_BAP_MAX);
    data.demo_batt_v = clampf(data.demo_batt_v, DEMO_BATT_MIN, DEMO_BATT_MAX);
    data.demo_fuel_flow_cc_min = clampf(data.demo_fuel_flow_cc_min, DEMO_FUEL_FLOW_MIN, DEMO_FUEL_FLOW_MAX);

    if (data.demo_speed_kph < 3.0f) {
        data.demo_gear = 0;
    } else if ((now_ms - data.demo_last_shift_ms) >= DEMO_GEAR_SHIFT_MS) {
        if (data.demo_gear <= 0) {
            data.demo_gear = 1;
            data.demo_shift_up = true;
        } else {
            if (data.demo_shift_up) {
                data.demo_gear++;
                if (data.demo_gear >= 6) {
                    data.demo_gear = 6;
                    data.demo_shift_up = false;
                }
            } else {
                data.demo_gear--;
                if (data.demo_gear <= 1) {
                    data.demo_gear = 1;
                    data.demo_shift_up = true;
                }
            }
        }
        data.demo_last_shift_ms = now_ms;
    }
}

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
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_set_width(label, 360);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, AppConfig::CX - 180, y + VALUE_NUDGE_Y);
    lv_label_set_text(label, init_text);
    return label;
}

static lv_obj_t *make_data_name_label_lg(lv_obj_t *screen, const char *text, int y) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_hex(0x7ab8f5), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(label, 420);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, AppConfig::CX - 210, y);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_data_val_label_lg(lv_obj_t *screen, const char *init_text, int y) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_set_width(label, 420);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, AppConfig::CX - 210, y + VALUE_NUDGE_Y);
    lv_label_set_text(label, init_text);
    return label;
}

static lv_obj_t *make_data_name_label_lg_at(lv_obj_t *screen, const char *text, int x, int y, int width) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_hex(0x7ab8f5), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_data_val_label_lg_at(lv_obj_t *screen, const char *init_text, int x, int y, int width) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x, y + VALUE_NUDGE_Y);
    lv_label_set_text(label, init_text);
    return label;
}

static lv_obj_t *make_unit_label_at(lv_obj_t *screen, const char *unit, int x, int y, int width) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_hex(0xBFC7CF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, unit);
    return label;
}

static void position_unit_right_of_value(lv_obj_t *value_label, lv_obj_t *unit_label) {
    if (!value_label || !unit_label) return;
    const char *text = lv_label_get_text(value_label);
    if (!text) text = "";

    const lv_font_t *value_font = lv_obj_get_style_text_font(value_label, LV_PART_MAIN);
    const lv_font_t *unit_font = lv_obj_get_style_text_font(unit_label, LV_PART_MAIN);
    if (!value_font || !unit_font) return;

    lv_coord_t letter_space = lv_obj_get_style_text_letter_space(value_label, LV_PART_MAIN);
    lv_coord_t text_w = lv_txt_get_width(text, (uint32_t)strlen(text), value_font, letter_space, LV_TEXT_FLAG_NONE);

    lv_coord_t value_x = lv_obj_get_x(value_label);
    lv_coord_t value_y = lv_obj_get_y(value_label);
    lv_coord_t value_w = lv_obj_get_width(value_label);
    lv_coord_t value_line_h = lv_font_get_line_height(value_font);
    lv_coord_t unit_line_h = lv_font_get_line_height(unit_font);

    lv_coord_t unit_x = value_x + value_w / 2 + text_w / 2 + UNIT_GAP_PX;
    lv_coord_t unit_y = value_y + value_line_h - unit_line_h;
    lv_obj_set_pos(unit_label, unit_x, unit_y);
}

static lv_obj_t *make_data_screen_base(AppContext *app, lv_obj_t **slot) {
    lv_obj_t *screen = lv_obj_create(NULL);
    *slot = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    (void)app;
    return screen;
}

static void tick_data1(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    bool demo_mode = app->data.demo_mode;
    if (demo_mode) update_data_demo_state(app, now);
    char buf[24];

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)lroundf(app->data.demo_rpm));
    } else if (is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)app->can.ht.rpm);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s1_rpm_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_tps_pct);
    } else if (is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.tps_pct);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s1_tps_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_coolant_c);
    } else if (is_recent(now, app->can.ht.last_0x3E0_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.coolant_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s1_coolant_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_speed_kph);
    } else if (is_recent(now, app->can.ht.last_0x370_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.wheel_speed_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s1_speed_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_fuel_press_kpa);
    } else if (is_recent(now, app->can.ht.last_0x361_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.fuel_press_kpa);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s1_fuelpsi_val_label, buf);

    position_unit_right_of_value(app->data.s1_rpm_val_label, app->data.s1_rpm_unit_label);
    position_unit_right_of_value(app->data.s1_tps_val_label, app->data.s1_tps_unit_label);
    position_unit_right_of_value(app->data.s1_coolant_val_label, app->data.s1_coolant_unit_label);
    position_unit_right_of_value(app->data.s1_speed_val_label, app->data.s1_speed_unit_label);
    position_unit_right_of_value(app->data.s1_fuelpsi_val_label, app->data.s1_fuelpress_unit_label);
}

static void tick_data2(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    bool demo_mode = app->data.demo_mode;
    if (demo_mode) update_data_demo_state(app, now);
    char buf[24];

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_batt_v);
    } else if (is_recent(now, app->can.ht.last_0x372_ms, DATA_MED_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.battery_volts);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_batt_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_fuel_press_kpa);
    } else if (is_recent(now, app->can.ht.last_0x361_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.fuel_press_kpa);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s2_fuelpress_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_bap_kpa);
    } else if (is_recent(now, app->can.ht.last_0x372_ms, DATA_MED_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.baro_kpa_abs);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_bap_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_coolant_c);
    } else if (is_recent(now, app->can.ht.last_0x3E0_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.coolant_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_ect_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_iat_c);
    } else if (is_recent(now, app->can.ht.last_0x3E0_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.air_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_iat_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_ethanol_pct);
    } else if (is_recent(now, app->can.ht.last_0x3E1_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.fuel_comp_pct);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_ethanol_val_label, buf);

    position_unit_right_of_value(app->data.s2_batt_val_label, app->data.s2_batt_unit_label);
    position_unit_right_of_value(app->data.s2_fuelpress_val_label, app->data.s2_fuelpress_unit_label);
    position_unit_right_of_value(app->data.s2_ethanol_val_label, app->data.s2_ethanol_unit_label);
    position_unit_right_of_value(app->data.s2_ect_val_label, app->data.s2_ect_unit_label);
    position_unit_right_of_value(app->data.s2_iat_val_label, app->data.s2_iat_unit_label);
    position_unit_right_of_value(app->data.s2_bap_val_label, app->data.s2_baro_unit_label);
}

static void tick_data3(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    bool demo_mode = app->data.demo_mode;
    if (demo_mode) update_data_demo_state(app, now);
    char buf[24];

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)lroundf(app->data.demo_rpm));
    } else if (is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)app->can.ht.rpm);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s3_rpm_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_speed_kph);
    } else if (is_recent(now, app->can.ht.last_0x370_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.wheel_speed_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s3_speed_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_tps_pct);
    } else if (is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.tps_pct);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s3_tps_val_label, buf);

    if (demo_mode) {
        if (app->data.demo_gear > 0) {
            snprintf(buf, sizeof(buf), "%d", (int)app->data.demo_gear);
        } else if (app->data.demo_gear == 0) {
            snprintf(buf, sizeof(buf), "N");
        } else {
            snprintf(buf, sizeof(buf), "R");
        }
    } else if (is_recent(now, app->can.ht.last_0x470_ms, DATA_MED_TIMEOUT_MS)) {
        if (app->can.ht.gear > 0) {
            snprintf(buf, sizeof(buf), "%d", (int)app->can.ht.gear);
        } else if (app->can.ht.gear == 0) {
            snprintf(buf, sizeof(buf), "N");
        } else if (app->can.ht.gear == -1) {
            snprintf(buf, sizeof(buf), "R");
        } else {
            snprintf(buf, sizeof(buf), "-");
        }
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s3_gear_val_label, buf);

    position_unit_right_of_value(app->data.s3_rpm_val_label, app->data.s3_rpm_unit_label);
    position_unit_right_of_value(app->data.s3_speed_val_label, app->data.s3_speed_unit_label);
    position_unit_right_of_value(app->data.s3_tps_val_label, app->data.s3_tps_unit_label);
}

static void tick_data4(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    bool demo_mode = app->data.demo_mode;
    if (demo_mode) update_data_demo_state(app, now);
    char buf[28];

    float speed_kph = 0.0f;
    float fuel_flow_cc_min = 0.0f;
    bool speed_valid = false;
    bool fuel_valid = false;

    if (demo_mode) {
        speed_valid = true;
        fuel_valid = true;
        speed_kph = app->data.demo_speed_kph;
        fuel_flow_cc_min = app->data.demo_fuel_flow_cc_min;
    } else {
        speed_valid = is_recent(now, app->can.ht.last_0x370_ms, DATA_FAST_TIMEOUT_MS);
        fuel_valid = is_recent(now, app->can.ht.last_0x371_ms, DATA_FAST_TIMEOUT_MS);
        if (speed_valid) speed_kph = app->can.ht.wheel_speed_kph;
        if (fuel_valid) fuel_flow_cc_min = app->can.ht.fuel_flow_cc_min;
    }

    if (app->data.s4_last_integrate_ms == 0) {
        app->data.s4_last_integrate_ms = now;
    }
    uint32_t dt_ms = now - app->data.s4_last_integrate_ms;
    app->data.s4_last_integrate_ms = now;

    float dt_h = (float)dt_ms / 3600000.0f;
    if (speed_valid && dt_h > 0.0f) {
        app->data.s4_distance_km += speed_kph * dt_h;
    }
    if (fuel_valid && dt_h > 0.0f) {
        float fuel_l_h = fuel_flow_cc_min * 0.06f;
        app->data.s4_fuel_used_l += fuel_l_h * dt_h;
    }

    float inst_fe = 0.0f;
    bool inst_fe_valid = false;
    if (speed_valid && fuel_valid) {
        float fuel_l_h = fuel_flow_cc_min * 0.06f;
        if (fuel_l_h > 0.03f && speed_kph > 0.5f) {
            inst_fe = speed_kph / fuel_l_h;
            inst_fe_valid = true;
        }
    }

    if (!app->data.s4_visual_initialized) {
        app->data.s4_inst_fe_visual = inst_fe_valid ? inst_fe : 0.0f;
        app->data.s4_visual_initialized = true;
    } else {
        float tau_ms = 120.0f;
        float alpha = (float)dt_ms / (tau_ms + (float)dt_ms);
        alpha = clampf(alpha, 0.08f, 0.75f);
        float target = inst_fe_valid ? inst_fe : 0.0f;
        app->data.s4_inst_fe_visual += (target - app->data.s4_inst_fe_visual) * alpha;
    }

    float arc_fe = clampf(app->data.s4_inst_fe_visual, 0.0f, FE_ARC_MAX_KM_PER_L);
    int arc_value = (int)lroundf((arc_fe / FE_ARC_MAX_KM_PER_L) * 1000.0f);
    lv_arc_set_value(app->data.s4_inst_fe_arc, arc_value);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_ambient_c);
    } else if (is_recent(now, app->can.ht.last_0x376_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.ambient_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s4_ambient_val_label, buf);

    snprintf(buf, sizeof(buf), "%.2f", app->data.s4_distance_km);
    lv_label_set_text(app->data.s4_distance_val_label, buf);

    if (inst_fe_valid) {
        snprintf(buf, sizeof(buf), "%.1f", inst_fe);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s4_inst_fe_val_label, buf);

    if (app->data.s4_fuel_used_l > 0.05f && app->data.s4_distance_km > 0.01f) {
        float accum_fe = app->data.s4_distance_km / app->data.s4_fuel_used_l;
        snprintf(buf, sizeof(buf), "%.1f", accum_fe);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s4_accum_fe_val_label, buf);

    position_unit_right_of_value(app->data.s4_ambient_val_label, app->data.s4_ambient_unit_label);
    position_unit_right_of_value(app->data.s4_distance_val_label, app->data.s4_distance_unit_label);
    position_unit_right_of_value(app->data.s4_inst_fe_val_label, app->data.s4_inst_fe_unit_label);
    position_unit_right_of_value(app->data.s4_accum_fe_val_label, app->data.s4_trip_fe_unit_label);
}

lv_obj_t *create_data_screen1(AppContext *app) {
    lv_obj_t *screen = make_data_screen_base(app, &app->data.screen1);

    const int row_stride = 82;
    const int name_y0 = 42;
    const int value_offset = 24;

    make_data_name_label(screen, "RPM", name_y0 + 0 * row_stride);
    app->data.s1_rpm_val_label = make_data_val_label(screen, "--", name_y0 + 0 * row_stride + value_offset);
    app->data.s1_rpm_unit_label = make_unit_label_at(screen, "RPM", 0, 0, 90);

    make_data_name_label(screen, "THROTTLE", name_y0 + 1 * row_stride);
    app->data.s1_tps_val_label = make_data_val_label(screen, "--", name_y0 + 1 * row_stride + value_offset);
    app->data.s1_tps_unit_label = make_unit_label_at(screen, "%", 0, 0, 60);

    make_data_name_label(screen, "COOLANT TEMP", name_y0 + 2 * row_stride);
    app->data.s1_coolant_val_label = make_data_val_label(screen, "--", name_y0 + 2 * row_stride + value_offset);
    app->data.s1_coolant_unit_label = make_unit_label_at(screen, "C", 0, 0, 60);

    make_data_name_label(screen, "SPEED", name_y0 + 3 * row_stride);
    app->data.s1_speed_val_label = make_data_val_label(screen, "--", name_y0 + 3 * row_stride + value_offset);
    app->data.s1_speed_unit_label = make_unit_label_at(screen, "km/h", 0, 0, 80);

    make_data_name_label(screen, "FUEL PRESSURE", name_y0 + 4 * row_stride);
    app->data.s1_fuelpsi_val_label = make_data_val_label(screen, "--", name_y0 + 4 * row_stride + value_offset);
    app->data.s1_fuelpress_unit_label = make_unit_label_at(screen, "kPa", 0, 0, 80);

    app->data.timer1 = lv_timer_create(tick_data1, 200, app);
    tick_data1(app->data.timer1);
    return screen;
}

lv_obj_t *create_data_screen2(AppContext *app) {
    lv_obj_t *screen = make_data_screen_base(app, &app->data.screen2);

    const int col_width = 180;
    const int left_x = 60;
    const int right_x = 216;
    const int row_stride = 146;
    const int name_y0 = 50;
    const int value_offset = 26;
    const int unit_w = 80;
    const int row_y_adjust[3] = {0, -10, -25};
    const int row_value_x_adjust[3] = {0, -10, -12};

    make_data_name_label_lg_at(screen, "BATT VOLT", left_x, name_y0 + 0 * row_stride + row_y_adjust[0], col_width);
    app->data.s2_batt_val_label = make_data_val_label_lg_at(screen, "--", left_x, name_y0 + 0 * row_stride + value_offset + row_y_adjust[0], col_width);
    app->data.s2_batt_unit_label = make_unit_label_at(screen, "V", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "FUEL PRESSURE", left_x, name_y0 + 1 * row_stride + row_y_adjust[1], col_width);
    app->data.s2_fuelpress_val_label = make_data_val_label_lg_at(screen, "--", left_x + row_value_x_adjust[1], name_y0 + 1 * row_stride + value_offset + row_y_adjust[1], col_width);
    app->data.s2_fuelpress_unit_label = make_unit_label_at(screen, "kPa", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "ETHANOL", left_x, name_y0 + 2 * row_stride + row_y_adjust[2], col_width);
    app->data.s2_ethanol_val_label = make_data_val_label_lg_at(screen, "--", left_x + row_value_x_adjust[2], name_y0 + 2 * row_stride + value_offset + row_y_adjust[2], col_width);
    app->data.s2_ethanol_unit_label = make_unit_label_at(screen, "%", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "ECT", right_x, name_y0 + 0 * row_stride + row_y_adjust[0], col_width);
    app->data.s2_ect_val_label = make_data_val_label_lg_at(screen, "--", right_x, name_y0 + 0 * row_stride + value_offset + row_y_adjust[0], col_width);
    app->data.s2_ect_unit_label = make_unit_label_at(screen, "C", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "IAT", right_x, name_y0 + 1 * row_stride + row_y_adjust[1], col_width);
    app->data.s2_iat_val_label = make_data_val_label_lg_at(screen, "--", right_x, name_y0 + 1 * row_stride + value_offset + row_y_adjust[1], col_width);
    app->data.s2_iat_unit_label = make_unit_label_at(screen, "C", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "BARO", right_x, name_y0 + 2 * row_stride + row_y_adjust[2], col_width);
    app->data.s2_bap_val_label = make_data_val_label_lg_at(screen, "--", right_x + row_value_x_adjust[2], name_y0 + 2 * row_stride + value_offset + row_y_adjust[2], col_width);
    app->data.s2_baro_unit_label = make_unit_label_at(screen, "kPa", 0, 0, unit_w);

    app->data.timer2 = lv_timer_create(tick_data2, 200, app);
    tick_data2(app->data.timer2);
    return screen;
}

lv_obj_t *create_data_screen3(AppContext *app) {
    lv_obj_t *screen = make_data_screen_base(app, &app->data.screen3);

    const int row_stride = 108;
    const int name_y0 = 30;
    const int value_offset = 26;
    const int unit_w = 90;
    const int row_value_x_adjust[4] = {-6, -6, 0, 0};

    make_data_name_label_lg(screen, "ENGINE SPEED", name_y0 + 0 * row_stride);
    app->data.s3_rpm_val_label = make_data_val_label_lg_at(screen, "--", AppConfig::CX - 210 + row_value_x_adjust[0], name_y0 + 0 * row_stride + value_offset, 420);
    app->data.s3_rpm_unit_label = make_unit_label_at(screen, "RPM", 0, 0, unit_w);

    make_data_name_label_lg(screen, "VEHICLE SPEED", name_y0 + 1 * row_stride);
    app->data.s3_speed_val_label = make_data_val_label_lg_at(screen, "--", AppConfig::CX - 210 + row_value_x_adjust[1], name_y0 + 1 * row_stride + value_offset, 420);
    app->data.s3_speed_unit_label = make_unit_label_at(screen, "km/h", 0, 0, unit_w);

    make_data_name_label_lg(screen, "TPS", name_y0 + 2 * row_stride);
    app->data.s3_tps_val_label = make_data_val_label_lg(screen, "--", name_y0 + 2 * row_stride + value_offset);
    app->data.s3_tps_unit_label = make_unit_label_at(screen, "%", 0, 0, unit_w);

    make_data_name_label_lg(screen, "GEAR", name_y0 + 3 * row_stride);
    app->data.s3_gear_val_label = make_data_val_label_lg(screen, "--", name_y0 + 3 * row_stride + value_offset);

    app->data.timer3 = lv_timer_create(tick_data3, 200, app);
    tick_data3(app->data.timer3);
    return screen;
}

lv_obj_t *create_data_screen4(AppContext *app) {
    lv_obj_t *screen = make_data_screen_base(app, &app->data.screen4);

    lv_obj_t *arc = lv_arc_create(screen);
    app->data.s4_inst_fe_arc = arc;
    lv_obj_set_size(arc, 420, 420);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, (int)AppConfig::AFR_ARC_START_DEG, (int)AppConfig::AFR_ARC_END_DEG);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 22, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x3b3b3b), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x20c870), LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    const int row_stride = 96;
    const int name_y0 = 34;
    const int value_offset = 26;
    const int unit_w = 90;
    const int value_x_shift = -7;

    make_data_name_label_lg(screen, "AMBIENT TEMP", name_y0 + 0 * row_stride);
    app->data.s4_ambient_val_label = make_data_val_label_lg_at(screen, "--", AppConfig::CX - 210 + value_x_shift, name_y0 + 0 * row_stride + value_offset, 420);
    app->data.s4_ambient_unit_label = make_unit_label_at(screen, "C", 0, 0, unit_w);

    make_data_name_label_lg(screen, "TRIP DISTANCE", name_y0 + 1 * row_stride);
    app->data.s4_distance_val_label = make_data_val_label_lg_at(screen, "0.00", AppConfig::CX - 210 + value_x_shift, name_y0 + 1 * row_stride + value_offset, 420);
    app->data.s4_distance_unit_label = make_unit_label_at(screen, "km", 0, 0, unit_w);

    make_data_name_label_lg(screen, "TRIP FUEL ECONOMY", name_y0 + 2 * row_stride);
    app->data.s4_accum_fe_val_label = make_data_val_label_lg_at(screen, "--", AppConfig::CX - 210 + value_x_shift, name_y0 + 2 * row_stride + value_offset, 420);
    app->data.s4_trip_fe_unit_label = make_unit_label_at(screen, "km/L", 0, 0, unit_w);

    make_data_name_label_lg(screen, "INST FUEL ECONOMY", name_y0 + 3 * row_stride);
    app->data.s4_inst_fe_val_label = make_data_val_label_lg_at(screen, "--", AppConfig::CX - 210 + value_x_shift, name_y0 + 3 * row_stride + value_offset, 420);
    app->data.s4_inst_fe_unit_label = make_unit_label_at(screen, "km/L", 0, 0, unit_w);

    app->data.timer4 = lv_timer_create(tick_data4, 100, app);
    tick_data4(app->data.timer4);
    return screen;
}