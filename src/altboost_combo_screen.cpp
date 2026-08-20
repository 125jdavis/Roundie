#include "altboost_combo_screen.h"
#include "can_bus.h"

#include <math.h>
#include <string.h>

namespace {

static constexpr uint32_t ALT_FAST_TIMEOUT_MS = 1200;
static constexpr uint32_t ALT_TARGET_TIMEOUT_MS = 2500;
static constexpr uint32_t ALT_VALUE_REFRESH_MS = 200;
static constexpr float ALT_LAMBDA_MIN = 0.70f;
static constexpr float ALT_LAMBDA_MAX = 1.30f;
static constexpr int ALT_ARC_RANGE_MAX = 1000;
static constexpr int ALT_MARKER_SIZE = 30;
static constexpr float ALT_MARKER_RADIUS = 206.0f;
static constexpr float ALT_GHOST_TRIGGER_PSI = 5.0f;
static constexpr uint32_t ALT_GHOST_HOLD_MS = 3000;
static constexpr float ALT_GHOST_INNER_R = 176.0f;
static constexpr float ALT_GHOST_OUTER_R = 220.0f;
static constexpr float ALT_TOP_ARC_START_DEG = 210.0f;
static constexpr float ALT_TOP_ARC_END_DEG = 330.0f;
static constexpr float ALT_BOTTOM_ARC_START_DEG = 30.0f;
static constexpr float ALT_BOTTOM_ARC_END_DEG = 150.0f;

static float clamp_boost(float psi) {
    return clampf(psi, AppConfig::BOOST_MIN, AppConfig::BOOST_MAX);
}

static float clamp_lambda(float lambda) {
    return clampf(lambda, ALT_LAMBDA_MIN, ALT_LAMBDA_MAX);
}

static float boost_to_t(float psi) {
    return (clamp_boost(psi) - AppConfig::BOOST_MIN) / (AppConfig::BOOST_MAX - AppConfig::BOOST_MIN);
}

static float lambda_to_t(float lambda) {
    return (clamp_lambda(lambda) - ALT_LAMBDA_MIN) / (ALT_LAMBDA_MAX - ALT_LAMBDA_MIN);
}

static float boost_to_angle_deg(float psi) {
    return arc_angle_for_t(ALT_TOP_ARC_START_DEG, ALT_TOP_ARC_END_DEG, boost_to_t(psi));
}

static float lambda_to_angle_deg(float lambda) {
    return arc_angle_for_t(ALT_BOTTOM_ARC_START_DEG, ALT_BOTTOM_ARC_END_DEG, lambda_to_t(lambda));
}

static float triangle_wave_01(float phase) {
    phase = phase - floorf(phase);
    if (phase < 0.5f) return phase * 2.0f;
    return (1.0f - phase) * 2.0f;
}

static void get_demo_values(AppContext *app,
                            uint32_t now,
                            float *boost_psi,
                            float *lambda_actual,
                            float *boost_target_psi,
                            float *lambda_target) {
    float p1 = (float)((now - app->alt_boost_combo.demo_start_ms) % 5200U) / 5200.0f;
    float p2 = (float)((now - app->alt_boost_combo.demo_start_ms) % 3700U) / 3700.0f;
    float p3 = (float)((now - app->alt_boost_combo.demo_start_ms) % 6100U) / 6100.0f;

    float boost_t = triangle_wave_01(p1);
    float target_t = triangle_wave_01(p1 + 0.18f);
    float lambda_t = triangle_wave_01(p2 + 0.33f);
    float lambda_target_t = triangle_wave_01(p3 + 0.47f);

    *boost_psi = AppConfig::BOOST_MIN + boost_t * (AppConfig::BOOST_MAX - AppConfig::BOOST_MIN);
    *boost_target_psi = AppConfig::BOOST_MIN + target_t * (AppConfig::BOOST_MAX - AppConfig::BOOST_MIN);
    *lambda_actual = ALT_LAMBDA_MIN + lambda_t * (ALT_LAMBDA_MAX - ALT_LAMBDA_MIN);
    *lambda_target = ALT_LAMBDA_MIN + lambda_target_t * (ALT_LAMBDA_MAX - ALT_LAMBDA_MIN);
}

static void draw_triangle_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;

    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    float cx = (float)(coords.x1 + coords.x2) * 0.5f;
    float cy = (float)(coords.y1 + coords.y2) * 0.5f;
    float vx = (float)AppConfig::CX - cx;
    float vy = (float)AppConfig::CY - cy;
    float len = sqrtf(vx * vx + vy * vy);
    if (len < 1.0f) return;

