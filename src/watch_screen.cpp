#include "watch_screen.h"

#include <math.h>

static void draw_thick_line(lv_obj_t *canvas, int x1, int y1, int x2, int y2, lv_color_t color, int thickness) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = thickness;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.opa = LV_OPA_COVER;
    lv_point_t points[] = {{(lv_coord_t)x1, (lv_coord_t)y1}, {(lv_coord_t)x2, (lv_coord_t)y2}};
    lv_canvas_draw_line(canvas, points, 2, &dsc);
}

static void redraw_hands(AppContext *app) {
    lv_canvas_fill_bg(app->watch.hand_canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    float hour_angle = ((app->watch.clk_hour % 12) * 30.0f + app->watch.clk_min * 0.5f + app->watch.clk_sec * (0.5f / 60.0f) - 90.0f) * (float)M_PI / 180.0f;
    float min_angle = (app->watch.clk_min * 6.0f + app->watch.clk_sec * 0.1f - 90.0f) * (float)M_PI / 180.0f;
    float sec_angle = (app->watch.clk_sec * 6.0f - 90.0f) * (float)M_PI / 180.0f;

    int hx = AppConfig::CX + (int)(cos(hour_angle) * 80);
    int hy = AppConfig::CY + (int)(sin(hour_angle) * 80);
    int hxt = AppConfig::CX - (int)(cos(hour_angle) * 20);
    int hyt = AppConfig::CY - (int)(sin(hour_angle) * 20);
    draw_thick_line(app->watch.hand_canvas, hxt, hyt, hx, hy, lv_color_hex(0xffffff), 8);

    int mx = AppConfig::CX + (int)(cos(min_angle) * 120);
    int my = AppConfig::CY + (int)(sin(min_angle) * 120);
    int mxt = AppConfig::CX - (int)(cos(min_angle) * 20);
    int myt = AppConfig::CY - (int)(sin(min_angle) * 20);
    draw_thick_line(app->watch.hand_canvas, mxt, myt, mx, my, lv_color_hex(0xffffff), 6);

    int sx = AppConfig::CX + (int)(cos(sec_angle) * 140);
    int sy = AppConfig::CY + (int)(sin(sec_angle) * 140);
    int sxt = AppConfig::CX - (int)(cos(sec_angle) * 30);
    int syt = AppConfig::CY - (int)(sin(sec_angle) * 30);
    draw_thick_line(app->watch.hand_canvas, sxt, syt, sx, sy, lv_color_hex(0xff3333), 3);

    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.bg_color = lv_color_hex(0xB8960C);
    dot_dsc.bg_opa = LV_OPA_COVER;
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.border_width = 0;
    lv_canvas_draw_rect(app->watch.hand_canvas, AppConfig::CX - 7, AppConfig::CY - 7, 14, 14, &dot_dsc);
}

static void tick_clock(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;

    app->watch.clk_sec++;
    if (app->watch.clk_sec >= 60) {
        app->watch.clk_sec = 0;
        app->watch.clk_min++;
    }
    if (app->watch.clk_min >= 60) {
        app->watch.clk_min = 0;
        app->watch.clk_hour++;
    }
    if (app->watch.clk_hour >= 24) app->watch.clk_hour = 0;

    char date_buf[12];
    snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%04d", app->watch.clk_day, app->watch.clk_month, app->watch.clk_year);
    lv_label_set_text(app->watch.date_label, date_buf);
    redraw_hands(app);
}

lv_obj_t *create_watch_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->watch.screen = screen;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *bezel = lv_obj_create(screen);
    lv_obj_set_size(bezel, AppConfig::RADIUS * 2 + 10, AppConfig::RADIUS * 2 + 10);
    lv_obj_center(bezel);
    lv_obj_set_style_radius(bezel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bezel, lv_color_hex(0xB8960C), 0);
    lv_obj_set_style_border_width(bezel, 0, 0);
    lv_obj_set_style_pad_all(bezel, 0, 0);

    lv_obj_t *face = lv_obj_create(screen);
    lv_obj_set_size(face, AppConfig::RADIUS * 2, AppConfig::RADIUS * 2);
    lv_obj_center(face);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_pad_all(face, 0, 0);

    lv_color_t *marker_buf = (lv_color_t *)heap_caps_malloc(
        AppConfig::LCD_WIDTH * AppConfig::LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_obj_t *marker_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(marker_canvas, marker_buf,
                         AppConfig::LCD_WIDTH, AppConfig::LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(marker_canvas, 0, 0);
    lv_canvas_fill_bg(marker_canvas, lv_color_hex(0x0a0a1a), LV_OPA_COVER);

    for (int i = 0; i < 12; i++) {
        float angle = (i * 30.0f - 90.0f) * (float)M_PI / 180.0f;
        bool major = (i % 3 == 0);
        int inner = major ? AppConfig::RADIUS - 28 : AppConfig::RADIUS - 16;
        int outer_r = AppConfig::RADIUS - 6;
        int width = major ? 6 : 3;

        int x1 = AppConfig::CX + (int)(cos(angle) * inner);
        int y1 = AppConfig::CY + (int)(sin(angle) * inner);
        int x2 = AppConfig::CX + (int)(cos(angle) * outer_r);
        int y2 = AppConfig::CY + (int)(sin(angle) * outer_r);

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = major ? lv_color_hex(0xB8960C) : lv_color_hex(0x888888);
        dsc.width = width;
        dsc.opa = LV_OPA_COVER;
        lv_point_t pts[] = {{(lv_coord_t)x1, (lv_coord_t)y1}, {(lv_coord_t)x2, (lv_coord_t)y2}};
        lv_canvas_draw_line(marker_canvas, pts, 2, &dsc);
    }

    lv_obj_t *date_box = lv_obj_create(screen);
    lv_obj_set_size(date_box, 110, 32);
    lv_obj_set_pos(date_box, AppConfig::CX + 40, AppConfig::CY - 16);
    lv_obj_set_style_bg_color(date_box, lv_color_hex(0x1a1a3a), 0);
    lv_obj_set_style_border_color(date_box, lv_color_hex(0xB8960C), 0);
    lv_obj_set_style_border_width(date_box, 2, 0);
    lv_obj_set_style_radius(date_box, 4, 0);
    lv_obj_set_style_pad_all(date_box, 0, 0);

    app->watch.date_label = lv_label_create(date_box);
    lv_obj_set_style_text_color(app->watch.date_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(app->watch.date_label);

    app->watch.canvas_buf = (lv_color_t *)heap_caps_malloc(
        AppConfig::LCD_WIDTH * AppConfig::LCD_HEIGHT * sizeof(lv_color_t) * 2, MALLOC_CAP_SPIRAM);
    app->watch.hand_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(app->watch.hand_canvas, app->watch.canvas_buf,
                         AppConfig::LCD_WIDTH, AppConfig::LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_set_pos(app->watch.hand_canvas, 0, 0);

    char date_buf[12];
    snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%04d", app->watch.clk_day, app->watch.clk_month, app->watch.clk_year);
    lv_label_set_text(app->watch.date_label, date_buf);
    redraw_hands(app);

    app->watch.timer = lv_timer_create(tick_clock, 1000, app);
    return screen;
}