#include "data_screen.h"
#include "can_bus.h"

#include <math.h>
#include <string.h>

static constexpr uint32_t DATA_FAST_TIMEOUT_MS = 1200;
static constexpr uint32_t DATA_MED_TIMEOUT_MS = 2500;
static constexpr uint32_t DATA_SLOW_TIMEOUT_MS = 3000;
static constexpr uint32_t DATA_DRIVING_UPDATE_MS = 1000 / 12;
static constexpr float FE_ARC_MAX_MPG = 40.0f;
static constexpr int FE_BAR_X = 58;
static constexpr int FE_BAR_Y = 230;
static constexpr int FE_BAR_W = 348;
static constexpr int FE_BAR_H = 44;
static constexpr int FE_MARKER_W = 28;
static constexpr int FE_MARKER_H = 28;
static constexpr int FE_MARKER_TOP_Y = 224;
static constexpr int BELOW_DV_UNIT_GAP_ENGINE = -4;
static constexpr int BELOW_DV_UNIT_GAP_DRIVING = -8;
static constexpr int VALUE_NUDGE_Y = 3;
static constexpr int UNIT_GAP_PX = 3;
static constexpr float DEMO_RPM_MIN = 750.0f;
static constexpr float DEMO_RPM_MAX = 7500.0f;
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
static constexpr uint32_t DEMO_RPM_RAMP_HALF_CYCLE_MS = 6000;

static constexpr uint32_t DEMO_GEAR_SHIFT_MS = 3000;

static float c_to_f(float temp_c) {
    return temp_c * 1.8f + 32.0f;
}

static float km_to_miles(float km) {
    return km * 0.621371f;
}

static float kph_to_mph(float kph) {
    return km_to_miles(kph);
}

static float km_per_l_to_mpg(float km_per_l) {
    return km_per_l * 2.3521458f;
}

static float l_per_100km_to_mpg(float l_per_100km) {
    if (l_per_100km <= 0.0f) return 0.0f;
    return 235.21458f / l_per_100km;
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

    uint32_t rpm_cycle_ms = DEMO_RPM_RAMP_HALF_CYCLE_MS * 2U;
    uint32_t rpm_phase_ms = (now_ms - data.demo_start_ms) % rpm_cycle_ms;
    float rpm_t = 0.0f;
    if (rpm_phase_ms < DEMO_RPM_RAMP_HALF_CYCLE_MS) {
        rpm_t = (float)rpm_phase_ms / (float)DEMO_RPM_RAMP_HALF_CYCLE_MS;
    } else {
        rpm_t = (float)(rpm_cycle_ms - rpm_phase_ms) / (float)DEMO_RPM_RAMP_HALF_CYCLE_MS;
    }
    float rpm_target = DEMO_RPM_MIN + rpm_t * (DEMO_RPM_MAX - DEMO_RPM_MIN);
    float speed_target = (0.5f + 0.5f * sinf(t * 0.42f - 0.6f)) * 165.0f;
    float tps_target = 4.0f + (0.5f + 0.5f * sinf(t * 1.25f + 0.8f)) * 76.0f;
    float coolant_target = 88.0f + 5.0f * sinf(t * 0.06f);
    float iat_target = 32.0f + 7.0f * sinf(t * 0.09f + 1.1f);
    float ambient_target = 24.0f + 4.0f * sinf(t * 0.04f - 0.5f);
    float ethanol_target = 70.0f + 4.0f * sinf(t * 0.025f + 2.0f);
    float fuel_press_target = 300.0f + (0.7f * tps_target) + 22.0f * sinf(t * 0.85f);
    float bap_target = 99.0f + 1.8f * sinf(t * 0.16f);
    float batt_target = 13.9f + 0.35f * sinf(t * 0.20f);

    data.demo_rpm = rpm_target;
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

static void position_value_label_left_for_overflow(lv_obj_t *label, lv_coord_t base_x) {
    if (!label) return;

    const char *text = lv_label_get_text(label);
    if (!text || !*text) return;

    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (!font) return;

    lv_coord_t letter_space = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
    lv_coord_t text_w = lv_txt_get_width(text, (uint32_t)strlen(text), font, letter_space, LV_TEXT_FLAG_NONE);

    lv_coord_t available_w = 480 - base_x;
    if (available_w < 1) available_w = 1;

    lv_obj_set_width(label, available_w);

    if (text_w > available_w) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_coord_t shift = text_w - available_w + 6;
        lv_coord_t new_x = base_x - shift;
        if (new_x < 0) new_x = 0;
        lv_obj_set_x(label, new_x);
    } else {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_x(label, base_x);
    }
}