    vx /= len;
    vy /= len;

    float nx = -vy;
    float ny = vx;

    const float tip_r = 13.0f;
    const float back_r = 10.0f;
    const float half_base = 9.0f;

    float tip_x = cx + vx * tip_r;
    float tip_y = cy + vy * tip_r;
    float base_cx = cx - vx * back_r;
    float base_cy = cy - vy * back_r;

    lv_point_t tri[3] = {
        {(lv_coord_t)lroundf(tip_x), (lv_coord_t)lroundf(tip_y)},
        {(lv_coord_t)lroundf(base_cx + nx * half_base), (lv_coord_t)lroundf(base_cy + ny * half_base)},
        {(lv_coord_t)lroundf(base_cx - nx * half_base), (lv_coord_t)lroundf(base_cy - ny * half_base)}
    };

    lv_draw_rect_dsc_t fill_dsc;
    lv_draw_rect_dsc_init(&fill_dsc);
    fill_dsc.bg_color = app_primary_text_color(app);
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

static void draw_ghost_line_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app || !app->alt_boost_combo.ghost_visible) return;

    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);

    float angle_rad = boost_to_angle_deg(app->alt_boost_combo.ghost_psi) * (float)M_PI / 180.0f;
    int x1 = (int)lroundf((float)AppConfig::CX + cosf(angle_rad) * ALT_GHOST_INNER_R);
    int y1 = (int)lroundf((float)AppConfig::CY + sinf(angle_rad) * ALT_GHOST_INNER_R);
    int x2 = (int)lroundf((float)AppConfig::CX + cosf(angle_rad) * ALT_GHOST_OUTER_R);
    int y2 = (int)lroundf((float)AppConfig::CY + sinf(angle_rad) * ALT_GHOST_OUTER_R);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = app_primary_text_color(app);
    dsc.width = 4;
    dsc.opa = LV_OPA_COVER;

    lv_point_t points[] = {
        {(lv_coord_t)x1, (lv_coord_t)y1},
        {(lv_coord_t)x2, (lv_coord_t)y2}
    };
    lv_draw_line(draw_ctx, &dsc, &points[0], &points[1]);
}

