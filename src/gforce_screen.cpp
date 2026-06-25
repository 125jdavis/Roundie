#include "gforce_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Config/calibrate screen button dimensions (260x66 pill buttons, font_32 labels)
static constexpr int GCFG_BTN_W  = 260;
static constexpr int GCFG_BTN_H  = 66;
static constexpr int GCFG_BTN_Y0 = 96;  // y of first button top
static constexpr int GCFG_BTN_DY = 84;  // vertical step between buttons

static void gforce_update_source_btn(AppContext *app);
static void gforce_update_calibrate_btn_state(AppContext *app);
static void create_gforce_config_screen(AppContext *app);
static void create_gforce_calibrate_screen(AppContext *app);

// ── screen navigation ─────────────────────────────────────────────────────────

static void gforce_load_main_screen(AppContext *app) {
    app->gforce.config_visible = false;
    lv_scr_load(app->gforce.screen);
}

static void gforce_load_config_screen(AppContext *app) {
    app->gforce.config_visible = true;
    gforce_update_source_btn(app);
    gforce_update_calibrate_btn_state(app);
    lv_scr_load(app->gforce.config_screen);
}

static void gforce_load_calibrate_screen(AppContext *app) {
    app->gforce.config_visible = true;
    if (app->gforce.cal_question_label) lv_obj_clear_flag(app->gforce.cal_question_label, LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_status_label)   lv_obj_add_flag(app->gforce.cal_status_label,   LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_yes_btn)        lv_obj_clear_flag(app->gforce.cal_yes_btn,        LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_no_btn)         lv_obj_clear_flag(app->gforce.cal_no_btn,         LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(app->gforce.calibrate_screen);
}

bool gforce_is_config_visible(const AppContext *app) {
    return app->gforce.config_visible;
}

void gforce_handle_double_tap(AppContext *app) {
    gforce_load_config_screen(app);
}

// ── config screen: button state helpers ──────────────────────────────────────

static void gforce_update_source_btn(AppContext *app) {
    if (!app->gforce.cfg_source_btn) return;
    if (app->gforce.source_internal) {
        lv_label_set_text(app->gforce.cfg_source_btn_label, "Source: Onboard");
        lv_obj_set_style_bg_color(app->gforce.cfg_source_btn, lv_color_hex(0x3d2a6a), 0);
    } else {
        lv_label_set_text(app->gforce.cfg_source_btn_label, "Source: CAN Bus");
        lv_obj_set_style_bg_color(app->gforce.cfg_source_btn, lv_color_hex(0x1e4d7a), 0);
    }
}

static void gforce_update_calibrate_btn_state(AppContext *app) {
    if (!app->gforce.cfg_calibrate_btn) return;
    if (app->gforce.source_internal) {
        lv_obj_clear_state(app->gforce.cfg_calibrate_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(app->gforce.cfg_calibrate_btn, lv_color_hex(0x2a5c3d), 0);
        if (app->gforce.cfg_calibrate_btn_label)
            lv_obj_set_style_text_color(app->gforce.cfg_calibrate_btn_label, lv_color_hex(0xFFFFFF), 0);
    } else {
        lv_obj_add_state(app->gforce.cfg_calibrate_btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(app->gforce.cfg_calibrate_btn, lv_color_hex(0x383d43), 0);
        if (app->gforce.cfg_calibrate_btn_label)
            lv_obj_set_style_text_color(app->gforce.cfg_calibrate_btn_label, lv_color_hex(0x8f99a3), 0);
    }
}

// ── config screen events ──────────────────────────────────────────────────────

static void gforce_reset_button_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    gforce_clear_peak_memory(app);
    gforce_load_main_screen(app);
}

static void gforce_exit_config_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    gforce_load_main_screen(app);
}

static void gforce_source_btn_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    app->gforce.source_internal = !app->gforce.source_internal;
    app->gforce.demo_mode = app->gforce.source_internal;
    app->gforce.demo_calibrated = false;
    app->gforce.demo_filter_valid = false;
    gforce_update_source_btn(app);
    gforce_update_calibrate_btn_state(app);
}

static void gforce_calibrate_btn_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app->gforce.source_internal) return;
    gforce_load_calibrate_screen(app);
}

// ── calibrate screen events ───────────────────────────────────────────────────

static void gforce_cal_return_timer_cb(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    lv_timer_del(timer);
    gforce_load_config_screen(app);
}

