#include "alternate_boost_screen.h"

#include <math.h>

namespace {

static constexpr float ALT_MIN_STEP = -15.0f;
static constexpr float ALT_MAX_STEP = 20.0f;
static constexpr float ALT_STEP_RANGE = ALT_MAX_STEP - ALT_MIN_STEP;
static constexpr float ALT_MIN_PSI = -(AppConfig::ALT_BOOST_INHG_MAX / AppConfig::ALT_BOOST_INHG_PER_PSI);
static constexpr int ALT_MARKER_SIZE = 24;
static constexpr float ALT_MARKER_RADIUS = 206.0f;

float clamp_alt_psi(float psi) {
    return clampf(psi, ALT_MIN_PSI, AppConfig::ALT_BOOST_PSI_MAX);
}

float psi_to_alt_step(float psi) {
    psi = clamp_alt_psi(psi);
    if (psi >= 0.0f) return psi;

    float inhg = clampf(-psi * AppConfig::ALT_BOOST_INHG_PER_PSI, 0.0f, AppConfig::ALT_BOOST_INHG_MAX);
    return -(inhg * 0.5f);
}

float alt_step_to_angle_deg(float step) {
    step = clampf(step, ALT_MIN_STEP, ALT_MAX_STEP);
    float norm = (step - ALT_MIN_STEP) / ALT_STEP_RANGE;
    return AppConfig::BOOST_START_DEG + norm * AppConfig::BOOST_SWEEP_DEG;
}

float psi_to_alt_angle_deg(float psi) {
    return alt_step_to_angle_deg(psi_to_alt_step(psi));
}

float psi_to_alt_angle_rad(float psi) {
    return psi_to_alt_angle_deg(psi) * (float)M_PI / 180.0f;
}

float get_demo_boost_psi(AppContext *app, uint32_t now) {
    uint32_t elapsed = (now - app->alt_boost.demo_start_ms) % AppConfig::ALT_BOOST_DEMO_PERIOD_MS;

    if (elapsed < 2500U) {
        float t = (float)elapsed / 2500.0f;
        return -12.0f + 10.0f * t;
    }
    if (elapsed < 4500U) {
        float t = (float)(elapsed - 2500U) / 2000.0f;
        return -2.0f + 14.0f * t;
    }
    if (elapsed < 6000U) {
        float t = (float)(elapsed - 4500U) / 1500.0f;
        return 12.0f + 6.0f * t;
    }
    if (elapsed < 9000U) {
        float t = (float)(elapsed - 6000U) / 3000.0f;
        return 18.0f - 10.0f * t;
    }
    if (elapsed < 10500U) {
        return 8.0f;
    }
    if (elapsed < 13800U) {
        float t = (float)(elapsed - 10500U) / 3300.0f;
        return 8.0f - 6.0f * t;
    }

    float t = (float)(elapsed - 13800U) / 200.0f;
    return 2.0f - 7.0f * t;
}

void compute_needle_area(AppContext *app, float psi, lv_area_t *area) {
    if (!app->alt_boost.needle_obj || !area) return;

    lv_area_t coords;
    lv_obj_get_coords(app->alt_boost.needle_obj, &coords);

    float angle = psi_to_alt_angle_rad(psi);
    float dx = cosf(angle);
    float dy = sinf(angle);
    float nx = -dy;
    float ny = dx;

    const float tip_r = 172.0f;
    const float tail_r = 34.0f;
    const float w_base = 10.0f;
    const float w_tip = 5.0f;
    const float w_tail = 10.0f;
    const int margin = 6;

    float cx = (float)coords.x1 + (float)app->alt_boost.needle_pivot_x_local;
    float cy = (float)coords.y1 + (float)app->alt_boost.needle_pivot_y_local;

    float tip_x = cx + dx * tip_r;
    float tip_y = cy + dy * tip_r;
    float tail_x = cx - dx * tail_r;
    float tail_y = cy - dy * tail_r;

    lv_point_t points[8] = {
        {(lv_coord_t)lroundf(cx + nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy + ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(cx - nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy - ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(tip_x - nx * (w_tip * 0.5f)), (lv_coord_t)lroundf(tip_y - ny * (w_tip * 0.5f))},
        {(lv_coord_t)lroundf(tip_x + nx * (w_tip * 0.5f)), (lv_coord_t)lroundf(tip_y + ny * (w_tip * 0.5f))},
        {(lv_coord_t)lroundf(cx + nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy + ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(cx - nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy - ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(tail_x - nx * (w_tail * 0.5f)), (lv_coord_t)lroundf(tail_y - ny * (w_tail * 0.5f))},
        {(lv_coord_t)lroundf(tail_x + nx * (w_tail * 0.5f)), (lv_coord_t)lroundf(tail_y + ny * (w_tail * 0.5f))}
    };

    lv_coord_t min_x = points[0].x;
    lv_coord_t max_x = points[0].x;
    lv_coord_t min_y = points[0].y;
    lv_coord_t max_y = points[0].y;

    for (uint32_t i = 1; i < 8; i++) {
        if (points[i].x < min_x) min_x = points[i].x;
        if (points[i].x > max_x) max_x = points[i].x;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }

    area->x1 = min_x - margin;
    area->y1 = min_y - margin;
    area->x2 = max_x + margin;
    area->y2 = max_y + margin;
}

void redraw_needle(AppContext *app) {
    if (!app->alt_boost.needle_obj) return;

    lv_area_t next_area;
    compute_needle_area(app, app->alt_boost.boost_psi_visual, &next_area);

    if (app->alt_boost.needle_last_area_valid) {
        lv_area_t union_area = {
            (lv_coord_t)LV_MIN(app->alt_boost.needle_last_area.x1, next_area.x1),
            (lv_coord_t)LV_MIN(app->alt_boost.needle_last_area.y1, next_area.y1),
            (lv_coord_t)LV_MAX(app->alt_boost.needle_last_area.x2, next_area.x2),
            (lv_coord_t)LV_MAX(app->alt_boost.needle_last_area.y2, next_area.y2)
        };
        lv_obj_invalidate_area(app->alt_boost.needle_obj, &union_area);
    } else {
        lv_obj_invalidate_area(app->alt_boost.needle_obj, &next_area);
    }

    app->alt_boost.needle_last_area = next_area;
    app->alt_boost.needle_last_area_valid = true;
}

void draw_needle_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    float angle = psi_to_alt_angle_rad(app->alt_boost.boost_psi_visual);
    float dx = cosf(angle);
    float dy = sinf(angle);
    float nx = -dy;
    float ny = dx;

    const float tip_r = 172.0f;
    const float tail_r = 34.0f;
    const float w_base = 10.0f;
    const float w_tip = 5.0f;
    const float w_tail = 10.0f;

    float cx = (float)coords.x1 + (float)app->alt_boost.needle_pivot_x_local;
    float cy = (float)coords.y1 + (float)app->alt_boost.needle_pivot_y_local;

    float tip_x = cx + dx * tip_r;
    float tip_y = cy + dy * tip_r;
    float tail_x = cx - dx * tail_r;
    float tail_y = cy - dy * tail_r;

    lv_point_t front_poly[4] = {
        {(lv_coord_t)lroundf(cx + nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy + ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(cx - nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy - ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(tip_x - nx * (w_tip * 0.5f)), (lv_coord_t)lroundf(tip_y - ny * (w_tip * 0.5f))},
        {(lv_coord_t)lroundf(tip_x + nx * (w_tip * 0.5f)), (lv_coord_t)lroundf(tip_y + ny * (w_tip * 0.5f))}
    };

    lv_point_t tail_poly[4] = {
        {(lv_coord_t)lroundf(cx + nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy + ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(cx - nx * (w_base * 0.5f)), (lv_coord_t)lroundf(cy - ny * (w_base * 0.5f))},
        {(lv_coord_t)lroundf(tail_x - nx * (w_tail * 0.5f)), (lv_coord_t)lroundf(tail_y - ny * (w_tail * 0.5f))},
        {(lv_coord_t)lroundf(tail_x + nx * (w_tail * 0.5f)), (lv_coord_t)lroundf(tail_y + ny * (w_tail * 0.5f))}
    };

    lv_draw_rect_dsc_t needle_dsc;
    lv_draw_rect_dsc_init(&needle_dsc);
    needle_dsc.bg_color = lv_color_hex(0xEAEA00);
    needle_dsc.bg_opa = LV_OPA_COVER;
    needle_dsc.border_width = 0;

    lv_draw_polygon(draw_ctx, &needle_dsc, front_poly, 4);
    lv_draw_polygon(draw_ctx, &needle_dsc, tail_poly, 4);
}

void draw_marker_event(lv_event_t *event) {
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

void set_marker_visible(AppContext *app, bool visible) {
    if (visible) {
        lv_obj_clear_flag(app->alt_boost.max_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(app->alt_boost.max_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_marker_pose(AppContext *app) {
    float angle_rad = app->alt_boost.marker_angle_deg * (float)M_PI / 180.0f;
    int marker_x = (int)lroundf((float)AppConfig::CX + cosf(angle_rad) * ALT_MARKER_RADIUS) - (ALT_MARKER_SIZE / 2);
    int marker_y = (int)lroundf((float)AppConfig::CY + sinf(angle_rad) * ALT_MARKER_RADIUS) - (ALT_MARKER_SIZE / 2);

    lv_obj_set_pos(app->alt_boost.max_marker, (lv_coord_t)marker_x, (lv_coord_t)marker_y);

    float marker_rotation_deg = app->alt_boost.marker_angle_deg - 90.0f;
    while (marker_rotation_deg < 0.0f) marker_rotation_deg += 360.0f;
    while (marker_rotation_deg >= 360.0f) marker_rotation_deg -= 360.0f;
    lv_obj_set_style_transform_angle(app->alt_boost.max_marker, (int16_t)lroundf(marker_rotation_deg * 10.0f), 0);
}

void update_value_labels(AppContext *app, float boost_psi) {
    char value_buf[16];

    if (boost_psi < 0.0f) {
        float inhg = clampf(-boost_psi * AppConfig::ALT_BOOST_INHG_PER_PSI, 0.0f, AppConfig::ALT_BOOST_INHG_MAX);
        snprintf(value_buf, sizeof(value_buf), "%.1f", inhg);
        lv_label_set_text(app->alt_boost.value_label, value_buf);
    } else {
        snprintf(value_buf, sizeof(value_buf), "%.1f", boost_psi);
        lv_label_set_text(app->alt_boost.value_label, value_buf);
    }
}

void update_marker_state(AppContext *app, float boost_psi, float current_angle_deg, uint32_t now, uint32_t dt_ms) {
    bool above_trigger = boost_psi > AppConfig::ALT_BOOST_MARKER_TRIGGER_PSI;

    if (above_trigger) {
        app->alt_boost.below_threshold_start_ms = 0;

        if (!app->alt_boost.marker_visible) {
            app->alt_boost.marker_visible = true;
            app->alt_boost.marker_angle_deg = current_angle_deg;
            app->alt_boost.peak_angle_deg = current_angle_deg;
            app->alt_boost.marker_hold_until_ms = now + AppConfig::ALT_BOOST_MARKER_HOLD_MS;
        } else if (current_angle_deg >= app->alt_boost.peak_angle_deg) {
            app->alt_boost.peak_angle_deg = current_angle_deg;
            app->alt_boost.marker_angle_deg = current_angle_deg;
            app->alt_boost.marker_hold_until_ms = now + AppConfig::ALT_BOOST_MARKER_HOLD_MS;
        } else if (now >= app->alt_boost.marker_hold_until_ms) {
            float max_step_deg = AppConfig::ALT_BOOST_MARKER_DECAY_DPS * ((float)dt_ms / 1000.0f);
            float diff = current_angle_deg - app->alt_boost.marker_angle_deg;
            if (fabsf(diff) <= max_step_deg) {
                app->alt_boost.marker_angle_deg = current_angle_deg;
            } else {
                app->alt_boost.marker_angle_deg += (diff > 0.0f) ? max_step_deg : -max_step_deg;
            }
        }
    } else if (app->alt_boost.marker_visible) {
        if (app->alt_boost.below_threshold_start_ms == 0) {
            app->alt_boost.below_threshold_start_ms = now;
        } else if ((now - app->alt_boost.below_threshold_start_ms) >= AppConfig::ALT_BOOST_MARKER_HIDE_MS) {
            app->alt_boost.marker_visible = false;
            app->alt_boost.marker_hold_until_ms = 0;
        }
    }

    set_marker_visible(app, app->alt_boost.marker_visible);
    if (app->alt_boost.marker_visible) {
        update_marker_pose(app);
    }
}

void tick_alt_boost(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();

    uint32_t dt_ms = (app->alt_boost.needle_frame_last_ms > 0 && now > app->alt_boost.needle_frame_last_ms)
        ? (now - app->alt_boost.needle_frame_last_ms)
        : AppConfig::GAUGE_TICK_MS;
    app->alt_boost.needle_frame_last_ms = now;

    float boost_psi_target = app->alt_boost.demo_mode ? get_demo_boost_psi(app, now) : app->can.boost_psi;
    boost_psi_target = clamp_alt_psi(boost_psi_target);

    float alpha = (float)dt_ms / (24.0f + (float)dt_ms);
    alpha = clampf(alpha, 0.08f, 0.72f);
    app->alt_boost.boost_psi_visual += (boost_psi_target - app->alt_boost.boost_psi_visual) * alpha;

    float needle_angle_deg = psi_to_alt_angle_deg(app->alt_boost.boost_psi_visual);
    bool redraw_needed = isnan(app->alt_boost.needle_last_drawn_angle_deg) ||
                         fabsf(needle_angle_deg - app->alt_boost.needle_last_drawn_angle_deg) >= AppConfig::NEEDLE_REDRAW_THRESHOLD_DEG;

    if ((now - app->alt_boost.value_label_last_ms) >= AppConfig::PSI_LABEL_UPDATE_MS) {
        app->alt_boost.value_label_last_ms = now;
        update_value_labels(app, boost_psi_target);
    }

    if (redraw_needed) {
        redraw_needle(app);
        app->alt_boost.needle_last_drawn_angle_deg = needle_angle_deg;
    }

    float marker_angle_target = psi_to_alt_angle_deg(boost_psi_target);
    update_marker_state(app, boost_psi_target, marker_angle_target, now, dt_ms);
}

}  // namespace

void altboost_toggle_demo(AppContext *app, uint32_t now_ms) {
    app->alt_boost.demo_mode = !app->alt_boost.demo_mode;
    if (app->alt_boost.demo_mode) {
        app->alt_boost.demo_start_ms = now_ms;
    }
}

lv_obj_t *create_alternate_boost_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->alt_boost.screen = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    app->alt_boost.gauge_bg_buf = (lv_color_t *)heap_caps_malloc(
        AppConfig::LCD_WIDTH * AppConfig::LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_obj_t *bg_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(bg_canvas, app->alt_boost.gauge_bg_buf,
                         AppConfig::LCD_WIDTH, AppConfig::LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(bg_canvas, 0, 0);
    lv_canvas_fill_bg(bg_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    for (int step = (int)ALT_MIN_STEP; step <= (int)ALT_MAX_STEP; step++) {
        float angle = alt_step_to_angle_deg((float)step) * (float)M_PI / 180.0f;
        bool major = (step % 5) == 0;

        int outer_r = 218;
        int inner_r = major ? 182 : 196;
        int tick_w = major ? 5 : 2;

        int x1 = AppConfig::CX + (int)lroundf(cosf(angle) * (float)inner_r);
        int y1 = AppConfig::CY + (int)lroundf(sinf(angle) * (float)inner_r);
        int x2 = AppConfig::CX + (int)lroundf(cosf(angle) * (float)outer_r);
        int y2 = AppConfig::CY + (int)lroundf(sinf(angle) * (float)outer_r);

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = lv_color_hex(0xFFFFFF);
        dsc.width = tick_w;
        dsc.opa = LV_OPA_COVER;
        lv_point_t pts[] = {{(lv_coord_t)x1, (lv_coord_t)y1}, {(lv_coord_t)x2, (lv_coord_t)y2}};
        lv_canvas_draw_line(bg_canvas, pts, 2, &dsc);

        if (!major) continue;

        lv_obj_t *num = lv_label_create(screen);
        lv_obj_set_style_text_color(num, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(num, &lv_font_montserrat_32, 0);

        char nbuf[8];
        if (step < 0) {
            snprintf(nbuf, sizeof(nbuf), "%d", -step * 2);
        } else {
            snprintf(nbuf, sizeof(nbuf), "%d", step);
        }

        lv_label_set_text(num, nbuf);
        lv_obj_update_layout(num);

        int label_r = 154;
        int nx = AppConfig::CX + (int)lroundf(cosf(angle) * (float)label_r);
        int ny = AppConfig::CY + (int)lroundf(sinf(angle) * (float)label_r);
        lv_obj_set_pos(num, nx - lv_obj_get_width(num) / 2, ny - lv_obj_get_height(num) / 2);
    }

    lv_obj_t *inhg_label = lv_label_create(screen);
    lv_obj_set_style_text_color(inhg_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(inhg_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(inhg_label, "In.Hg");
    lv_obj_set_pos(inhg_label, 88, 300);

    lv_obj_t *psi_half_label = lv_label_create(screen);
    lv_obj_set_style_text_color(psi_half_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(psi_half_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(psi_half_label, "PSI");
    lv_obj_set_pos(psi_half_label, 322, 300);

    app->alt_boost.value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->alt_boost.value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->alt_boost.value_label, &lv_font_montserrat_semibold_72, 0);
    lv_obj_set_width(app->alt_boost.value_label, 320);
    lv_obj_set_style_text_align(app->alt_boost.value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app->alt_boost.value_label, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_label_set_text(app->alt_boost.value_label, "0.0");

    app->alt_boost.unit_label = nullptr;

    app->alt_boost.boost_title_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->alt_boost.boost_title_label, lv_color_hex(0x7ab8f5), 0);
    lv_obj_set_style_text_font(app->alt_boost.boost_title_label, &lv_font_montserrat_semibold_40, 0);
    lv_obj_set_width(app->alt_boost.boost_title_label, 320);
    lv_obj_set_style_text_align(app->alt_boost.boost_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(app->alt_boost.boost_title_label, AppConfig::CX - 160, AppConfig::CY + 38);
    lv_label_set_text(app->alt_boost.boost_title_label, "BOOST");

    app->alt_boost.needle_pivot_x_local = AppConfig::NEEDLE_CANVAS_SIZE / 2;
    app->alt_boost.needle_pivot_y_local = AppConfig::NEEDLE_CANVAS_SIZE / 2;
    app->alt_boost.needle_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(app->alt_boost.needle_obj);
    lv_obj_set_size(app->alt_boost.needle_obj, AppConfig::NEEDLE_CANVAS_SIZE, AppConfig::NEEDLE_CANVAS_SIZE);
    lv_obj_set_pos(app->alt_boost.needle_obj,
                   AppConfig::CX - app->alt_boost.needle_pivot_x_local,
                   AppConfig::CY - app->alt_boost.needle_pivot_y_local);
    lv_obj_add_flag(app->alt_boost.needle_obj, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(app->alt_boost.needle_obj, draw_needle_event, LV_EVENT_DRAW_MAIN, app);

    app->alt_boost.max_marker = lv_obj_create(screen);
    lv_obj_remove_style_all(app->alt_boost.max_marker);
    lv_obj_set_size(app->alt_boost.max_marker, ALT_MARKER_SIZE, ALT_MARKER_SIZE);
    lv_obj_set_style_transform_pivot_x(app->alt_boost.max_marker, ALT_MARKER_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(app->alt_boost.max_marker, ALT_MARKER_SIZE / 2, 0);
    lv_obj_add_event_cb(app->alt_boost.max_marker, draw_marker_event, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_flag(app->alt_boost.max_marker, LV_OBJ_FLAG_HIDDEN);

    app->alt_boost.needle_pivot = lv_obj_create(screen);
    lv_obj_set_size(app->alt_boost.needle_pivot, 40, 40);
    lv_obj_set_pos(app->alt_boost.needle_pivot, AppConfig::CX - 20, AppConfig::CY - 20);
    lv_obj_set_style_radius(app->alt_boost.needle_pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(app->alt_boost.needle_pivot, lv_color_hex(0xEAEA00), 0);
    lv_obj_set_style_border_width(app->alt_boost.needle_pivot, 0, 0);
    lv_obj_set_style_pad_all(app->alt_boost.needle_pivot, 0, 0);

    uint32_t now = lv_tick_get();
    app->alt_boost.boost_psi_visual = clamp_alt_psi(app->can.boost_psi);
    app->alt_boost.value_label_last_ms = now;
    app->alt_boost.needle_frame_last_ms = now;
    app->alt_boost.needle_last_drawn_angle_deg = NAN;
    app->alt_boost.needle_last_area_valid = false;
    app->alt_boost.marker_visible = false;
    app->alt_boost.marker_angle_deg = 0.0f;
    app->alt_boost.peak_angle_deg = 0.0f;
    app->alt_boost.marker_hold_until_ms = 0;
    app->alt_boost.below_threshold_start_ms = 0;

    update_value_labels(app, app->alt_boost.boost_psi_visual);
    redraw_needle(app);

    app->alt_boost.timer = lv_timer_create(tick_alt_boost, AppConfig::GAUGE_TICK_MS, app);

    return screen;
}