static void set_marker_hidden(lv_obj_t *obj, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void move_marker_to_angle(lv_obj_t *obj, float angle_deg) {
    float angle_rad = angle_deg * (float)M_PI / 180.0f;
    int x = (int)lroundf((float)AppConfig::CX + cosf(angle_rad) * ALT_MARKER_RADIUS) - ALT_MARKER_SIZE / 2;
    int y = (int)lroundf((float)AppConfig::CY + sinf(angle_rad) * ALT_MARKER_RADIUS) - ALT_MARKER_SIZE / 2;
    lv_obj_set_pos(obj, (lv_coord_t)x, (lv_coord_t)y);
}

static void position_boost_unit_label(lv_obj_t *value_label, lv_obj_t *unit_label) {
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

    lv_coord_t unit_x = value_x + value_w / 2 + text_w / 2 + 6;
    lv_coord_t unit_y = value_y + value_line_h - unit_line_h + 2;
    lv_obj_set_pos(unit_label, unit_x, unit_y);
}

static void update_ghost_state(AppContext *app, float boost_psi_actual, uint32_t now) {
    if (!app->alt_boost_combo.ghost_visible) {
        if (boost_psi_actual >= ALT_GHOST_TRIGGER_PSI) {
            app->alt_boost_combo.ghost_visible = true;
            app->alt_boost_combo.ghost_psi = boost_psi_actual;
            app->alt_boost_combo.ghost_hold_until_ms = now + ALT_GHOST_HOLD_MS;
        }
        return;
    }

    if (boost_psi_actual > app->alt_boost_combo.ghost_psi) {
        app->alt_boost_combo.ghost_psi = boost_psi_actual;
        app->alt_boost_combo.ghost_hold_until_ms = now + ALT_GHOST_HOLD_MS;
        return;
    }

    if (now < app->alt_boost_combo.ghost_hold_until_ms) return;

    if (boost_psi_actual >= ALT_GHOST_TRIGGER_PSI) {
        app->alt_boost_combo.ghost_psi = boost_psi_actual;
        app->alt_boost_combo.ghost_hold_until_ms = now + ALT_GHOST_HOLD_MS;
    } else {
        app->alt_boost_combo.ghost_visible = false;
    }
}

static void set_arc_visual_style(AppContext *app, lv_obj_t *arc) {
    lv_obj_set_style_arc_width(arc, 34, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 34, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, app_bar_gauge_color2(app), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, app_bar_gauge_color1(app), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

static void tick_altboost_combo(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    static uint32_t last_value_refresh_ms = 0;

    float boost_psi_actual = 0.0f;
    float lambda_actual = 1.0f;
    float target_boost_psi = 0.0f;
    float target_lambda = 1.0f;

    if (app->alt_boost_combo.demo_mode) {
        get_demo_values(app, now, &boost_psi_actual, &lambda_actual, &target_boost_psi, &target_lambda);
    } else {
        boost_psi_actual = clamp_boost(app->can.boost_psi);

        if (can_is_recent(now, app->can.ht.last_0x368_ms, ALT_FAST_TIMEOUT_MS)) {
            lambda_actual = app->can.ht.lambda1;
        }

        if (can_is_recent(now, app->can.ht.last_0x372_ms, ALT_TARGET_TIMEOUT_MS)) {
            target_boost_psi = clamp_boost(app->can.ht.target_boost_kpa * AppConfig::KPA_TO_PSI);
        } else {
            target_boost_psi = boost_psi_actual;
        }

        if (can_is_recent(now, app->can.ht.last_0x3E9_ms, ALT_TARGET_TIMEOUT_MS) && app->can.ht.target_lambda > 0.0f) {
            target_lambda = clamp_lambda(app->can.ht.target_lambda);
        } else {
            target_lambda = lambda_actual;
        }
    }

    lambda_actual = clamp_lambda(lambda_actual);

    uint32_t dt_ms = (app->alt_boost_combo.visual_last_ms > 0 && now > app->alt_boost_combo.visual_last_ms)
        ? (now - app->alt_boost_combo.visual_last_ms)
        : AppConfig::BOOSTAFR_ARC_TICK_MS;
    app->alt_boost_combo.visual_last_ms = now;

    if (!app->alt_boost_combo.visual_initialized) {
        app->alt_boost_combo.boost_visual_psi = boost_psi_actual;
        app->alt_boost_combo.lambda_visual = lambda_actual;
        app->alt_boost_combo.visual_initialized = true;
    } else {
        float boost_alpha = (float)dt_ms / (56.0f + (float)dt_ms);
        float lambda_alpha = (float)dt_ms / (78.0f + (float)dt_ms);
        boost_alpha = clampf(boost_alpha, 0.08f, 0.78f);
        lambda_alpha = clampf(lambda_alpha, 0.06f, 0.72f);

        app->alt_boost_combo.boost_visual_psi += (boost_psi_actual - app->alt_boost_combo.boost_visual_psi) * boost_alpha;
        app->alt_boost_combo.lambda_visual += (lambda_actual - app->alt_boost_combo.lambda_visual) * lambda_alpha;
    }

    int boost_arc_value = (int)lroundf(boost_to_t(app->alt_boost_combo.boost_visual_psi) * (float)ALT_ARC_RANGE_MAX);
    int lambda_arc_value = (int)lroundf(lambda_to_t(app->alt_boost_combo.lambda_visual) * (float)ALT_ARC_RANGE_MAX);
    boost_arc_value = (int)clampf((float)boost_arc_value, 0.0f, (float)ALT_ARC_RANGE_MAX);
    lambda_arc_value = (int)clampf((float)lambda_arc_value, 0.0f, (float)ALT_ARC_RANGE_MAX);

    lv_arc_set_value(app->alt_boost_combo.boost_arc, boost_arc_value);
    lv_arc_set_value(app->alt_boost_combo.lambda_arc, lambda_arc_value);

    move_marker_to_angle(app->alt_boost_combo.boost_target_marker, boost_to_angle_deg(target_boost_psi));
    set_marker_hidden(app->alt_boost_combo.boost_target_marker, false);
    lv_obj_invalidate(app->alt_boost_combo.boost_target_marker);

    move_marker_to_angle(app->alt_boost_combo.lambda_target_marker, lambda_to_angle_deg(target_lambda));
    set_marker_hidden(app->alt_boost_combo.lambda_target_marker, false);
    lv_obj_invalidate(app->alt_boost_combo.lambda_target_marker);

    update_ghost_state(app, boost_psi_actual, now);
    set_marker_hidden(app->alt_boost_combo.ghost_line_obj, !app->alt_boost_combo.ghost_visible);
    if (app->alt_boost_combo.ghost_visible) {
        lv_obj_invalidate(app->alt_boost_combo.ghost_line_obj);
    }

    bool refresh_values = (last_value_refresh_ms == 0) || ((now - last_value_refresh_ms) >= ALT_VALUE_REFRESH_MS);
    if (refresh_values) {
        last_value_refresh_ms = now;

        char buf[24];
        if (boost_psi_actual < 0.0f) {
            float inhg = clampf(-boost_psi_actual * AppConfig::ALT_BOOST_INHG_PER_PSI, 0.0f, AppConfig::ALT_BOOST_INHG_MAX);
            snprintf(buf, sizeof(buf), "%.1f", inhg);
            lv_label_set_text(app->alt_boost_combo.boost_value_label, buf);
            lv_label_set_text(app->alt_boost_combo.boost_unit_label, "In. Hg");
        } else {
            snprintf(buf, sizeof(buf), "%.1f", boost_psi_actual);
            lv_label_set_text(app->alt_boost_combo.boost_value_label, buf);
            lv_label_set_text(app->alt_boost_combo.boost_unit_label, "PSI");
        }

        position_boost_unit_label(app->alt_boost_combo.boost_value_label, app->alt_boost_combo.boost_unit_label);

        snprintf(buf, sizeof(buf), "%.2f", lambda_actual);
        lv_label_set_text(app->alt_boost_combo.lambda_value_label, buf);
    }
}

}  // namespace

void altboost_combo_toggle_demo(AppContext *app, uint32_t now_ms) {
    app->alt_boost_combo.demo_mode = !app->alt_boost_combo.demo_mode;
    if (app->alt_boost_combo.demo_mode) {
        app->alt_boost_combo.demo_start_ms = now_ms;
    }
}

lv_obj_t *create_altboost_combo_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->alt_boost_combo.screen = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    app->alt_boost_combo.boost_arc = lv_arc_create(screen);
    lv_obj_set_size(app->alt_boost_combo.boost_arc, 420, 420);
    lv_obj_center(app->alt_boost_combo.boost_arc);
    lv_arc_set_rotation(app->alt_boost_combo.boost_arc, 0);
    lv_arc_set_bg_angles(app->alt_boost_combo.boost_arc, (int)ALT_TOP_ARC_START_DEG, (int)ALT_TOP_ARC_END_DEG);
    lv_arc_set_range(app->alt_boost_combo.boost_arc, 0, ALT_ARC_RANGE_MAX);
    lv_arc_set_value(app->alt_boost_combo.boost_arc, 0);
    set_arc_visual_style(app, app->alt_boost_combo.boost_arc);

    app->alt_boost_combo.lambda_arc = lv_arc_create(screen);
    lv_obj_set_size(app->alt_boost_combo.lambda_arc, 420, 420);
    lv_obj_center(app->alt_boost_combo.lambda_arc);
    lv_arc_set_rotation(app->alt_boost_combo.lambda_arc, 0);
    lv_arc_set_bg_angles(app->alt_boost_combo.lambda_arc, (int)ALT_BOTTOM_ARC_START_DEG, (int)ALT_BOTTOM_ARC_END_DEG);
    lv_arc_set_range(app->alt_boost_combo.lambda_arc, 0, ALT_ARC_RANGE_MAX);
    lv_arc_set_value(app->alt_boost_combo.lambda_arc, 500);
    set_arc_visual_style(app, app->alt_boost_combo.lambda_arc);

    app->alt_boost_combo.boost_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->alt_boost_combo.boost_value_label, app_primary_text_color(app), 0);
    lv_obj_set_style_text_font(app->alt_boost_combo.boost_value_label, &lv_font_montserrat_semibold_72, 0);
    lv_obj_set_width(app->alt_boost_combo.boost_value_label, 260);
    lv_obj_set_style_text_align(app->alt_boost_combo.boost_value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->alt_boost_combo.boost_value_label, AppConfig::CX - 130, 118);
    lv_label_set_text(app->alt_boost_combo.boost_value_label, "0.0");

    app->alt_boost_combo.boost_unit_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->alt_boost_combo.boost_unit_label, lv_color_hex(0xBFC7CF), 0);
    lv_obj_set_style_text_font(app->alt_boost_combo.boost_unit_label, &lv_font_montserrat_32, 0);
    lv_label_set_text(app->alt_boost_combo.boost_unit_label, "PSI");

    app->alt_boost_combo.boost_title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->alt_boost_combo.boost_title_label, app_secondary_text_color(app), 0);
    lv_obj_set_style_text_font(app->alt_boost_combo.boost_title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(app->alt_boost_combo.boost_title_label, 320);
    lv_obj_set_style_text_align(app->alt_boost_combo.boost_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->alt_boost_combo.boost_title_label, AppConfig::CX - 160, 194);
    lv_label_set_text(app->alt_boost_combo.boost_title_label, "BOOST");

    app->alt_boost_combo.lambda_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->alt_boost_combo.lambda_value_label, app_primary_text_color(app), 0);
    lv_obj_set_style_text_font(app->alt_boost_combo.lambda_value_label, &lv_font_montserrat_semibold_72, 0);
    lv_obj_set_width(app->alt_boost_combo.lambda_value_label, 320);
    lv_obj_set_style_text_align(app->alt_boost_combo.lambda_value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->alt_boost_combo.lambda_value_label, AppConfig::CX - 160, 258);
    lv_label_set_text(app->alt_boost_combo.lambda_value_label, "1.000");

    app->alt_boost_combo.lambda_title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->alt_boost_combo.lambda_title_label, app_secondary_text_color(app), 0);
    lv_obj_set_style_text_font(app->alt_boost_combo.lambda_title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(app->alt_boost_combo.lambda_title_label, 320);
    lv_obj_set_style_text_align(app->alt_boost_combo.lambda_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->alt_boost_combo.lambda_title_label, AppConfig::CX - 160, 332);
    lv_label_set_text(app->alt_boost_combo.lambda_title_label, "LAMBDA");

    app->alt_boost_combo.boost_target_marker = lv_obj_create(screen);
    lv_obj_remove_style_all(app->alt_boost_combo.boost_target_marker);
    lv_obj_set_size(app->alt_boost_combo.boost_target_marker, ALT_MARKER_SIZE, ALT_MARKER_SIZE);
    lv_obj_add_event_cb(app->alt_boost_combo.boost_target_marker, draw_triangle_event, LV_EVENT_DRAW_MAIN, app);
    lv_obj_clear_flag(app->alt_boost_combo.boost_target_marker, LV_OBJ_FLAG_CLICKABLE);

    app->alt_boost_combo.lambda_target_marker = lv_obj_create(screen);
    lv_obj_remove_style_all(app->alt_boost_combo.lambda_target_marker);
    lv_obj_set_size(app->alt_boost_combo.lambda_target_marker, ALT_MARKER_SIZE, ALT_MARKER_SIZE);
    lv_obj_add_event_cb(app->alt_boost_combo.lambda_target_marker, draw_triangle_event, LV_EVENT_DRAW_MAIN, app);
    lv_obj_clear_flag(app->alt_boost_combo.lambda_target_marker, LV_OBJ_FLAG_CLICKABLE);

    app->alt_boost_combo.ghost_line_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(app->alt_boost_combo.ghost_line_obj);
    lv_obj_set_size(app->alt_boost_combo.ghost_line_obj, AppConfig::LCD_WIDTH, AppConfig::LCD_HEIGHT);
    lv_obj_set_pos(app->alt_boost_combo.ghost_line_obj, 0, 0);
    lv_obj_add_event_cb(app->alt_boost_combo.ghost_line_obj, draw_ghost_line_event, LV_EVENT_DRAW_MAIN, app);
    lv_obj_clear_flag(app->alt_boost_combo.ghost_line_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(app->alt_boost_combo.ghost_line_obj, LV_OBJ_FLAG_HIDDEN);

    uint32_t now = lv_tick_get();
    app->alt_boost_combo.visual_last_ms = now;
    app->alt_boost_combo.boost_visual_psi = clamp_boost(app->can.boost_psi);
    app->alt_boost_combo.lambda_visual = 1.0f;
    app->alt_boost_combo.visual_initialized = false;
    app->alt_boost_combo.ghost_visible = false;
    app->alt_boost_combo.ghost_psi = 0.0f;
    app->alt_boost_combo.ghost_hold_until_ms = 0;

    move_marker_to_angle(app->alt_boost_combo.boost_target_marker, boost_to_angle_deg(0.0f));
    move_marker_to_angle(app->alt_boost_combo.lambda_target_marker, lambda_to_angle_deg(1.0f));
    set_marker_hidden(app->alt_boost_combo.boost_target_marker, false);
    set_marker_hidden(app->alt_boost_combo.lambda_target_marker, false);

    app->alt_boost_combo.timer = lv_timer_create(tick_altboost_combo, AppConfig::BOOSTAFR_ARC_TICK_MS, app);
    tick_altboost_combo(app->alt_boost_combo.timer);

    lv_obj_move_foreground(app->alt_boost_combo.boost_target_marker);
    lv_obj_move_foreground(app->alt_boost_combo.lambda_target_marker);
    lv_obj_move_foreground(app->alt_boost_combo.ghost_line_obj);

    return screen;
}