static void gforce_cal_yes_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (app->platform.imu_ready && app->platform.imu.getAccelerometer(ax, ay, az)) {
        app->gforce.demo_ax0               = ax;
        app->gforce.demo_ay0               = ay;
        app->gforce.demo_az0               = az;
        app->gforce.demo_calibrated        = true;
        app->gforce.demo_filter_valid      = false;
        app->gforce.demo_lateral_filt      = 0.0f;
        app->gforce.demo_longitudinal_filt = 0.0f;
    }
    if (app->gforce.cal_yes_btn)        lv_obj_add_flag(app->gforce.cal_yes_btn,        LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_no_btn)         lv_obj_add_flag(app->gforce.cal_no_btn,          LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_question_label) lv_obj_add_flag(app->gforce.cal_question_label,  LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_status_label) {
        lv_label_set_text(app->gforce.cal_status_label, "Calibrated!");
        lv_obj_clear_flag(app->gforce.cal_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_create(gforce_cal_return_timer_cb, 1500, app);
}

static void gforce_cal_no_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (app->gforce.cal_yes_btn)        lv_obj_add_flag(app->gforce.cal_yes_btn,        LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_no_btn)         lv_obj_add_flag(app->gforce.cal_no_btn,          LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_question_label) lv_obj_add_flag(app->gforce.cal_question_label,  LV_OBJ_FLAG_HIDDEN);
    if (app->gforce.cal_status_label) {
        lv_label_set_text(app->gforce.cal_status_label, "Calibration cancelled");
        lv_obj_clear_flag(app->gforce.cal_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_create(gforce_cal_return_timer_cb, 1500, app);
}

static float normalize_g_from_ms2(float value_ms2) {
    return value_ms2 / AppConfig::GFORCE_MS2_PER_G;
}

static bool gforce_is_fresh(const AppContext *app, uint32_t now_ms) {
    const HaltechData &ht = app->can.ht;
    return ht.last_0x36B_ms > 0 && ht.last_0x36E_ms > 0 &&
           (now_ms - ht.last_0x36B_ms) < AppConfig::GFORCE_CAN_TIMEOUT_MS &&
           (now_ms - ht.last_0x36E_ms) < AppConfig::GFORCE_CAN_TIMEOUT_MS;
}

static int wrap_bin(int bin) {
    const int count = (int)AppConfig::GFORCE_ENVELOPE_BINS;
    while (bin < 0) bin += count;
    while (bin >= count) bin -= count;
    return bin;
}

static void update_envelope_bin_max(AppContext *app, int bin, float radius_norm) {
    int idx = wrap_bin(bin);
    if (radius_norm > app->gforce.envelope_radius_norm[idx]) {
        app->gforce.envelope_radius_norm[idx] = radius_norm;
        app->gforce.envelope_dirty = true;
    }
}

static void update_envelope_between(AppContext *app, int from_bin, float from_norm, int to_bin, float to_norm) {
    const int count = (int)AppConfig::GFORCE_ENVELOPE_BINS;
    int diff = to_bin - from_bin;
    if (diff > count / 2) diff -= count;
    if (diff < -(count / 2)) diff += count;

    int steps = (diff >= 0) ? diff : -diff;
    if (steps == 0) {
        update_envelope_bin_max(app, to_bin, to_norm);
        return;
    }

    int dir = diff > 0 ? 1 : -1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float interp = from_norm + (to_norm - from_norm) * t;
        update_envelope_bin_max(app, from_bin + dir * i, interp);
    }
}

static float envelope_radius_for_bin(const AppContext *app, int bin) {
    const int count = (int)AppConfig::GFORCE_ENVELOPE_BINS;
    const float *bins = app->gforce.envelope_radius_norm;
    if (bins[bin] >= 0.0f) return bins[bin];

    int prev = -1;
    for (int step = 1; step < count; step++) {
        int idx = (bin - step + count) % count;
        if (bins[idx] >= 0.0f) {
            prev = idx;
            break;
        }
    }

    int next = -1;
    for (int step = 1; step < count; step++) {
        int idx = (bin + step) % count;
        if (bins[idx] >= 0.0f) {
            next = idx;
            break;
        }
    }

    if (prev < 0 && next < 0) return -1.0f;
    if (prev < 0) return bins[next];
    if (next < 0) return bins[prev];

    int cw_steps = (next - prev + count) % count;
    int from_prev = (bin - prev + count) % count;
    if (cw_steps <= 0) return bins[prev];

    float t = (float)from_prev / (float)cw_steps;
    return bins[prev] + (bins[next] - bins[prev]) * t;
}

static void draw_envelope_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *obj = lv_event_get_target(event);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const int cx = (int)AppConfig::CX;
    const int cy = (int)AppConfig::CY;
    const float max_r = AppConfig::GFORCE_EDGE_RADIUS_PX;
    const int bin_count = (int)AppConfig::GFORCE_ENVELOPE_BINS;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x5a5a5a);
    line_dsc.width = 2;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    bool have_any = false;
    for (int i = 0; i < bin_count; i++) {
        if (app->gforce.envelope_radius_norm[i] >= 0.0f) {
            have_any = true;
            break;
        }
    }
    if (!have_any) return;

    for (int i = 0; i < bin_count; i++) {
        int j = (i + 1) % bin_count;
        float ri = envelope_radius_for_bin(app, i);
        float rj = envelope_radius_for_bin(app, j);
        if (ri < 0.0f || rj < 0.0f) continue;

        float ai = (2.0f * (float)M_PI * (float)i) / (float)bin_count;
        float aj = (2.0f * (float)M_PI * (float)j) / (float)bin_count;

        lv_point_t p1 = {
            (lv_coord_t)lroundf((float)coords.x1 + (float)cx + cosf(ai) * (ri * max_r)),
            (lv_coord_t)lroundf((float)coords.y1 + (float)cy + sinf(ai) * (ri * max_r))
        };
        lv_point_t p2 = {
            (lv_coord_t)lroundf((float)coords.x1 + (float)cx + cosf(aj) * (rj * max_r)),
            (lv_coord_t)lroundf((float)coords.y1 + (float)cy + sinf(aj) * (rj * max_r))
        };

        lv_draw_line(draw_ctx, &line_dsc, &p1, &p2);
    }
}

