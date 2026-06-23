#include "boostafr_screen.h"

#include <limits.h>
#include <math.h>

static float triangle_wave_01(float phase) {
    phase = phase - floorf(phase);
    if (phase < 0.5f) return phase * 2.0f;
    return (1.0f - phase) * 2.0f;
}

static void get_demo_values(AppContext *app, uint32_t now, float *map_psi, float *afr_actual) {
    float boost_phase = (float)((now - app->boostafr.demo_start_ms) % AppConfig::BOOSTAFR_DEMO_BOOST_PERIOD_MS) /
                        (float)AppConfig::BOOSTAFR_DEMO_BOOST_PERIOD_MS;
    float afr_phase = (float)((now - app->boostafr.demo_start_ms) % AppConfig::BOOSTAFR_DEMO_AFR_PERIOD_MS) /
                      (float)AppConfig::BOOSTAFR_DEMO_AFR_PERIOD_MS;

    float boost_t = triangle_wave_01(boost_phase);
    float afr_t = triangle_wave_01(afr_phase + 0.25f);

    *map_psi = AppConfig::BOOST_MIN + boost_t * (AppConfig::BOOST_MAX - AppConfig::BOOST_MIN);
    *afr_actual = AppConfig::AFR_MIN + afr_t * (AppConfig::AFR_MAX - AppConfig::AFR_MIN);
}

void boostafr_toggle_demo(AppContext *app, uint32_t now_ms) {
    app->boostafr.demo_mode = !app->boostafr.demo_mode;
    if (app->boostafr.demo_mode) {
        app->boostafr.demo_start_ms = now_ms;
    }
}

static void draw_triangle_marker_event(lv_event_t *event) {
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_coord_t w = lv_area_get_width(&coords);
    lv_coord_t h = lv_area_get_height(&coords);

    lv_point_t tri[3] = {
        {(lv_coord_t)(coords.x1 + w / 2), (lv_coord_t)coords.y1},
        {(lv_coord_t)coords.x1, (lv_coord_t)coords.y2},
        {(lv_coord_t)coords.x2, (lv_coord_t)coords.y2}
    };

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0xFFFFFF);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;

    lv_draw_polygon(draw_ctx, &dsc, tri, 3);
}