static lv_obj_t *make_unit_label_at(AppContext *app, lv_obj_t *screen, const char *unit, int x, int y, int width) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, app_primary_text_color(app), 0);
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

static void position_unit_right_of_value_with_y_offset(lv_obj_t *value_label, lv_obj_t *unit_label, lv_coord_t y_offset_px) {
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
    lv_coord_t unit_y = value_y + value_line_h - unit_line_h + y_offset_px;
    lv_obj_set_pos(unit_label, unit_x, unit_y);
}

static void position_unit_below_value_centered(lv_obj_t *value_label, lv_obj_t *unit_label, lv_coord_t gap_px) {
    if (!value_label || !unit_label) return;
    const lv_font_t *unit_font = lv_obj_get_style_text_font(unit_label, LV_PART_MAIN);
    if (!unit_font) return;

    lv_coord_t value_x = lv_obj_get_x(value_label);
    lv_coord_t value_y = lv_obj_get_y(value_label);
    lv_coord_t value_w = lv_obj_get_width(value_label);
    lv_coord_t value_h = lv_obj_get_height(value_label);
    lv_coord_t unit_w = lv_obj_get_width(unit_label);

    lv_coord_t unit_x = value_x + (value_w - unit_w) / 2;
    lv_coord_t unit_y = value_y + value_h + gap_px;
    lv_obj_set_pos(unit_label, unit_x, unit_y);
}

static void position_unit_below_value_centered_to_title(lv_obj_t *value_label, lv_obj_t *unit_label,
                                                         lv_coord_t title_x, lv_coord_t title_w,
                                                         lv_coord_t gap_px) {
    if (!value_label || !unit_label) return;

    lv_coord_t value_y = lv_obj_get_y(value_label);
    lv_coord_t value_h = lv_obj_get_height(value_label);
    lv_coord_t unit_y = value_y + value_h + gap_px;

    lv_obj_set_width(unit_label, title_w);
    lv_obj_set_style_text_align(unit_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(unit_label, title_x, unit_y);
}

static lv_obj_t *make_data_screen_base(AppContext *app, lv_obj_t **slot) {
    lv_obj_t *screen = lv_obj_create(NULL);
    *slot = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    (void)app;
    return screen;
}

static void draw_down_triangle_marker_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_coord_t w = lv_area_get_width(&coords);
    lv_coord_t h = lv_area_get_height(&coords);

    lv_point_t tri[3] = {
        {(lv_coord_t)(coords.x1 + w / 2), (lv_coord_t)coords.y2},
        {(lv_coord_t)coords.x1, (lv_coord_t)coords.y1},
        {(lv_coord_t)coords.x2, (lv_coord_t)coords.y1}
    };

    lv_draw_rect_dsc_t fill_dsc;
    lv_draw_rect_dsc_init(&fill_dsc);
    fill_dsc.bg_color = app ? app_primary_text_color(app) : lv_color_hex(0xFFFFFF);
    fill_dsc.bg_opa = LV_OPA_COVER;
    fill_dsc.border_width = 0;

    lv_draw_polygon(draw_ctx, &fill_dsc, tri, 3);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x202020);
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;

    lv_draw_line(draw_ctx, &line_dsc, &tri[0], &tri[1]);
    lv_draw_line(draw_ctx, &line_dsc, &tri[0], &tri[2]);
    lv_draw_line(draw_ctx, &line_dsc, &tri[1], &tri[2]);
}