void gforce_clear_peak_memory(AppContext *app) {
    for (uint16_t i = 0; i < AppConfig::GFORCE_ENVELOPE_BINS; i++) {
        app->gforce.envelope_radius_norm[i] = -1.0f;
    }
    app->gforce.envelope_prev_bin = -1;
    app->gforce.envelope_prev_norm = 0.0f;
    app->gforce.envelope_prev_valid = false;
    app->gforce.envelope_dirty = true;
    if (app->gforce.envelope_layer) {
        lv_obj_invalidate(app->gforce.envelope_layer);
    }
}

void gforce_set_demo_mode(AppContext *app, bool enabled, uint32_t now_ms) {
    app->gforce.demo_mode = enabled;
    app->gforce.source_internal = enabled;
    app->gforce.demo_start_ms = now_ms;
    app->gforce.demo_calibrated = false;
    app->gforce.demo_filter_valid = false;
    gforce_update_source_btn(app);
    gforce_update_calibrate_btn_state(app);
}

static void tick_gforce(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now_ms = lv_tick_get();

    bool fresh = gforce_is_fresh(app, now_ms);
    float lateral_g = 0.0f;
    float longitudinal_g = 0.0f;

    if (app->gforce.source_internal || app->gforce.demo_mode) {
        float ax = 0.0f;
        float ay = 0.0f;
        float az = 0.0f;
        if (app->platform.imu_ready && app->platform.imu.getAccelerometer(ax, ay, az)) {
            if (!app->gforce.demo_calibrated) {
                app->gforce.demo_ax0 = ax;
                app->gforce.demo_ay0 = ay;
                app->gforce.demo_az0 = az;
                app->gforce.demo_calibrated = true;
            }

            // Align demo motion to screen expectations:
            // rest position centers the dot, and 90 deg CCW tilt drives left.
            float lateral_raw = -(ay - app->gforce.demo_ay0);
            float longitudinal_raw = (az - app->gforce.demo_az0);

            if (!app->gforce.demo_filter_valid) {
                app->gforce.demo_lateral_filt = lateral_raw;
                app->gforce.demo_longitudinal_filt = longitudinal_raw;
                app->gforce.demo_filter_valid = true;
            } else {
                const float alpha = 0.156f;
                app->gforce.demo_lateral_filt += (lateral_raw - app->gforce.demo_lateral_filt) * alpha;
                app->gforce.demo_longitudinal_filt += (longitudinal_raw - app->gforce.demo_longitudinal_filt) * alpha;
            }

            lateral_g = app->gforce.demo_lateral_filt;
            longitudinal_g = app->gforce.demo_longitudinal_filt;
            fresh = true;
        } else {
            fresh = false;
        }
    } else if (fresh) {
        lateral_g = normalize_g_from_ms2(app->can.ht.lateral_g_ms2);
        longitudinal_g = normalize_g_from_ms2(app->can.ht.longitudinal_g_ms2);
    }

    if (!fresh) {
        lv_obj_add_flag(app->gforce.dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(app->gforce.mag_label, "--.--");
        if (app->gforce.mag_shadow_label) {
            lv_label_set_text(app->gforce.mag_shadow_label, "--.--");
        }
        app->gforce.envelope_prev_valid = false;
        return;
    }

    const float max_g = AppConfig::GFORCE_MAX_G;
    const float max_r_px = AppConfig::GFORCE_EDGE_RADIUS_PX;
    const float dot_radius_px = 7.0f;

    float x = clampf(lateral_g / max_g, -1.0f, 1.0f);
    float y = clampf(longitudinal_g / max_g, -1.0f, 1.0f);
    float norm = sqrtf(x * x + y * y);
    if (norm > 1.0f) {
        x /= norm;
        y /= norm;
    }

    float mag_g = sqrtf(lateral_g * lateral_g + longitudinal_g * longitudinal_g);
    char mag_buf[20];
    snprintf(mag_buf, sizeof(mag_buf), "%.2f", mag_g);
    lv_label_set_text(app->gforce.mag_label, mag_buf);
    if (app->gforce.mag_shadow_label) {
        lv_label_set_text(app->gforce.mag_shadow_label, mag_buf);
    }

    lv_coord_t dot_x = (lv_coord_t)lroundf((float)AppConfig::CX + x * max_r_px - dot_radius_px);
    lv_coord_t dot_y = (lv_coord_t)lroundf((float)AppConfig::CY + y * max_r_px - dot_radius_px);
    lv_obj_set_pos(app->gforce.dot, dot_x, dot_y);
    lv_obj_clear_flag(app->gforce.dot, LV_OBJ_FLAG_HIDDEN);

    float env_x = lateral_g;
    float env_y = longitudinal_g;
    float env_mag = sqrtf(env_x * env_x + env_y * env_y);
    float env_norm = clampf(env_mag / max_g, 0.0f, 1.0f);
    float angle = atan2f(env_y, env_x);
    if (angle < 0.0f) angle += 2.0f * (float)M_PI;

    int bin = (int)lroundf((angle / (2.0f * (float)M_PI)) * (float)(AppConfig::GFORCE_ENVELOPE_BINS - 1));
    if (bin < 0) bin = 0;
    if (bin >= (int)AppConfig::GFORCE_ENVELOPE_BINS) bin = (int)AppConfig::GFORCE_ENVELOPE_BINS - 1;

    if (app->gforce.envelope_prev_valid) {
        update_envelope_between(app,
                                app->gforce.envelope_prev_bin,
                                app->gforce.envelope_prev_norm,
                                bin,
                                env_norm);
    } else {
        update_envelope_bin_max(app, bin, env_norm);
    }

    app->gforce.envelope_prev_bin = bin;
    app->gforce.envelope_prev_norm = env_norm;
    app->gforce.envelope_prev_valid = true;

    if (app->gforce.envelope_dirty && app->gforce.envelope_layer) {
        lv_obj_invalidate(app->gforce.envelope_layer);
        app->gforce.envelope_dirty = false;
    }
}

lv_obj_t *create_gforce_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->gforce.screen = screen;

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);

    const float crosshair_extent = AppConfig::GFORCE_EDGE_RADIUS_PX - 10.0f;
    app->gforce.crosshair_h_pts[0] = {(lv_coord_t)lroundf((float)AppConfig::CX - crosshair_extent), (lv_coord_t)AppConfig::CY};
    app->gforce.crosshair_h_pts[1] = {(lv_coord_t)lroundf((float)AppConfig::CX + crosshair_extent), (lv_coord_t)AppConfig::CY};
    app->gforce.crosshair_v_pts[0] = {(lv_coord_t)AppConfig::CX, (lv_coord_t)lroundf((float)AppConfig::CY - crosshair_extent)};
    app->gforce.crosshair_v_pts[1] = {(lv_coord_t)AppConfig::CX, (lv_coord_t)lroundf((float)AppConfig::CY + crosshair_extent)};

    app->gforce.crosshair_h = lv_line_create(screen);
    lv_line_set_points(app->gforce.crosshair_h, app->gforce.crosshair_h_pts, 2);
    lv_obj_set_style_line_color(app->gforce.crosshair_h, lv_color_hex(0x8a8a8a), 0);
    lv_obj_set_style_line_width(app->gforce.crosshair_h, 3, 0);
    lv_obj_set_style_line_rounded(app->gforce.crosshair_h, true, 0);

    app->gforce.crosshair_v = lv_line_create(screen);
    lv_line_set_points(app->gforce.crosshair_v, app->gforce.crosshair_v_pts, 2);
    lv_obj_set_style_line_color(app->gforce.crosshair_v, lv_color_hex(0x8a8a8a), 0);
    lv_obj_set_style_line_width(app->gforce.crosshair_v, 3, 0);
    lv_obj_set_style_line_rounded(app->gforce.crosshair_v, true, 0);

    const float ring_g_values[3] = {1.0f, 0.66f, 0.33f};
    for (int i = 0; i < 3; i++) {
        float t = ring_g_values[i] / AppConfig::GFORCE_MAX_G;
        lv_coord_t radius = (lv_coord_t)lroundf(AppConfig::GFORCE_EDGE_RADIUS_PX * t);
        lv_obj_t *ring = lv_obj_create(screen);
        app->gforce.rings[i] = ring;
        lv_obj_set_size(ring, (lv_coord_t)(radius * 2), (lv_coord_t)(radius * 2));
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(0x8a8a8a), 0);
        lv_obj_set_style_border_width(ring, 3, 0);
        lv_obj_center(ring);
    }

    app->gforce.envelope_layer = lv_obj_create(screen);
    lv_obj_remove_style_all(app->gforce.envelope_layer);
    lv_obj_set_size(app->gforce.envelope_layer, AppConfig::LCD_WIDTH, AppConfig::LCD_HEIGHT);
    lv_obj_set_pos(app->gforce.envelope_layer, 0, 0);
    lv_obj_add_flag(app->gforce.envelope_layer, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(app->gforce.envelope_layer, draw_envelope_event, LV_EVENT_DRAW_MAIN, app);

    app->gforce.dot = lv_obj_create(screen);
    lv_obj_set_size(app->gforce.dot, 14, 14);
    lv_obj_set_style_radius(app->gforce.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(app->gforce.dot, lv_color_hex(0xFF7A00), 0);
    lv_obj_set_style_bg_opa(app->gforce.dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(app->gforce.dot, 0, 0);
    lv_obj_set_pos(app->gforce.dot, AppConfig::CX - 7, AppConfig::CY - 7);

    app->gforce.mag_shadow_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->gforce.mag_shadow_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_opa(app->gforce.mag_shadow_label, (lv_opa_t)219, 0);
    lv_obj_set_style_text_font(app->gforce.mag_shadow_label, &lv_font_montserrat_64, 0);
    lv_obj_set_style_text_letter_space(app->gforce.mag_shadow_label, 1, 0);
    lv_obj_set_width(app->gforce.mag_shadow_label, 280);
    lv_obj_set_style_text_align(app->gforce.mag_shadow_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app->gforce.mag_shadow_label, LV_ALIGN_BOTTOM_MID, 5, -96);
    lv_label_set_text(app->gforce.mag_shadow_label, "--.--");

    app->gforce.mag_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->gforce.mag_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->gforce.mag_label, &lv_font_montserrat_64, 0);
    lv_obj_set_width(app->gforce.mag_label, 280);
    lv_obj_set_style_text_align(app->gforce.mag_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app->gforce.mag_label, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_label_set_text(app->gforce.mag_label, "--.--");

    app->gforce.mag_unit_shadow_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->gforce.mag_unit_shadow_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_opa(app->gforce.mag_unit_shadow_label, (lv_opa_t)219, 0);
    lv_obj_set_style_text_font(app->gforce.mag_unit_shadow_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_letter_space(app->gforce.mag_unit_shadow_label, 1, 0);
    lv_obj_set_width(app->gforce.mag_unit_shadow_label, 240);
    lv_obj_set_style_text_align(app->gforce.mag_unit_shadow_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app->gforce.mag_unit_shadow_label, LV_ALIGN_BOTTOM_MID, 5, -22);
    lv_label_set_text(app->gforce.mag_unit_shadow_label, "G-Force");

    app->gforce.mag_unit_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->gforce.mag_unit_label, lv_color_hex(0xBFC7CF), 0);
    lv_obj_set_style_text_font(app->gforce.mag_unit_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(app->gforce.mag_unit_label, 240);
    lv_obj_set_style_text_align(app->gforce.mag_unit_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app->gforce.mag_unit_label, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_label_set_text(app->gforce.mag_unit_label, "G-Force");

    lv_obj_move_foreground(app->gforce.dot);
    lv_obj_move_foreground(app->gforce.mag_shadow_label);
    lv_obj_move_foreground(app->gforce.mag_label);
    lv_obj_move_foreground(app->gforce.mag_unit_shadow_label);
    lv_obj_move_foreground(app->gforce.mag_unit_label);

    gforce_clear_peak_memory(app);
    app->gforce.timer = lv_timer_create(tick_gforce, AppConfig::GFORCE_TICK_MS, app);
    create_gforce_config_screen(app);
    create_gforce_calibrate_screen(app);
    return screen;
}

static void create_gforce_config_screen(AppContext *app) {
    lv_obj_t *scr = lv_obj_create(NULL);
    app->gforce.config_screen = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
    lv_label_set_text(title, "G-Force Config");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    // Reset History
    lv_obj_t *reset_btn = lv_btn_create(scr);
    lv_obj_set_size(reset_btn, GCFG_BTN_W, GCFG_BTN_H);
    lv_obj_set_style_radius(reset_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x1e4d7a), 0);
    lv_obj_set_style_bg_opa(reset_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(reset_btn, 0, 0);
    lv_obj_align(reset_btn, LV_ALIGN_TOP_MID, 0, GCFG_BTN_Y0 + 18);
    lv_obj_add_event_cb(reset_btn, gforce_reset_button_event, LV_EVENT_CLICKED, app);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_obj_set_style_text_color(reset_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(reset_lbl, &lv_font_montserrat_24, 0);
    lv_label_set_text(reset_lbl, "Reset History");
    lv_obj_center(reset_lbl);

    // Source toggle (pill button, tap to switch between CAN Bus / Onboard)
    app->gforce.cfg_source_btn = lv_btn_create(scr);
    lv_obj_set_size(app->gforce.cfg_source_btn, GCFG_BTN_W, GCFG_BTN_H);
    lv_obj_set_style_radius(app->gforce.cfg_source_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(app->gforce.cfg_source_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(app->gforce.cfg_source_btn, 0, 0);
    lv_obj_align(app->gforce.cfg_source_btn, LV_ALIGN_TOP_MID, 0, GCFG_BTN_Y0 + 18 + GCFG_BTN_DY);
    lv_obj_add_event_cb(app->gforce.cfg_source_btn, gforce_source_btn_event, LV_EVENT_CLICKED, app);
    app->gforce.cfg_source_btn_label = lv_label_create(app->gforce.cfg_source_btn);
    lv_obj_set_style_text_color(app->gforce.cfg_source_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->gforce.cfg_source_btn_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(app->gforce.cfg_source_btn_label, "Source: CAN Bus");
    lv_obj_center(app->gforce.cfg_source_btn_label);

    // Calibrate
    app->gforce.cfg_calibrate_btn = lv_btn_create(scr);
    lv_obj_set_size(app->gforce.cfg_calibrate_btn, GCFG_BTN_W, GCFG_BTN_H);
    lv_obj_set_style_radius(app->gforce.cfg_calibrate_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(app->gforce.cfg_calibrate_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(app->gforce.cfg_calibrate_btn, 0, 0);
    lv_obj_align(app->gforce.cfg_calibrate_btn, LV_ALIGN_TOP_MID, 0, GCFG_BTN_Y0 + 18 + GCFG_BTN_DY * 2);
    lv_obj_add_event_cb(app->gforce.cfg_calibrate_btn, gforce_calibrate_btn_event, LV_EVENT_CLICKED, app);
    app->gforce.cfg_calibrate_btn_label = lv_label_create(app->gforce.cfg_calibrate_btn);
    lv_obj_set_style_text_font(app->gforce.cfg_calibrate_btn_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(app->gforce.cfg_calibrate_btn_label, "Calibrate");
    lv_obj_center(app->gforce.cfg_calibrate_btn_label);

    // Exit Config
    lv_obj_t *exit_btn = lv_btn_create(scr);
    lv_obj_set_size(exit_btn, GCFG_BTN_W, GCFG_BTN_H);
    lv_obj_set_style_radius(exit_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(exit_btn, lv_color_hex(0x5b3035), 0);
    lv_obj_set_style_bg_opa(exit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(exit_btn, 0, 0);
    lv_obj_align(exit_btn, LV_ALIGN_TOP_MID, 0, GCFG_BTN_Y0 + 18 + GCFG_BTN_DY * 3);
    lv_obj_add_event_cb(exit_btn, gforce_exit_config_event, LV_EVENT_CLICKED, app);
    lv_obj_t *exit_lbl = lv_label_create(exit_btn);
    lv_obj_set_style_text_color(exit_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(exit_lbl, &lv_font_montserrat_24, 0);
    lv_label_set_text(exit_lbl, "Exit Config");
    lv_obj_center(exit_lbl);

    gforce_update_source_btn(app);
    gforce_update_calibrate_btn_state(app);
}

static void create_gforce_calibrate_screen(AppContext *app) {
    lv_obj_t *scr = lv_obj_create(NULL);
    app->gforce.calibrate_screen = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_label_set_text(title, "Calibration");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    app->gforce.cal_question_label = lv_label_create(scr);
    lv_obj_set_style_text_color(app->gforce.cal_question_label, lv_color_hex(0xd7dde4), 0);
    lv_obj_set_style_text_font(app->gforce.cal_question_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(app->gforce.cal_question_label, 390);
    lv_obj_set_style_text_align(app->gforce.cal_question_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(app->gforce.cal_question_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(app->gforce.cal_question_label, "Are you stopped on\nflat ground?");
    lv_obj_align(app->gforce.cal_question_label, LV_ALIGN_CENTER, 0, -40);

    app->gforce.cal_status_label = lv_label_create(scr);
    lv_obj_set_style_text_color(app->gforce.cal_status_label, lv_color_hex(0xFFD060), 0);
    lv_obj_set_style_text_font(app->gforce.cal_status_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(app->gforce.cal_status_label, 390);
    lv_obj_set_style_text_align(app->gforce.cal_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(app->gforce.cal_status_label, "");
    lv_obj_align(app->gforce.cal_status_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(app->gforce.cal_status_label, LV_OBJ_FLAG_HIDDEN);

    // Yes / No buttons side by side at bottom
    app->gforce.cal_yes_btn = lv_btn_create(scr);
    lv_obj_set_size(app->gforce.cal_yes_btn, 190, GCFG_BTN_H);
    lv_obj_set_style_radius(app->gforce.cal_yes_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(app->gforce.cal_yes_btn, lv_color_hex(0x2a5c3d), 0);
    lv_obj_set_style_bg_opa(app->gforce.cal_yes_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(app->gforce.cal_yes_btn, 0, 0);
    lv_obj_align(app->gforce.cal_yes_btn, LV_ALIGN_BOTTOM_LEFT, 35, -110);
    lv_obj_add_event_cb(app->gforce.cal_yes_btn, gforce_cal_yes_event, LV_EVENT_CLICKED, app);
    lv_obj_t *yes_lbl = lv_label_create(app->gforce.cal_yes_btn);
    lv_obj_set_style_text_color(yes_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(yes_lbl, &lv_font_montserrat_32, 0);
    lv_label_set_text(yes_lbl, "Yes");
    lv_obj_center(yes_lbl);

    app->gforce.cal_no_btn = lv_btn_create(scr);
    lv_obj_set_size(app->gforce.cal_no_btn, 190, GCFG_BTN_H);
    lv_obj_set_style_radius(app->gforce.cal_no_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(app->gforce.cal_no_btn, lv_color_hex(0x5b3035), 0);
    lv_obj_set_style_bg_opa(app->gforce.cal_no_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(app->gforce.cal_no_btn, 0, 0);
    lv_obj_align(app->gforce.cal_no_btn, LV_ALIGN_BOTTOM_RIGHT, -35, -110);
    lv_obj_add_event_cb(app->gforce.cal_no_btn, gforce_cal_no_event, LV_EVENT_CLICKED, app);
    lv_obj_t *no_lbl = lv_label_create(app->gforce.cal_no_btn);
    lv_obj_set_style_text_color(no_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(no_lbl, &lv_font_montserrat_32, 0);
    lv_label_set_text(no_lbl, "No");
    lv_obj_center(no_lbl);
}