static void tick_boostafr_arcs(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    static int last_map_arc_value = -1;
    static int last_afr_arc_value = -1;
    static bool afr_marker_hidden = false;
    static bool boost_marker_hidden = true;
    static int last_afr_marker_x = INT32_MIN;
    static int last_afr_marker_y = INT32_MIN;
    static int last_boost_marker_x = INT32_MIN;
    static int last_boost_marker_y = INT32_MIN;
    static uint8_t boost_color_mode = 0xFF;
    static uint8_t afr_color_mode = 0xFF;

    auto set_boost_color_mode = [&](uint8_t mode) {
        if (mode == boost_color_mode) return;
        boost_color_mode = mode;
        lv_color_t arc_color = (mode == 1) ? lv_color_hex(0xFF5A00) : lv_color_hex(0x10b8ff);
        lv_color_t text_color = (mode == 1) ? lv_color_hex(0xFF5A00) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_arc_color(app->boostafr.map_arc, arc_color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(app->boostafr.psi_label, text_color, 0);
        lv_obj_set_style_text_color(app->boostafr.psi_title_label, text_color, 0);
    };

    auto set_afr_color_mode = [&](uint8_t mode) {
        if (mode == afr_color_mode) return;
        afr_color_mode = mode;
        lv_color_t arc_color = (mode == 1) ? lv_color_hex(0xFF5A00) : lv_color_hex(0x20c870);
        lv_color_t text_color = (mode == 1) ? lv_color_hex(0xFF5A00) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_arc_color(app->boostafr.afr_arc, arc_color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(app->boostafr.afr_label, text_color, 0);
        lv_obj_set_style_text_color(app->boostafr.afr_title_label, text_color, 0);
    };

    auto set_marker_hidden = [](lv_obj_t *marker, bool hidden, bool *cached_hidden) {
        if (*cached_hidden == hidden) return;
        *cached_hidden = hidden;
        if (hidden) {
            lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        }
    };

    auto set_marker_pos_if_changed = [](lv_obj_t *marker, int x, int y, int *last_x, int *last_y) {
        if (x == *last_x && y == *last_y) return;
        *last_x = x;
        *last_y = y;
        lv_obj_set_pos(marker, (lv_coord_t)x, (lv_coord_t)y);
    };

    auto set_arc_value_if_changed = [](lv_obj_t *arc, int value, int *last_value) {
        if (value == *last_value) return;
        *last_value = value;
        lv_arc_set_value(arc, value);
    };

    float map_psi_target = 0.0f;
    float afr_actual_target = AppConfig::STOICH_AFR;

    if (app->boostafr.demo_mode) {
        get_demo_values(app, now, &map_psi_target, &afr_actual_target);
        set_marker_hidden(app->boostafr.afr_target_marker, true, &afr_marker_hidden);
        set_marker_hidden(app->boostafr.boost_target_marker, true, &boost_marker_hidden);
        set_boost_color_mode(0);
        set_afr_color_mode(0);
        app->boostafr.boost_over_target_start_ms = 0;
        app->boostafr.lambda_lean_start_ms = 0;
        app->boostafr.boost_over_target_alert = false;
        app->boostafr.lambda_lean_alert = false;
    } else {
        HaltechData &ht = app->can.ht;
        set_marker_hidden(app->boostafr.afr_target_marker, false, &afr_marker_hidden);

        if (ht.last_0x360_ms > 0 && (now - ht.last_0x360_ms) < 1200) {
            map_psi_target = (ht.map_kpa_abs - AppConfig::ATMOSPHERIC_KPA) * AppConfig::KPA_TO_PSI;
        }
        map_psi_target = clampf(map_psi_target, AppConfig::BOOST_MIN, AppConfig::BOOST_MAX);
        float boost_kpa_gauge = map_psi_target / AppConfig::KPA_TO_PSI;

        float lambda_actual = 1.0f;
        if (ht.last_0x368_ms > 0 && (now - ht.last_0x368_ms) < 1200) {
            lambda_actual = ht.lambda1;
        }

        float lambda_target = 1.0f;
        if (ht.last_0x3E9_ms > 0 && (now - ht.last_0x3E9_ms) < 2500 && ht.target_lambda > 0.0f) {
            lambda_target = ht.target_lambda;
        }

        afr_actual_target = clampf(lambda_actual * AppConfig::STOICH_AFR, AppConfig::AFR_MIN, AppConfig::AFR_MAX);
        float afr_target = clampf(lambda_target * AppConfig::STOICH_AFR, AppConfig::AFR_MIN, AppConfig::AFR_MAX);

        bool rpm_active = ht.last_0x360_ms > 0 && (now - ht.last_0x360_ms) < 1200 && ht.rpm >= 1000;

        float t = (afr_target - AppConfig::AFR_MIN) / (AppConfig::AFR_MAX - AppConfig::AFR_MIN);
        float angle_deg = arc_angle_for_t(AppConfig::AFR_ARC_START_DEG, AppConfig::AFR_ARC_END_DEG, t);
        float angle_rad = angle_deg * (float)M_PI / 180.0f;
        float radius = 206.0f;
        int afr_marker_x = (int)lroundf((float)AppConfig::CX + cosf(angle_rad) * radius - 12.0f);
        int afr_marker_y = (int)lroundf((float)AppConfig::CY + sinf(angle_rad) * radius - 12.0f);
        set_marker_pos_if_changed(app->boostafr.afr_target_marker, afr_marker_x, afr_marker_y,
                                  &last_afr_marker_x, &last_afr_marker_y);

        bool target_boost_valid = ht.last_0x372_ms > 0 && (now - ht.last_0x372_ms) < 2500;
        if (target_boost_valid) {
            float target_psi = clampf(ht.target_boost_kpa * AppConfig::KPA_TO_PSI, AppConfig::BOOST_MIN, AppConfig::BOOST_MAX);
            float bt = (target_psi - AppConfig::BOOST_MIN) / (AppConfig::BOOST_MAX - AppConfig::BOOST_MIN);
            float bangle_deg = arc_angle_for_t(AppConfig::BOOST_ARC_START_DEG, AppConfig::BOOST_ARC_END_DEG, bt);
            float bangle_rad = bangle_deg * (float)M_PI / 180.0f;
            float bradius = 206.0f;
            int boost_marker_x = (int)lroundf((float)AppConfig::CX + cosf(bangle_rad) * bradius - 12.0f);
            int boost_marker_y = (int)lroundf((float)AppConfig::CY + sinf(bangle_rad) * bradius - 12.0f);
            set_marker_pos_if_changed(app->boostafr.boost_target_marker, boost_marker_x, boost_marker_y,
                                      &last_boost_marker_x, &last_boost_marker_y);
            set_marker_hidden(app->boostafr.boost_target_marker, false, &boost_marker_hidden);
        } else {
            set_marker_hidden(app->boostafr.boost_target_marker, true, &boost_marker_hidden);
        }

        bool boost_cond = rpm_active && target_boost_valid && ((boost_kpa_gauge - ht.target_boost_kpa) >= AppConfig::BOOST_ALERT_KPA_DELTA);
        if (boost_cond) {
            if (app->boostafr.boost_over_target_start_ms == 0) app->boostafr.boost_over_target_start_ms = now;
            if ((now - app->boostafr.boost_over_target_start_ms) >= AppConfig::ALERT_HOLD_MS) app->boostafr.boost_over_target_alert = true;
        } else {
            app->boostafr.boost_over_target_start_ms = 0;
            app->boostafr.boost_over_target_alert = false;
        }

        bool lambda_cond = rpm_active && (lambda_actual > (lambda_target * AppConfig::LAMBDA_LEAN_RATIO));
        if (lambda_cond) {
            if (app->boostafr.lambda_lean_start_ms == 0) app->boostafr.lambda_lean_start_ms = now;
            if ((now - app->boostafr.lambda_lean_start_ms) >= AppConfig::ALERT_HOLD_MS) app->boostafr.lambda_lean_alert = true;
        } else {
            app->boostafr.lambda_lean_start_ms = 0;
            app->boostafr.lambda_lean_alert = false;
        }

        bool flash_on = (((now / 180U) % 2U) == 0U);
        set_boost_color_mode((app->boostafr.boost_over_target_alert && flash_on) ? 1 : 0);
        set_afr_color_mode((app->boostafr.lambda_lean_alert && flash_on) ? 1 : 0);
    }

    uint32_t dt_ms = (app->boostafr.visual_last_ms > 0 && now > app->boostafr.visual_last_ms)
        ? (now - app->boostafr.visual_last_ms)
        : AppConfig::BOOSTAFR_ARC_TICK_MS;
    app->boostafr.visual_last_ms = now;

    if (!app->boostafr.visual_initialized) {
        app->boostafr.map_visual_psi = map_psi_target;
        app->boostafr.afr_visual = afr_actual_target;
        app->boostafr.visual_initialized = true;
    } else {
        float map_alpha = (float)dt_ms / (AppConfig::BOOSTAFR_MAP_TAU_MS + (float)dt_ms);
        float afr_alpha = (float)dt_ms / (AppConfig::BOOSTAFR_AFR_TAU_MS + (float)dt_ms);
        map_alpha = clampf(map_alpha, 0.10f, 0.85f);
        afr_alpha = clampf(afr_alpha, 0.08f, 0.80f);

        app->boostafr.map_visual_psi += (map_psi_target - app->boostafr.map_visual_psi) * map_alpha;
        app->boostafr.afr_visual += (afr_actual_target - app->boostafr.afr_visual) * afr_alpha;
    }

    int map_arc_value = (int)lroundf(((app->boostafr.map_visual_psi - AppConfig::BOOST_MIN) /
                                      (AppConfig::BOOST_MAX - AppConfig::BOOST_MIN)) * 1000.0f);
    map_arc_value = (int)clampf((float)map_arc_value, 0.0f, 1000.0f);
    set_arc_value_if_changed(app->boostafr.map_arc, map_arc_value, &last_map_arc_value);

    int afr_arc_value = (int)lroundf(((app->boostafr.afr_visual - AppConfig::AFR_MIN) /
                                      (AppConfig::AFR_MAX - AppConfig::AFR_MIN)) * 1000.0f);
    afr_arc_value = (int)clampf((float)afr_arc_value, 0.0f, 1000.0f);
    set_arc_value_if_changed(app->boostafr.afr_arc, afr_arc_value, &last_afr_arc_value);
}

static void tick_boostafr_labels(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    char buf[24];

    if (app->boostafr.demo_mode) {
        float map_psi = 0.0f;
        float afr_actual = 14.7f;
        get_demo_values(app, now, &map_psi, &afr_actual);

        snprintf(buf, sizeof(buf), "%.1f", map_psi);
        lv_label_set_text(app->boostafr.psi_label, buf);

        snprintf(buf, sizeof(buf), "%.1f", afr_actual);
        lv_label_set_text(app->boostafr.afr_label, buf);
        return;
    }

    float map_psi = 0.0f;
    if (app->can.ht.last_0x360_ms > 0 && (now - app->can.ht.last_0x360_ms) < 1200) {
        map_psi = (app->can.ht.map_kpa_abs - AppConfig::ATMOSPHERIC_KPA) * AppConfig::KPA_TO_PSI;
    }
    map_psi = clampf(map_psi, AppConfig::BOOST_MIN, AppConfig::BOOST_MAX);
    snprintf(buf, sizeof(buf), "%.1f", map_psi);
    lv_label_set_text(app->boostafr.psi_label, buf);

    float lambda_actual = 1.0f;
    if (app->can.ht.last_0x368_ms > 0 && (now - app->can.ht.last_0x368_ms) < 1200) {
        lambda_actual = app->can.ht.lambda1;
    }
    float afr_actual = clampf(lambda_actual * AppConfig::STOICH_AFR, AppConfig::AFR_MIN, AppConfig::AFR_MAX);
    snprintf(buf, sizeof(buf), "%.1f", afr_actual);
    lv_label_set_text(app->boostafr.afr_label, buf);
}

lv_obj_t *create_boostafr_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->boostafr.screen = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *top_arc = lv_arc_create(screen);
    app->boostafr.map_arc = top_arc;
    lv_obj_set_size(top_arc, 420, 420);
    lv_obj_center(top_arc);
    lv_arc_set_rotation(top_arc, 0);
    lv_arc_set_bg_angles(top_arc, (int)AppConfig::BOOST_ARC_START_DEG, (int)AppConfig::BOOST_ARC_END_DEG);
    lv_arc_set_range(top_arc, 0, 1000);
    lv_arc_set_value(top_arc, 0);
    lv_obj_set_style_arc_width(top_arc, 36, LV_PART_MAIN);
    lv_obj_set_style_arc_width(top_arc, 36, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(top_arc, lv_color_hex(0x3b3b3b), LV_PART_MAIN);
    lv_obj_set_style_arc_color(top_arc, lv_color_hex(0x10b8ff), LV_PART_INDICATOR);
    lv_obj_set_style_opa(top_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(top_arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_opa(top_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(top_arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *bottom_arc = lv_arc_create(screen);
    app->boostafr.afr_arc = bottom_arc;
    lv_obj_set_size(bottom_arc, 420, 420);
    lv_obj_center(bottom_arc);
    lv_arc_set_rotation(bottom_arc, 0);
    lv_arc_set_bg_angles(bottom_arc, (int)AppConfig::AFR_ARC_START_DEG, (int)AppConfig::AFR_ARC_END_DEG);
    lv_arc_set_range(bottom_arc, 0, 1000);
    lv_arc_set_value(bottom_arc, 470);
    lv_obj_set_style_arc_width(bottom_arc, 36, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bottom_arc, 36, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(bottom_arc, lv_color_hex(0x3b3b3b), LV_PART_MAIN);
    lv_obj_set_style_arc_color(bottom_arc, lv_color_hex(0x20c870), LV_PART_INDICATOR);
    lv_obj_set_style_opa(bottom_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(bottom_arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_opa(bottom_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(bottom_arc, LV_OBJ_FLAG_CLICKABLE);

    app->boostafr.psi_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->boostafr.psi_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->boostafr.psi_label, &lv_font_montserrat_medium_72, 0);
    lv_obj_set_width(app->boostafr.psi_label, 360);
    lv_obj_set_style_text_align(app->boostafr.psi_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->boostafr.psi_label, AppConfig::CX - 180, 112);
    lv_label_set_text(app->boostafr.psi_label, "0.0");

    app->boostafr.psi_title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->boostafr.psi_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->boostafr.psi_title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(app->boostafr.psi_title_label, 320);
    lv_obj_set_style_text_align(app->boostafr.psi_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->boostafr.psi_title_label, AppConfig::CX - 160, 182);
    lv_label_set_text(app->boostafr.psi_title_label, "PSI BOOST");

    app->boostafr.afr_title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->boostafr.afr_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->boostafr.afr_title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(app->boostafr.afr_title_label, 320);
    lv_obj_set_style_text_align(app->boostafr.afr_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->boostafr.afr_title_label, AppConfig::CX - 160, 277);
    lv_label_set_text(app->boostafr.afr_title_label, "AFR");

    app->boostafr.afr_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->boostafr.afr_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->boostafr.afr_label, &lv_font_montserrat_medium_72, 0);
    lv_obj_set_width(app->boostafr.afr_label, 360);
    lv_obj_set_style_text_align(app->boostafr.afr_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->boostafr.afr_label, AppConfig::CX - 180, 322);
    lv_label_set_text(app->boostafr.afr_label, "14.7");

    app->boostafr.afr_target_marker = lv_obj_create(screen);
    lv_obj_remove_style_all(app->boostafr.afr_target_marker);
    lv_obj_set_size(app->boostafr.afr_target_marker, 24, 24);
    lv_obj_set_pos(app->boostafr.afr_target_marker, AppConfig::CX - 12, AppConfig::CY + 180);
    lv_obj_add_event_cb(app->boostafr.afr_target_marker, draw_triangle_marker_event, LV_EVENT_DRAW_MAIN, nullptr);

    app->boostafr.boost_target_marker = lv_obj_create(screen);
    lv_obj_remove_style_all(app->boostafr.boost_target_marker);
    lv_obj_set_size(app->boostafr.boost_target_marker, 24, 24);
    lv_obj_set_pos(app->boostafr.boost_target_marker, AppConfig::CX - 12, AppConfig::CY - 200);
    lv_obj_add_event_cb(app->boostafr.boost_target_marker, draw_triangle_marker_event, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_flag(app->boostafr.boost_target_marker, LV_OBJ_FLAG_HIDDEN);

    app->boostafr.arc_timer = lv_timer_create(tick_boostafr_arcs, AppConfig::BOOSTAFR_ARC_TICK_MS, app);
    app->boostafr.label_timer = lv_timer_create(tick_boostafr_labels, AppConfig::BOOSTAFR_LABEL_TICK_MS, app);
    tick_boostafr_arcs(app->boostafr.arc_timer);
    tick_boostafr_labels(app->boostafr.label_timer);

    return screen;
}