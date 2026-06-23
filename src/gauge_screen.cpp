#include "gauge_screen.h"

#include <math.h>

#include "can_bus.h"

static float psi_to_angle_rad(float psi) {
    float norm = (psi - AppConfig::BOOST_MIN) / AppConfig::BOOST_RANGE;
    float deg = AppConfig::BOOST_START_DEG + norm * AppConfig::BOOST_SWEEP_DEG;
    return deg * (float)M_PI / 180.0f;
}

static void compute_needle_area(AppContext *app, float psi, lv_area_t *area) {
    if (!app->gauge.needle_obj || !area) return;

    lv_area_t coords;
    lv_obj_get_coords(app->gauge.needle_obj, &coords);

    float angle = psi_to_angle_rad(psi);
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

    float cx = (float)coords.x1 + (float)app->gauge.needle_pivot_x_local;
    float cy = (float)coords.y1 + (float)app->gauge.needle_pivot_y_local;

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

static void redraw_gauge_needle(AppContext *app) {
    if (!app->gauge.needle_obj) return;

    lv_area_t next_area;
    compute_needle_area(app, app->gauge.boost_psi_visual, &next_area);

    if (app->gauge.needle_last_area_valid) {
        lv_area_t union_area = {
            (lv_coord_t)LV_MIN(app->gauge.needle_last_area.x1, next_area.x1),
            (lv_coord_t)LV_MIN(app->gauge.needle_last_area.y1, next_area.y1),
            (lv_coord_t)LV_MAX(app->gauge.needle_last_area.x2, next_area.x2),
            (lv_coord_t)LV_MAX(app->gauge.needle_last_area.y2, next_area.y2)
        };
        lv_obj_invalidate_area(app->gauge.needle_obj, &union_area);
    } else {
        lv_obj_invalidate_area(app->gauge.needle_obj, &next_area);
    }

    app->gauge.needle_last_area = next_area;
    app->gauge.needle_last_area_valid = true;
    app->gauge.needle_invalidate_accum++;
}

static void update_perf_overlay(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    if (!app->gauge.perf_label) return;

    uint32_t now = lv_tick_get();
    if (app->gauge.perf_window_start_ms == 0) {
        app->gauge.perf_window_start_ms = now;
        return;
    }

    uint32_t elapsed_ms = now - app->gauge.perf_window_start_ms;
    if (elapsed_ms < 1000) return;

    float elapsed_s = (float)elapsed_ms / 1000.0f;
    float flushes_per_s = (float)app->gauge.flush_count_accum / elapsed_s;
    float invalidates_per_s = (float)app->gauge.needle_invalidate_accum / elapsed_s;
    float paints_per_s = (float)app->gauge.needle_paint_accum / elapsed_s;
    float avg_flush_ms = app->gauge.flush_count_accum > 0
        ? ((float)app->gauge.flush_us_accum / 1000.0f) / (float)app->gauge.flush_count_accum
        : 0.0f;
    float mpix_per_s = (float)app->gauge.flush_pixels_accum / (elapsed_s * 1000000.0f);

    char perf_buf[192];
    snprintf(perf_buf, sizeof(perf_buf),
             "%s\nneedle req %.1f/s draw %.1f/s\nflush %.1f/s avg %.2f ms\npixels %.2f MPix/s",
             app->platform.lvgl_buffer_mode,
             invalidates_per_s,
             paints_per_s,
             flushes_per_s,
             avg_flush_ms,
             mpix_per_s);
    lv_label_set_text(app->gauge.perf_label, perf_buf);

    app->gauge.perf_window_start_ms = now;
    app->gauge.flush_count_accum = 0;
    app->gauge.needle_invalidate_accum = 0;
    app->gauge.needle_paint_accum = 0;
    app->gauge.flush_pixels_accum = 0;
    app->gauge.flush_us_accum = 0;
}

void gauge_set_profiler_visible(AppContext *app, bool visible) {
    app->gauge.profiler_visible = visible;

    if (app->gauge.perf_label) {
        if (visible) {
            lv_obj_clear_flag(app->gauge.perf_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(app->gauge.perf_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (app->gauge.perf_timer) {
        if (visible) {
            app->gauge.perf_window_start_ms = lv_tick_get();
            app->gauge.flush_count_accum = 0;
            app->gauge.needle_invalidate_accum = 0;
            app->gauge.needle_paint_accum = 0;
            app->gauge.flush_pixels_accum = 0;
            app->gauge.flush_us_accum = 0;
            lv_timer_resume(app->gauge.perf_timer);
        } else {
            lv_timer_pause(app->gauge.perf_timer);
        }
    }
}

static void draw_needle_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    float angle = psi_to_angle_rad(app->gauge.boost_psi_visual);
    float dx = cosf(angle);
    float dy = sinf(angle);
    float nx = -dy;
    float ny = dx;

    const float tip_r = 172.0f;
    const float tail_r = 34.0f;
    const float w_base = 10.0f;
    const float w_tip = 5.0f;
    const float w_tail = 10.0f;

    float cx = (float)coords.x1 + (float)app->gauge.needle_pivot_x_local;
    float cy = (float)coords.y1 + (float)app->gauge.needle_pivot_y_local;

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
    needle_dsc.bg_color = lv_color_hex(0xFF1020);
    needle_dsc.bg_opa = LV_OPA_COVER;
    needle_dsc.border_width = 0;

    lv_draw_polygon(draw_ctx, &needle_dsc, front_poly, 4);
    lv_draw_polygon(draw_ctx, &needle_dsc, tail_poly, 4);
    app->gauge.needle_paint_accum++;
}

static void tick_gauge(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    uint32_t now = lv_tick_get();
    float prev_psi = app->can.boost_psi;
    can_poll(app, now);

    uint32_t dt_ms = (app->gauge.needle_frame_last_ms > 0 && now > app->gauge.needle_frame_last_ms)
        ? (now - app->gauge.needle_frame_last_ms)
        : 4;
    float alpha = 0.30f;
    if (dt_ms > 0) {
        float abs_rate_dps = fabsf((app->can.boost_psi - prev_psi) *
                                   (AppConfig::BOOST_SWEEP_DEG / AppConfig::BOOST_RANGE) *
                                   (1000.0f / (float)dt_ms));
        float anti_tear_mix = 0.0f;
        if (abs_rate_dps > AppConfig::ANTI_TEAR_SOFT_DPS) {
            anti_tear_mix = (abs_rate_dps - AppConfig::ANTI_TEAR_SOFT_DPS) /
                            (AppConfig::ANTI_TEAR_HARD_DPS - AppConfig::ANTI_TEAR_SOFT_DPS);
            if (anti_tear_mix > 1.0f) anti_tear_mix = 1.0f;
        }

        float tau_ms = AppConfig::ANTI_TEAR_TAU_LOW_MS +
                       (AppConfig::ANTI_TEAR_TAU_HIGH_MS - AppConfig::ANTI_TEAR_TAU_LOW_MS) * anti_tear_mix;
        alpha = (float)dt_ms / (tau_ms + (float)dt_ms);
        if (alpha < 0.12f) alpha = 0.12f;
        if (alpha > 0.72f) alpha = 0.72f;
    }

    float prev_visual_psi = app->gauge.boost_psi_visual;
    app->gauge.boost_psi_visual += (app->can.boost_psi - app->gauge.boost_psi_visual) * alpha;

    float max_visual_psi_step = (AppConfig::ANTI_TEAR_MAX_VISUAL_DPS * (float)dt_ms / 1000.0f) *
                                (AppConfig::BOOST_RANGE / AppConfig::BOOST_SWEEP_DEG);
    float visual_dpsi = app->gauge.boost_psi_visual - prev_visual_psi;
    if (visual_dpsi > max_visual_psi_step) {
        app->gauge.boost_psi_visual = prev_visual_psi + max_visual_psi_step;
    } else if (visual_dpsi < -max_visual_psi_step) {
        app->gauge.boost_psi_visual = prev_visual_psi - max_visual_psi_step;
    }

    app->gauge.needle_frame_last_ms = now;

    float needle_angle_deg = psi_to_angle_rad(app->gauge.boost_psi_visual) * 180.0f / (float)M_PI;
    bool redraw_needed = isnan(app->gauge.needle_last_drawn_angle_deg) ||
                         fabsf(needle_angle_deg - app->gauge.needle_last_drawn_angle_deg) >= AppConfig::NEEDLE_REDRAW_THRESHOLD_DEG;

    if ((now - app->gauge.psi_label_last_ms) >= AppConfig::PSI_LABEL_UPDATE_MS) {
        app->gauge.psi_label_last_ms = now;
        char psi_buf[16];
        snprintf(psi_buf, sizeof(psi_buf), "%.1f", app->can.boost_psi);
        lv_label_set_text(app->gauge.psi_value_label, psi_buf);
    }

    if (redraw_needed) {
        redraw_gauge_needle(app);
        app->gauge.needle_last_drawn_angle_deg = needle_angle_deg;
    }
}

lv_obj_t *create_gauge_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->gauge.screen = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *face = lv_obj_create(screen);
    lv_obj_set_size(face, AppConfig::RADIUS * 2, AppConfig::RADIUS * 2);
    lv_obj_center(face);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_pad_all(face, 0, 0);

    app->gauge.gauge_bg_buf = (lv_color_t *)heap_caps_malloc(
        AppConfig::LCD_WIDTH * AppConfig::LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_obj_t *bg_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(bg_canvas, app->gauge.gauge_bg_buf,
                         AppConfig::LCD_WIDTH, AppConfig::LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(bg_canvas, 0, 0);
    lv_canvas_fill_bg(bg_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    for (int psi = (int)AppConfig::BOOST_MIN; psi <= (int)AppConfig::BOOST_MAX; psi++) {
        float angle = psi_to_angle_rad((float)psi);
        bool major = (psi % 5 == 0);

        int outer_r = 218;
        int inner_r = major ? 182 : 196;
        int tick_w = major ? 5 : 2;

        int x1 = AppConfig::CX + (int)(cos(angle) * inner_r);
        int y1 = AppConfig::CY + (int)(sin(angle) * inner_r);
        int x2 = AppConfig::CX + (int)(cos(angle) * outer_r);
        int y2 = AppConfig::CY + (int)(sin(angle) * outer_r);

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = lv_color_hex(0xFFFFFF);
        dsc.width = tick_w;
        dsc.opa = LV_OPA_COVER;
        lv_point_t pts[] = {{(lv_coord_t)x1, (lv_coord_t)y1}, {(lv_coord_t)x2, (lv_coord_t)y2}};
        lv_canvas_draw_line(bg_canvas, pts, 2, &dsc);

        if (major) {
            lv_obj_t *num = lv_label_create(screen);
            lv_obj_set_style_text_color(num, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_40, 0);

            char nbuf[8];
            snprintf(nbuf, sizeof(nbuf), "%d", psi);
            lv_label_set_text(num, nbuf);
            lv_obj_update_layout(num);

            int label_r = 154;
            int nx = AppConfig::CX + (int)(cos(angle) * label_r);
            int ny = AppConfig::CY + (int)(sin(angle) * label_r);
            lv_obj_set_pos(num, nx - lv_obj_get_width(num) / 2, ny - lv_obj_get_height(num) / 2);
        }
    }

    app->gauge.psi_value_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->gauge.psi_value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->gauge.psi_value_label, &lv_font_montserrat_48, 0);
    lv_obj_align(app->gauge.psi_value_label, LV_ALIGN_BOTTOM_MID, 0, -72);
    lv_label_set_text(app->gauge.psi_value_label, "0.0");

    app->gauge.psi_unit_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->gauge.psi_unit_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->gauge.psi_unit_label, &lv_font_montserrat_32, 0);
    lv_obj_align(app->gauge.psi_unit_label, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text(app->gauge.psi_unit_label, "PSI");

    app->gauge.needle_pivot_x_local = AppConfig::NEEDLE_CANVAS_SIZE / 2;
    app->gauge.needle_pivot_y_local = AppConfig::NEEDLE_CANVAS_SIZE / 2;
    app->gauge.needle_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(app->gauge.needle_obj);
    lv_obj_set_size(app->gauge.needle_obj, AppConfig::NEEDLE_CANVAS_SIZE, AppConfig::NEEDLE_CANVAS_SIZE);
    lv_obj_set_pos(app->gauge.needle_obj,
                   AppConfig::CX - app->gauge.needle_pivot_x_local,
                   AppConfig::CY - app->gauge.needle_pivot_y_local);
    lv_obj_add_flag(app->gauge.needle_obj, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(app->gauge.needle_obj, draw_needle_event, LV_EVENT_DRAW_MAIN, app);

    app->gauge.needle_pivot = lv_obj_create(screen);
    lv_obj_set_size(app->gauge.needle_pivot, 40, 40);
    lv_obj_set_pos(app->gauge.needle_pivot, AppConfig::CX - 20, AppConfig::CY - 20);
    lv_obj_set_style_radius(app->gauge.needle_pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(app->gauge.needle_pivot, lv_color_hex(0xFF1020), 0);
    lv_obj_set_style_border_width(app->gauge.needle_pivot, 0, 0);
    lv_obj_set_style_pad_all(app->gauge.needle_pivot, 0, 0);

    app->gauge.perf_label = lv_label_create(screen);
    lv_obj_set_style_text_color(app->gauge.perf_label, lv_color_hex(0x7f7f7f), 0);
    lv_obj_set_style_text_font(app->gauge.perf_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(app->gauge.perf_label, 220);
    lv_obj_set_style_text_align(app->gauge.perf_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app->gauge.perf_label, LV_ALIGN_TOP_MID, 0, 105);
    lv_label_set_text(app->gauge.perf_label, "profiling...");
    lv_obj_add_flag(app->gauge.perf_label, LV_OBJ_FLAG_HIDDEN);

    uint32_t now = lv_tick_get();
    app->gauge.boost_psi_visual = app->can.boost_psi;
    app->gauge.needle_frame_last_ms = now;
    app->gauge.needle_last_drawn_angle_deg = NAN;
    app->gauge.psi_label_last_ms = now;
    app->gauge.needle_last_area_valid = false;
    app->gauge.perf_window_start_ms = now;
    app->gauge.flush_count_accum = 0;
    app->gauge.needle_invalidate_accum = 0;
    app->gauge.needle_paint_accum = 0;
    app->gauge.flush_pixels_accum = 0;
    app->gauge.flush_us_accum = 0;

    redraw_gauge_needle(app);
    app->gauge.timer = lv_timer_create(tick_gauge, AppConfig::GAUGE_TICK_MS, app);
    app->gauge.perf_timer = lv_timer_create(update_perf_overlay, 1000, app);
    gauge_set_profiler_visible(app, false);

    return screen;
}