static void tick_data1(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    bool demo_mode = app->data.demo_mode;
    if (demo_mode) update_data_demo_state(app, now);
    char buf[24];

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)lroundf(app->data.demo_rpm));
    } else if (can_is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)app->can.ht.rpm);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s1_rpm_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_tps_pct);
    } else if (can_is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.tps_pct);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s1_tps_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_coolant_c);
    } else if (can_is_recent(now, app->can.ht.last_0x3E0_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.coolant_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s1_coolant_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", kph_to_mph(app->data.demo_speed_kph));
    } else if (can_is_recent(now, app->can.ht.last_0x370_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", kph_to_mph(app->can.ht.wheel_speed_kph));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s1_speed_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_fuel_press_kpa);
    } else if (can_is_recent(now, app->can.ht.last_0x361_ms, DATA_FAST_TIMEOUT_MS)) {
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
    } else if (can_is_recent(now, app->can.ht.last_0x372_ms, DATA_MED_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.battery_volts);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_batt_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_fuel_press_kpa);
    } else if (can_is_recent(now, app->can.ht.last_0x361_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.fuel_press_kpa);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s2_fuelpress_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_bap_kpa);
    } else if (can_is_recent(now, app->can.ht.last_0x372_ms, DATA_MED_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.baro_kpa_abs);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_bap_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_coolant_c);
    } else if (can_is_recent(now, app->can.ht.last_0x3E0_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.coolant_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_ect_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_iat_c);
    } else if (can_is_recent(now, app->can.ht.last_0x3E0_ms, DATA_SLOW_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.1f", app->can.ht.air_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s2_iat_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.1f", app->data.demo_ethanol_pct);
    } else if (can_is_recent(now, app->can.ht.last_0x3E1_ms, DATA_SLOW_TIMEOUT_MS)) {
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
    } else if (can_is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)app->can.ht.rpm);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s3_rpm_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", kph_to_mph(app->data.demo_speed_kph));
    } else if (can_is_recent(now, app->can.ht.last_0x370_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", kph_to_mph(app->can.ht.wheel_speed_kph));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(app->data.s3_speed_val_label, buf);

    if (demo_mode) {
        snprintf(buf, sizeof(buf), "%.0f", app->data.demo_tps_pct);
    } else if (can_is_recent(now, app->can.ht.last_0x360_ms, DATA_FAST_TIMEOUT_MS)) {
        snprintf(buf, sizeof(buf), "%.0f", app->can.ht.tps_pct);
    } else {
        snprintf(buf, sizeof(buf), "--");
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
    } else if (can_is_recent(now, app->can.ht.last_0x470_ms, DATA_MED_TIMEOUT_MS)) {
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

    position_unit_below_value_centered_to_title(app->data.s3_speed_val_label, app->data.s3_speed_unit_label,
                                                241, 160, BELOW_DV_UNIT_GAP_ENGINE);
    position_unit_below_value_centered_to_title(app->data.s3_tps_val_label, app->data.s3_tps_unit_label,
                                                56, 188, BELOW_DV_UNIT_GAP_ENGINE);
}

static void tick_data4(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    bool demo_mode = app->data.demo_mode;
    bool daniel_ike = app->can.can_database == CAN_DB_DANIEL_IKE_GAUGE;
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
        speed_valid = can_is_recent(now, app->can.ht.last_0x370_ms, DATA_FAST_TIMEOUT_MS);
        fuel_valid = can_is_recent(now, app->can.ht.last_0x371_ms, DATA_FAST_TIMEOUT_MS);
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

    bool ambient_valid = false;
    float ambient_display_f = 0.0f;
    bool distance_valid = false;
    float distance_display_miles = 0.0f;
    bool inst_economy_valid = false;
    float inst_economy_display_mpg = 0.0f;
    bool trip_economy_valid = false;
    float trip_economy_display_mpg = 0.0f;

    if (demo_mode) {
        ambient_valid = true;
        ambient_display_f = c_to_f(app->data.demo_ambient_c);
        distance_valid = true;
        distance_display_miles = km_to_miles(app->data.s4_distance_km);
        if (inst_fe_valid) {
            inst_economy_valid = true;
            inst_economy_display_mpg = km_per_l_to_mpg(inst_fe);
        }
        if (app->data.s4_fuel_used_l > 0.05f && app->data.s4_distance_km > 0.01f) {
            trip_economy_valid = true;
            trip_economy_display_mpg = km_per_l_to_mpg(app->data.s4_distance_km / app->data.s4_fuel_used_l);
        }
    } else if (daniel_ike) {
        ambient_valid = can_is_recent(now, app->can.ht.last_0x3E0_ms, DATA_SLOW_TIMEOUT_MS);
        if (ambient_valid) ambient_display_f = c_to_f(app->can.ht.ambient_temp_c);

        bool economy_valid = can_is_recent(now, app->can.ht.last_0x3E1_ms, DATA_SLOW_TIMEOUT_MS);
        if (economy_valid) {
            distance_valid = true;
            distance_display_miles = km_to_miles(app->can.ht.trip_distance_km);

            if (app->can.ht.inst_fuel_per_100km > 0.0f) {
                inst_economy_valid = true;
                inst_economy_display_mpg = l_per_100km_to_mpg(app->can.ht.inst_fuel_per_100km);
            }

            if (app->can.ht.trip_fuel_per_100km > 0.0f) {
                trip_economy_valid = true;
                trip_economy_display_mpg = l_per_100km_to_mpg(app->can.ht.trip_fuel_per_100km);
            }
        }
    } else {
        ambient_valid = can_is_recent(now, app->can.ht.last_0x376_ms, DATA_SLOW_TIMEOUT_MS);
        if (ambient_valid) ambient_display_f = c_to_f(app->can.ht.ambient_temp_c);

        distance_valid = true;
        distance_display_miles = km_to_miles(app->data.s4_distance_km);

        if (inst_fe_valid) {
            inst_economy_valid = true;
            inst_economy_display_mpg = km_per_l_to_mpg(inst_fe);
        }

        if (app->data.s4_fuel_used_l > 0.05f && app->data.s4_distance_km > 0.01f) {
            trip_economy_valid = true;
            trip_economy_display_mpg = km_per_l_to_mpg(app->data.s4_distance_km / app->data.s4_fuel_used_l);
        }
    }

    float bar_fe = clampf(inst_economy_display_mpg, 0.0f, FE_ARC_MAX_MPG);
    int bar_value = (int)lroundf((bar_fe / FE_ARC_MAX_MPG) * 1000.0f);
    lv_bar_set_value(app->data.s4_inst_fe_arc, bar_value, LV_ANIM_OFF);

    if (app->data.s4_trip_fe_marker) {
        if (trip_economy_valid) {
            int marker_x = FE_BAR_X - FE_MARKER_W / 2 +
                           (int)lroundf((clampf(trip_economy_display_mpg, 0.0f, FE_ARC_MAX_MPG) / FE_ARC_MAX_MPG) * (float)FE_BAR_W);
            int min_x = FE_BAR_X - FE_MARKER_W / 2;
            int max_x = FE_BAR_X + FE_BAR_W - FE_MARKER_W / 2;
            if (marker_x < min_x) marker_x = min_x;
            if (marker_x > max_x) marker_x = max_x;
            lv_obj_set_pos(app->data.s4_trip_fe_marker, marker_x, FE_MARKER_TOP_Y);
            lv_obj_clear_flag(app->data.s4_trip_fe_marker, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->data.s4_trip_fe_marker, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (ambient_valid) {
        snprintf(buf, sizeof(buf), "%.0f°", ambient_display_f);
    } else {
        snprintf(buf, sizeof(buf), "--°");
    }
    lv_label_set_text(app->data.s4_ambient_val_label, buf);

    if (distance_valid) {
        if (distance_display_miles >= 100.0f) {
            snprintf(buf, sizeof(buf), "%.0f", distance_display_miles);
        } else {
            snprintf(buf, sizeof(buf), "%.1f", distance_display_miles);
        }
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s4_distance_val_label, buf);

    if (inst_economy_valid) {
        snprintf(buf, sizeof(buf), "%.1f", inst_economy_display_mpg);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s4_inst_fe_val_label, buf);

    if (trip_economy_valid) {
        snprintf(buf, sizeof(buf), "%.1f", trip_economy_display_mpg);
    } else {
        snprintf(buf, sizeof(buf), "--.-");
    }
    lv_label_set_text(app->data.s4_accum_fe_val_label, buf);
    position_value_label_left_for_overflow(app->data.s4_accum_fe_val_label, AppConfig::CX - 210 - 6);

    position_unit_below_value_centered_to_title(app->data.s4_distance_val_label, app->data.s4_distance_unit_label,
                                                246, 170, BELOW_DV_UNIT_GAP_DRIVING);
    position_unit_below_value_centered_to_title(app->data.s4_accum_fe_val_label, app->data.s4_trip_fe_unit_label,
                                                AppConfig::CX - 210, 420, BELOW_DV_UNIT_GAP_DRIVING);
}

lv_obj_t *create_data_screen1(AppContext *app) {
    lv_obj_t *screen = make_data_screen_base(app, &app->data.screen1);

    const int row_stride = 82;
    const int name_y0 = 42;
    const int value_offset = 24;

    make_data_name_label(screen, "RPM", name_y0 + 0 * row_stride);
    app->data.s1_rpm_val_label = make_data_val_label(screen, "--", name_y0 + 0 * row_stride + value_offset);
    app->data.s1_rpm_unit_label = make_unit_label_at(app, screen, "RPM", 0, 0, 90);

    make_data_name_label(screen, "THROTTLE", name_y0 + 1 * row_stride);
    app->data.s1_tps_val_label = make_data_val_label(screen, "--", name_y0 + 1 * row_stride + value_offset);
    app->data.s1_tps_unit_label = make_unit_label_at(app, screen, "%", 0, 0, 60);

    make_data_name_label(screen, "COOLANT TEMP", name_y0 + 2 * row_stride);
    app->data.s1_coolant_val_label = make_data_val_label(screen, "--", name_y0 + 2 * row_stride + value_offset);
    app->data.s1_coolant_unit_label = make_unit_label_at(app, screen, "C", 0, 0, 60);

    make_data_name_label(screen, "SPEED", name_y0 + 3 * row_stride);
    app->data.s1_speed_val_label = make_data_val_label(screen, "--", name_y0 + 3 * row_stride + value_offset);
    app->data.s1_speed_unit_label = make_unit_label_at(app, screen, "km/h", 0, 0, 80);

    make_data_name_label(screen, "FUEL PRESSURE", name_y0 + 4 * row_stride);
    app->data.s1_fuelpsi_val_label = make_data_val_label(screen, "--", name_y0 + 4 * row_stride + value_offset);
    app->data.s1_fuelpress_unit_label = make_unit_label_at(app, screen, "kPa", 0, 0, 80);

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
    app->data.s2_batt_unit_label = make_unit_label_at(app, screen, "V", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "FUEL PRESSURE", left_x, name_y0 + 1 * row_stride + row_y_adjust[1], col_width);
    app->data.s2_fuelpress_val_label = make_data_val_label_lg_at(screen, "--", left_x + row_value_x_adjust[1], name_y0 + 1 * row_stride + value_offset + row_y_adjust[1], col_width);
    app->data.s2_fuelpress_unit_label = make_unit_label_at(app, screen, "kPa", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "ETHANOL", left_x, name_y0 + 2 * row_stride + row_y_adjust[2], col_width);
    app->data.s2_ethanol_val_label = make_data_val_label_lg_at(screen, "--", left_x + row_value_x_adjust[2], name_y0 + 2 * row_stride + value_offset + row_y_adjust[2], col_width);
    app->data.s2_ethanol_unit_label = make_unit_label_at(app, screen, "%", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "ECT", right_x, name_y0 + 0 * row_stride + row_y_adjust[0], col_width);
    app->data.s2_ect_val_label = make_data_val_label_lg_at(screen, "--", right_x, name_y0 + 0 * row_stride + value_offset + row_y_adjust[0], col_width);
    app->data.s2_ect_unit_label = make_unit_label_at(app, screen, "C", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "IAT", right_x, name_y0 + 1 * row_stride + row_y_adjust[1], col_width);
    app->data.s2_iat_val_label = make_data_val_label_lg_at(screen, "--", right_x, name_y0 + 1 * row_stride + value_offset + row_y_adjust[1], col_width);
    app->data.s2_iat_unit_label = make_unit_label_at(app, screen, "C", 0, 0, unit_w);

    make_data_name_label_lg_at(screen, "BARO", right_x, name_y0 + 2 * row_stride + row_y_adjust[2], col_width);
    app->data.s2_bap_val_label = make_data_val_label_lg_at(screen, "--", right_x + row_value_x_adjust[2], name_y0 + 2 * row_stride + value_offset + row_y_adjust[2], col_width);
    app->data.s2_baro_unit_label = make_unit_label_at(app, screen, "kPa", 0, 0, unit_w);

    app->data.timer2 = lv_timer_create(tick_data2, 200, app);
    tick_data2(app->data.timer2);
    return screen;
}

lv_obj_t *create_data_screen3(AppContext *app) {
    lv_obj_t *screen = make_data_screen_base(app, &app->data.screen3);

    const int title_w = 160;
    const int left_x = 54;
    const int right_x = 241;
    const int top_title_y = 92;
    const int bottom_title_y = 246;
    const int top_value_y = 120;
    const int bottom_value_y = 274;

    lv_obj_t *s3_rpm_title = make_data_name_label_lg_at(screen, "RPM", left_x + 6, top_title_y, title_w);
    lv_obj_set_style_text_font(s3_rpm_title, &lv_font_montserrat_24, 0);
    app->data.s3_rpm_val_label = make_data_val_label_lg_at(screen, "--", left_x - 12, top_value_y, title_w + 48);
    lv_obj_set_style_text_font(app->data.s3_rpm_val_label, &lv_font_montserrat_semibold_84, 0);
    lv_obj_set_style_text_color(app->data.s3_rpm_val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(app->data.s3_rpm_val_label, LV_OPA_COVER, 0);
    lv_label_set_long_mode(app->data.s3_rpm_val_label, LV_LABEL_LONG_CLIP);
    app->data.s3_rpm_unit_label = make_unit_label_at(app, screen, "RPM", 0, 0, 70);
    lv_obj_add_flag(app->data.s3_rpm_unit_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *s3_gear_title = make_data_name_label_lg_at(screen, "GEAR", right_x + 10, top_title_y, title_w);
    lv_obj_set_style_text_font(s3_gear_title, &lv_font_montserrat_24, 0);
    app->data.s3_gear_val_label = make_data_val_label_lg_at(screen, "--", right_x + 4, top_value_y, title_w + 16);
    lv_obj_set_style_text_font(app->data.s3_gear_val_label, &lv_font_montserrat_semibold_84, 0);
    lv_obj_set_style_text_color(app->data.s3_gear_val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(app->data.s3_gear_val_label, LV_OPA_COVER, 0);

    lv_obj_t *s3_tps_title = make_data_name_label_lg_at(screen, "THROTTLE", left_x + 2, bottom_title_y, title_w + 28);
    lv_obj_set_style_text_font(s3_tps_title, &lv_font_montserrat_24, 0);
    app->data.s3_tps_val_label = make_data_val_label_lg_at(screen, "--", left_x - 8, bottom_value_y, title_w + 30);
    lv_obj_set_style_text_font(app->data.s3_tps_val_label, &lv_font_montserrat_semibold_84, 0);
    lv_obj_set_style_text_color(app->data.s3_tps_val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(app->data.s3_tps_val_label, LV_OPA_COVER, 0);
    app->data.s3_tps_unit_label = make_unit_label_at(app, screen, "%", 0, 0, 36);

    lv_obj_t *s3_speed_title = make_data_name_label_lg_at(screen, "SPEED", right_x, bottom_title_y, title_w);
    lv_obj_set_style_text_font(s3_speed_title, &lv_font_montserrat_24, 0);
    app->data.s3_speed_val_label = make_data_val_label_lg_at(screen, "--", right_x - 12, bottom_value_y, title_w + 36);
    lv_obj_set_style_text_font(app->data.s3_speed_val_label, &lv_font_montserrat_semibold_84, 0);
    lv_obj_set_style_text_color(app->data.s3_speed_val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(app->data.s3_speed_val_label, LV_OPA_COVER, 0);
    app->data.s3_speed_unit_label = make_unit_label_at(app, screen, "MPH", 0, 0, 84);

    app->data.timer3 = lv_timer_create(tick_data3, DATA_DRIVING_UPDATE_MS, app);
    tick_data3(app->data.timer3);
    return screen;
}

lv_obj_t *create_data_screen4(AppContext *app) {
    lv_obj_t *screen = make_data_screen_base(app, &app->data.screen4);

    const int title_w = 170;
    const int top_title_y = 90;
    const int top_val_y = 110;
    const int bottom_value_y = 335;
    const int avg_title_y = bottom_value_y - 20;

    lv_obj_t *s4_ambient_title = make_data_name_label_lg_at(screen, "AMB. TEMP", 56, top_title_y, title_w);
    lv_obj_set_style_text_font(s4_ambient_title, &lv_font_montserrat_24, 0);
    app->data.s4_ambient_val_label = make_data_val_label_lg_at(screen, "--", 54, top_val_y, title_w);
    lv_obj_set_style_text_font(app->data.s4_ambient_val_label, &lv_font_montserrat_semibold_84, 0);
    lv_obj_set_style_text_color(app->data.s4_ambient_val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(app->data.s4_ambient_val_label, LV_OPA_COVER, 0);
    app->data.s4_ambient_unit_label = make_unit_label_at(app, screen, "oF", 0, 0, 70);
    lv_obj_set_style_text_font(app->data.s4_ambient_unit_label, &lv_font_montserrat_18, 0);
    lv_obj_add_flag(app->data.s4_ambient_unit_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *s4_trip_title = make_data_name_label_lg_at(screen, "TRIP", 246, top_title_y, title_w);
    lv_obj_set_style_text_font(s4_trip_title, &lv_font_montserrat_24, 0);
    app->data.s4_distance_val_label = make_data_val_label_lg_at(screen, "--.-", 244, top_val_y, title_w);
    lv_obj_set_style_text_font(app->data.s4_distance_val_label, &lv_font_montserrat_semibold_84, 0);
    lv_obj_set_style_text_color(app->data.s4_distance_val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(app->data.s4_distance_val_label, LV_OPA_COVER, 0);
    app->data.s4_distance_unit_label = make_unit_label_at(app, screen, "MILES", 0, 0, 120);
    lv_obj_set_style_text_font(app->data.s4_distance_unit_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(app->data.s4_distance_unit_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *bar_bg = lv_obj_create(screen);
    lv_obj_remove_style_all(bar_bg);
    lv_obj_set_size(bar_bg, FE_BAR_W, FE_BAR_H);
    lv_obj_set_pos(bar_bg, FE_BAR_X, FE_BAR_Y);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x1f1f1f), 0);
    lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_bg, 2, 0);
    lv_obj_set_style_border_color(bar_bg, lv_color_hex(0x303030), 0);
    lv_obj_set_style_radius(bar_bg, 0, 0);

    lv_obj_t *bar = lv_bar_create(screen);
    app->data.s4_inst_fe_arc = bar;
    lv_obj_set_size(bar, FE_BAR_W, FE_BAR_H);
    lv_obj_set_pos(bar, FE_BAR_X, FE_BAR_Y);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, app_bar_gauge_color2(app), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, app_bar_gauge_color1(app), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    for (int tick = 1; tick < 4; tick++) {
        lv_obj_t *mark = lv_obj_create(screen);
        lv_obj_remove_style_all(mark);
        lv_obj_set_size(mark, 2, 8);
        lv_obj_set_pos(mark, FE_BAR_X + (FE_BAR_W * tick) / 4 - 1, FE_BAR_Y + FE_BAR_H - 2);
        lv_obj_set_style_bg_color(mark, app_primary_text_color(app), 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    }

    lv_obj_t *bar_min = lv_label_create(screen);
    lv_obj_set_style_text_color(bar_min, app_primary_text_color(app), 0);
    lv_obj_set_style_text_font(bar_min, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(bar_min, FE_BAR_X - 4, FE_BAR_Y + FE_BAR_H + 4);
    lv_label_set_text(bar_min, "0 MPG");

    lv_obj_t *bar_mid = lv_label_create(screen);
    lv_obj_set_style_text_color(bar_mid, app_primary_text_color(app), 0);
    lv_obj_set_style_text_font(bar_mid, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(bar_mid, FE_BAR_X + FE_BAR_W / 2 - 10, FE_BAR_Y + FE_BAR_H + 4);
    lv_label_set_text(bar_mid, "20");

    lv_obj_t *bar_max = lv_label_create(screen);
    lv_obj_set_style_text_color(bar_max, app_primary_text_color(app), 0);
    lv_obj_set_style_text_font(bar_max, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(bar_max, FE_BAR_X + FE_BAR_W - 16, FE_BAR_Y + FE_BAR_H + 4);
    lv_label_set_text(bar_max, "40");

    app->data.s4_trip_fe_marker = lv_obj_create(screen);
    lv_obj_remove_style_all(app->data.s4_trip_fe_marker);
    lv_obj_set_size(app->data.s4_trip_fe_marker, FE_MARKER_W, FE_MARKER_H);
    lv_obj_add_event_cb(app->data.s4_trip_fe_marker, draw_down_triangle_marker_event, LV_EVENT_DRAW_MAIN, app);
    lv_obj_add_flag(app->data.s4_trip_fe_marker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *s4_avg_title = make_data_name_label_lg(screen, "AVG. FUEL ECONOMY", avg_title_y);
    lv_obj_set_style_text_font(s4_avg_title, &lv_font_montserrat_24, 0);
    app->data.s4_accum_fe_val_label = make_data_val_label_lg_at(screen, "--.-", AppConfig::CX - 210 - 6, bottom_value_y, 420);
    lv_obj_set_style_text_font(app->data.s4_accum_fe_val_label, &lv_font_montserrat_semibold_84, 0);
    lv_obj_set_style_text_color(app->data.s4_accum_fe_val_label, app_primary_text_color(app), 0);
    lv_obj_set_style_text_opa(app->data.s4_accum_fe_val_label, LV_OPA_COVER, 0);
    app->data.s4_trip_fe_unit_label = make_unit_label_at(app, screen, "mpg", 0, 0, 70);
    lv_obj_set_style_text_font(app->data.s4_trip_fe_unit_label, &lv_font_montserrat_18, 0);

    app->data.s4_inst_fe_val_label = lv_label_create(screen);
    lv_obj_add_flag(app->data.s4_inst_fe_val_label, LV_OBJ_FLAG_HIDDEN);
    app->data.s4_inst_fe_unit_label = lv_label_create(screen);
    lv_obj_add_flag(app->data.s4_inst_fe_unit_label, LV_OBJ_FLAG_HIDDEN);

    app->data.timer4 = lv_timer_create(tick_data4, 100, app);
    tick_data4(app->data.timer4);
    return screen;
}