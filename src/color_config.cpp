#include "color_config.h"

#include <math.h>

#include "altboost_combo_screen.h"
#include "alternate_boost_screen.h"
#include "boostafr_screen.h"
#include "gauge_screen.h"

namespace {

static constexpr uint8_t PICKER_WHEEL_HUE_BINS = 22;
static constexpr int PICKER_WHEEL_SIZE = 466;
static constexpr int PICKER_WHEEL_OUTER_RADIUS = 238;
static constexpr int PICKER_WHEEL_INNER_RADIUS = 150;
static constexpr lv_coord_t PICKER_CENTER_Y = 0;
static constexpr int PICKER_POINTER_LENGTH = 24;
static constexpr float PICKER_POINTER_HALF_ANGLE_DEG = 5.4f;
static constexpr int PICKER_CENTER_ACTION_SIZE = 84;
static constexpr int PICKER_CENTER_HUB_SIZE = 300;
static constexpr int PICKER_CENTER_CHECK_Y = -82;
static constexpr int PICKER_CENTER_LABEL_Y = 0;
static constexpr int PICKER_CENTER_CANCEL_Y = 82;
// Logical hue order around the wheel (red -> orange -> yellow -> green -> teal -> blue -> purple -> pink -> white)
static const uint32_t kWheelPaletteHex[PICKER_WHEEL_HUE_BINS] = {
    0xE31212,  // deep red
    0xFF2F14,  // red orange
    0xF54927,  // deep orange
    0xF08205,  // orange
    0xF5B027,  // yellow orange
    0xFFCA29,  // yellow orange
    0xF5DD27,  // yellow
    0xE4F005,  // yellow
    0xB5FC0F,  // yellow green
    0xB8F257,  // lime green
    0xABF779,  // light lime
    0x22CC1F,  // green
    0x27F5B0,  // teal
    0x2DB595,  // sea green
    0x21C297,  // dark teal
    0x256C8E,  // teal blue
    0x2568DB,  // blue
    0x65C6F7,  // light blue
    0x7AB8F5,  // light blue
    0x8F37FA,  // purple
    0xF02E99,  // neon lavender
    0xFFFFFF,  // white
};
static lv_obj_t *g_picker_check_line_a = nullptr;
static lv_obj_t *g_picker_check_line_b = nullptr;
static lv_point_t g_picker_check_pts_a[2] = {{18, 38}, {33, 53}};
static lv_point_t g_picker_check_pts_b[2] = {{33, 53}, {57, 21}};
static lv_point_t g_picker_x_pts_a[2] = {{20, 18}, {58, 56}};
static lv_point_t g_picker_x_pts_b[2] = {{58, 18}, {20, 56}};
static lv_point_t g_pointer_tip = {0, 0};
static lv_point_t g_pointer_base_left = {0, 0};
static lv_point_t g_pointer_base_right = {0, 0};

static void build_picker_screen(AppContext *app);
static void draw_discrete_wheel_event(lv_event_t *event);
static void draw_pointer_event(lv_event_t *event);
static void discrete_wheel_interaction_event(lv_event_t *event);
static void sync_picker_preview_title_color(AppContext *app);
static void sync_picker_pointer(AppContext *app);
static void commit_picker_color(AppContext *app);
static void picker_check_interaction_event(lv_event_t *event);
static void picker_cancel_interaction_event(lv_event_t *event);
static void picker_screen_gesture_event(lv_event_t *event);

static const char *kParameterNames[COLOR_PARAMETER_COUNT] = {
    "PRIMARY TEXT COLOR",
    "SECONDARY TEXT COLOR",
    "GAUGE NEEDLE COLOR",
    "GAUGE TICK COLOR",
    "BAR GAUGE COLOR 1",
    "BAR GAUGE COLOR 2",
    "SHIFT LIGHT COLOR",
};

static const char *kThemePrefKeys[COLOR_PARAMETER_COUNT] = {
    "clr_pri",
    "clr_sec",
    "clr_need",
    "clr_tick",
    "clr_bar1",
    "clr_bar2",
    "clr_shift",
};

static ThemeState default_theme() {
    ThemeState theme;
    return theme;
}

static uint32_t color_to_hex24(lv_color_t color) {
    return lv_color_to32(color) & 0x00FFFFFFUL;
}

static lv_color_t wheel_band_color(uint8_t hue_bin) {
    if (hue_bin >= PICKER_WHEEL_HUE_BINS) hue_bin = PICKER_WHEEL_HUE_BINS - 1;
    return lv_color_hex(kWheelPaletteHex[hue_bin]);
}

static uint8_t nearest_wheel_bin(uint32_t color_hex) {
    int r = (int)((color_hex >> 16) & 0xFF);
    int g = (int)((color_hex >> 8) & 0xFF);
    int b = (int)(color_hex & 0xFF);

    uint8_t best_bin = 0;
    uint32_t best_dist = 0xFFFFFFFFUL;
    for (uint8_t i = 0; i < PICKER_WHEEL_HUE_BINS; i++) {
        uint32_t candidate = kWheelPaletteHex[i];
        int cr = (int)((candidate >> 16) & 0xFF);
        int cg = (int)((candidate >> 8) & 0xFF);
        int cb = (int)(candidate & 0xFF);
        int dr = r - cr;
        int dg = g - cg;
        int db = b - cb;
        uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);
        if (dist < best_dist) {
            best_dist = dist;
            best_bin = i;
        }
    }
    return best_bin;
}

static bool color_close_hex(uint32_t a, uint32_t b, uint8_t tol) {
    int ar = (int)((a >> 16) & 0xFF);
    int ag = (int)((a >> 8) & 0xFF);
    int ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF);
    int bg = (int)((b >> 8) & 0xFF);
    int bb = (int)(b & 0xFF);
    return abs(ar - br) <= tol && abs(ag - bg) <= tol && abs(ab - bb) <= tol;
}

static bool is_primary_candidate(uint32_t hex, const ThemeState &old_theme) {
    return color_close_hex(hex, 0xFFFFFFUL, 10) ||
           color_close_hex(hex, old_theme.colors[COLOR_PRIMARY_TEXT], 10) ||
           color_close_hex(hex, 0xBFC7CFUL, 12);
}

static bool is_secondary_candidate(uint32_t hex, const ThemeState &old_theme) {
    return color_close_hex(hex, 0x7AB8F5UL, 16) ||
           color_close_hex(hex, 0x10B8FFUL, 16) ||
           color_close_hex(hex, old_theme.colors[COLOR_SECONDARY_TEXT], 12);
}

static void apply_text_palette_recursive(lv_obj_t *obj, const ThemeState &old_theme, const ThemeState &new_theme) {
    if (!obj) return;

    lv_color_t text_color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    uint32_t text_hex = color_to_hex24(text_color);
    if (is_primary_candidate(text_hex, old_theme)) {
        lv_obj_set_style_text_color(obj, lv_color_hex(new_theme.colors[COLOR_PRIMARY_TEXT]), 0);
    } else if (is_secondary_candidate(text_hex, old_theme)) {
        lv_obj_set_style_text_color(obj, lv_color_hex(new_theme.colors[COLOR_SECONDARY_TEXT]), 0);
    }

    uint32_t child_count = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        apply_text_palette_recursive(lv_obj_get_child(obj, i), old_theme, new_theme);
    }
}

static lv_obj_t *current_demo_screen(AppContext *app) {
    if (!app) return nullptr;
    if (app->nav.current_demo >= DEMO_SCREEN_COUNT) return nullptr;
    return app->nav.slots[app->nav.current_demo].screen;
}

static bool wheel_color_for_point(lv_obj_t *wheel, const lv_point_t &p, lv_color_t *out_color) {
    if (!wheel || !out_color) return false;

    int cx = AppConfig::CX;
    int cy = AppConfig::CY;

    int dx = p.x - cx;
    int dy = p.y - cy;
    float r = sqrtf((float)(dx * dx + dy * dy));
    if (r < (float)PICKER_WHEEL_INNER_RADIUS || r > (float)PICKER_WHEEL_OUTER_RADIUS) return false;

    float angle = atan2f((float)dy, (float)dx) * 180.0f / (float)M_PI;
    if (angle < 0.0f) angle += 360.0f;

    float hue_step = 360.0f / (float)PICKER_WHEEL_HUE_BINS;
    int hue_bin = (int)floorf(angle / hue_step);
    if (hue_bin < 0) hue_bin = 0;
    if (hue_bin >= PICKER_WHEEL_HUE_BINS) hue_bin = PICKER_WHEEL_HUE_BINS - 1;

    *out_color = wheel_band_color((uint8_t)hue_bin);
    return true;
}

static void draw_discrete_wheel_event(lv_event_t *event) {
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);

    lv_point_t center = {(lv_coord_t)AppConfig::CX, (lv_coord_t)AppConfig::CY};

    float hue_step = 360.0f / (float)PICKER_WHEEL_HUE_BINS;
    int radius = (PICKER_WHEEL_OUTER_RADIUS + PICKER_WHEEL_INNER_RADIUS) / 2;
    int width = PICKER_WHEEL_OUTER_RADIUS - PICKER_WHEEL_INNER_RADIUS;

    for (uint8_t h = 0; h < PICKER_WHEEL_HUE_BINS; h++) {
        lv_color_t c = wheel_band_color(h);

        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.color = c;
        arc.width = (lv_coord_t)width;
        arc.opa = LV_OPA_COVER;
        arc.rounded = 0;

        uint16_t start = (uint16_t)lroundf((float)h * hue_step);
        uint16_t end = (uint16_t)lroundf((float)(h + 1) * hue_step);
        lv_draw_arc(draw_ctx, &arc, &center, (uint16_t)radius, start, end);
    }
}

static void discrete_wheel_interaction_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;

    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_CLICKED) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_color_t selected;
    if (!wheel_color_for_point(app->color_cfg.picker_colorwheel, p, &selected)) return;

    // Live preview while finger moves across the wheel.
    app->color_cfg.working_color = color_to_hex24(selected);
    sync_picker_preview_title_color(app);
    sync_picker_pointer(app);
}

static void sync_picker_preview_title_color(AppContext *app) {
    if (!app) return;
    // Label text stays white; only tint the check mark with the selected color.
    if (g_picker_check_line_a) {
        lv_obj_set_style_line_color(g_picker_check_line_a, lv_color_hex(app->color_cfg.working_color), 0);
    }
    if (g_picker_check_line_b) {
        lv_obj_set_style_line_color(g_picker_check_line_b, lv_color_hex(app->color_cfg.working_color), 0);
    }
}

static void sync_picker_pointer(AppContext *app) {
    if (!app || !app->color_cfg.picker_colorwheel) return;

    int cx = AppConfig::CX;
    int cy = AppConfig::CY;

    uint8_t bin = nearest_wheel_bin(app->color_cfg.working_color);
    float step = 360.0f / (float)PICKER_WHEEL_HUE_BINS;
    float angle_deg = (float)bin * step + (step * 0.5f);
    float tip_rad = angle_deg * (float)M_PI / 180.0f;
    float base_left_rad = (angle_deg - PICKER_POINTER_HALF_ANGLE_DEG) * (float)M_PI / 180.0f;
    float base_right_rad = (angle_deg + PICKER_POINTER_HALF_ANGLE_DEG) * (float)M_PI / 180.0f;

    // Equilateral triangle pointing outward: base at inner edge, tip at calibrated length.
    int base_r = PICKER_WHEEL_INNER_RADIUS;
    int tip_r = base_r + PICKER_POINTER_LENGTH;

    g_pointer_tip.x = (lv_coord_t)lroundf((float)cx + cosf(tip_rad) * (float)tip_r);
    g_pointer_tip.y = (lv_coord_t)lroundf((float)cy + sinf(tip_rad) * (float)tip_r);
    g_pointer_base_left.x = (lv_coord_t)lroundf((float)cx + cosf(base_left_rad) * (float)base_r);
    g_pointer_base_left.y = (lv_coord_t)lroundf((float)cy + sinf(base_left_rad) * (float)base_r);
    g_pointer_base_right.x = (lv_coord_t)lroundf((float)cx + cosf(base_right_rad) * (float)base_r);
    g_pointer_base_right.y = (lv_coord_t)lroundf((float)cy + sinf(base_right_rad) * (float)base_r);

    lv_obj_invalidate(app->color_cfg.picker_colorwheel);
}

static void draw_pointer_event(lv_event_t *event) {
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);

    lv_point_t tri[3] = {g_pointer_tip, g_pointer_base_left, g_pointer_base_right};

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0x9C9C9C);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;

    lv_draw_polygon(draw_ctx, &dsc, tri, 3);
}

static void commit_picker_color(AppContext *app) {
    if (!app) return;

    app->theme.colors[app->color_cfg.active_parameter] = app->color_cfg.working_color;
    theme_save(app);
    theme_apply_runtime(app);

    app->color_cfg.visible = true;
    app->color_cfg.picker_visible = false;
    lv_scr_load(app->color_cfg.list_screen);
}

static void picker_check_interaction_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    commit_picker_color(app);
}

static void picker_cancel_interaction_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    app->color_cfg.visible = true;
    app->color_cfg.picker_visible = false;
    lv_scr_load(app->color_cfg.list_screen);
}

static void picker_screen_gesture_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        app->color_cfg.visible = true;
        app->color_cfg.picker_visible = false;
        lv_scr_load(app->color_cfg.list_screen);
    }
}

static void color_config_open_picker(AppContext *app, ColorParameter parameter) {
    if (!app->color_cfg.picker_screen) {
        build_picker_screen(app);
    }

    app->color_cfg.active_parameter = parameter;
    app->color_cfg.working_color = kWheelPaletteHex[nearest_wheel_bin(app->theme.colors[parameter])];

    if (app->color_cfg.picker_title) {
        lv_label_set_text(app->color_cfg.picker_title, kParameterNames[parameter]);
    }
    sync_picker_preview_title_color(app);
    sync_picker_pointer(app);

    if (app->color_cfg.picker_colorwheel) {
        lv_obj_invalidate(app->color_cfg.picker_colorwheel);
    }

    app->color_cfg.visible = true;
    app->color_cfg.picker_visible = true;
    lv_scr_load(app->color_cfg.picker_screen);
}

static void parameter_item_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app || !app->color_cfg.list_container) return;

    lv_obj_t *target = lv_event_get_target(event);
    uint32_t child_count = lv_obj_get_child_cnt(app->color_cfg.list_container);
    for (uint32_t i = 0; i < child_count && i < COLOR_PARAMETER_COUNT; i++) {
        if (lv_obj_get_child(app->color_cfg.list_container, i) == target) {
            color_config_open_picker(app, (ColorParameter)i);
            return;
        }
    }
}

static void reset_defaults_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;

    theme_reset_defaults(app);
    theme_save(app);
    theme_apply_runtime(app);
}

static void exit_config_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    color_config_close(app);
}

static void select_color_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;

    app->theme.colors[app->color_cfg.active_parameter] = app->color_cfg.working_color;
    theme_save(app);
    theme_apply_runtime(app);

    app->color_cfg.visible = true;
    app->color_cfg.picker_visible = false;
    lv_scr_load(app->color_cfg.list_screen);
}

static void cancel_picker_event(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    if (!app) return;

    app->color_cfg.visible = true;
    app->color_cfg.picker_visible = false;
    lv_scr_load(app->color_cfg.list_screen);
}

static lv_obj_t *make_list_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, AppContext *app, uint32_t bg_hex) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 352, 66);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, app);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void build_list_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->color_cfg.list_screen = screen;

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_40, 0);
    lv_label_set_text(title, "Configuration");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t *list = lv_obj_create(screen);
    app->color_cfg.list_container = list;
    lv_obj_set_size(list, 388, 356);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 18, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    for (uint8_t i = 0; i < COLOR_PARAMETER_COUNT; i++) {
        lv_obj_t *btn = make_list_button(list, kParameterNames[i], parameter_item_event, app, 0x1e4d7a);
        (void)btn;
    }

    lv_obj_t *reset_btn = make_list_button(list, "DEFAULT COLORS", reset_defaults_event, app, 0x2a5c3d);

    lv_obj_t *exit_btn = make_list_button(list, "EXIT", exit_config_event, app, 0x5b3035);
}

static void build_picker_screen(AppContext *app) {
    lv_obj_t *screen = lv_obj_create(NULL);
    app->color_cfg.picker_screen = screen;

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_add_event_cb(screen, picker_screen_gesture_event, LV_EVENT_GESTURE, app);

    app->color_cfg.picker_colorwheel = lv_obj_create(screen);
    lv_obj_set_size(app->color_cfg.picker_colorwheel, PICKER_WHEEL_SIZE, PICKER_WHEEL_SIZE);
    lv_obj_set_pos(app->color_cfg.picker_colorwheel, 0, 0);
    lv_obj_clear_flag(app->color_cfg.picker_colorwheel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(app->color_cfg.picker_colorwheel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(app->color_cfg.picker_colorwheel, 0, 0);
    lv_obj_set_style_pad_all(app->color_cfg.picker_colorwheel, 0, 0);
    lv_obj_set_style_outline_width(app->color_cfg.picker_colorwheel, 0, 0);
    lv_obj_set_style_shadow_width(app->color_cfg.picker_colorwheel, 0, 0);
    lv_obj_add_flag(app->color_cfg.picker_colorwheel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(app->color_cfg.picker_colorwheel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(app->color_cfg.picker_colorwheel, draw_discrete_wheel_event, LV_EVENT_DRAW_MAIN, app);
    lv_obj_add_event_cb(app->color_cfg.picker_colorwheel, draw_pointer_event, LV_EVENT_DRAW_MAIN, app);
    lv_obj_add_event_cb(app->color_cfg.picker_colorwheel, discrete_wheel_interaction_event, LV_EVENT_ALL, app);

    lv_obj_t *center_hub = lv_obj_create(screen);
    lv_obj_set_size(center_hub, PICKER_CENTER_HUB_SIZE, PICKER_CENTER_HUB_SIZE);
    lv_obj_align(center_hub, LV_ALIGN_CENTER, 0, PICKER_CENTER_Y);
    lv_obj_set_style_radius(center_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_hub, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(center_hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_hub, 0, 0);
    lv_obj_clear_flag(center_hub, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *check_btn = lv_btn_create(screen);
    lv_obj_set_size(check_btn, PICKER_CENTER_ACTION_SIZE, PICKER_CENTER_ACTION_SIZE);
    lv_obj_align(check_btn, LV_ALIGN_CENTER, 0, PICKER_CENTER_CHECK_Y);
    lv_obj_set_style_radius(check_btn, 0, 0);
    lv_obj_set_style_bg_opa(check_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(check_btn, 0, 0);
    lv_obj_set_style_outline_width(check_btn, 0, 0);
    lv_obj_set_style_shadow_width(check_btn, 0, 0);
    lv_obj_set_style_pad_all(check_btn, 0, 0);
    lv_obj_clear_flag(check_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(check_btn, picker_check_interaction_event, LV_EVENT_CLICKED, app);

    g_picker_check_line_a = lv_line_create(check_btn);
    lv_line_set_points(g_picker_check_line_a, g_picker_check_pts_a, 2);
    lv_obj_set_style_line_width(g_picker_check_line_a, 8, 0);
    lv_obj_set_style_line_rounded(g_picker_check_line_a, true, 0);
    lv_obj_set_style_line_color(g_picker_check_line_a, lv_color_hex(0xFFFFFF), 0);

    g_picker_check_line_b = lv_line_create(check_btn);
    lv_line_set_points(g_picker_check_line_b, g_picker_check_pts_b, 2);
    lv_obj_set_style_line_width(g_picker_check_line_b, 8, 0);
    lv_obj_set_style_line_rounded(g_picker_check_line_b, true, 0);
    lv_obj_set_style_line_color(g_picker_check_line_b, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *cancel_btn = lv_btn_create(screen);
    lv_obj_set_size(cancel_btn, PICKER_CENTER_ACTION_SIZE, PICKER_CENTER_ACTION_SIZE);
    lv_obj_align(cancel_btn, LV_ALIGN_CENTER, 0, PICKER_CENTER_CANCEL_Y);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cancel_btn, 0, 0);
    lv_obj_set_style_outline_width(cancel_btn, 0, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);
    lv_obj_set_style_pad_all(cancel_btn, 0, 0);
    lv_obj_clear_flag(cancel_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cancel_btn, picker_cancel_interaction_event, LV_EVENT_CLICKED, app);

    lv_obj_t *x_line_a = lv_line_create(cancel_btn);
    lv_line_set_points(x_line_a, g_picker_x_pts_a, 2);
    lv_obj_set_style_line_width(x_line_a, 8, 0);
    lv_obj_set_style_line_rounded(x_line_a, true, 0);
    lv_obj_set_style_line_color(x_line_a, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *x_line_b = lv_line_create(cancel_btn);
    lv_line_set_points(x_line_b, g_picker_x_pts_b, 2);
    lv_obj_set_style_line_width(x_line_b, 8, 0);
    lv_obj_set_style_line_rounded(x_line_b, true, 0);
    lv_obj_set_style_line_color(x_line_b, lv_color_hex(0xFFFFFF), 0);

    app->color_cfg.picker_value_slider = nullptr;
    app->color_cfg.picker_preview = nullptr;

    app->color_cfg.picker_title = lv_label_create(screen);
    lv_obj_set_style_text_color(app->color_cfg.picker_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(app->color_cfg.picker_title, &lv_font_montserrat_24, 0);
    lv_obj_set_width(app->color_cfg.picker_title, 210);
    lv_obj_set_style_text_align(app->color_cfg.picker_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app->color_cfg.picker_title, LV_ALIGN_CENTER, 0, PICKER_CENTER_LABEL_Y);
    lv_label_set_long_mode(app->color_cfg.picker_title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(app->color_cfg.picker_title, kParameterNames[0]);
    sync_picker_preview_title_color(app);
    sync_picker_pointer(app);
}

}  // namespace

void theme_load(AppContext *app) {
    ThemeState defaults = default_theme();

    for (uint8_t i = 0; i < COLOR_PARAMETER_COUNT; i++) {
        uint32_t value = app->platform.prefs.getULong(kThemePrefKeys[i], defaults.colors[i]);
        app->theme.colors[i] = value & 0x00FFFFFFUL;
    }
}

void theme_save(AppContext *app) {
    for (uint8_t i = 0; i < COLOR_PARAMETER_COUNT; i++) {
        app->platform.prefs.putULong(kThemePrefKeys[i], app->theme.colors[i] & 0x00FFFFFFUL);
    }
}

void theme_reset_defaults(AppContext *app) {
    ThemeState defaults = default_theme();
    for (uint8_t i = 0; i < COLOR_PARAMETER_COUNT; i++) {
        app->theme.colors[i] = defaults.colors[i];
    }
}

void theme_apply_runtime(AppContext *app) {
    ThemeState old_theme = default_theme();
    if (app->color_cfg.has_last_applied_theme) {
        old_theme = app->color_cfg.last_applied_theme;
    }

    for (uint8_t i = 0; i < DEMO_SCREEN_COUNT; i++) {
        if (i == DEMO_GFORCE) continue;
        apply_text_palette_recursive(app->nav.slots[i].screen, old_theme, app->theme);
    }
    // G-Force config and calibration screens keep static white text

    if (app->boostafr.map_arc) {
        lv_obj_set_style_arc_color(app->boostafr.map_arc, app_bar_gauge_color2(app), LV_PART_INDICATOR);
    }
    if (app->boostafr.afr_arc) {
        lv_obj_set_style_arc_color(app->boostafr.afr_arc, app_bar_gauge_color1(app), LV_PART_INDICATOR);
    }
    if (app->alt_boost_combo.boost_arc) {
        lv_obj_set_style_arc_color(app->alt_boost_combo.boost_arc, app_bar_gauge_color2(app), LV_PART_MAIN);
        lv_obj_set_style_arc_color(app->alt_boost_combo.boost_arc, app_bar_gauge_color1(app), LV_PART_INDICATOR);
    }
    if (app->alt_boost_combo.lambda_arc) {
        lv_obj_set_style_arc_color(app->alt_boost_combo.lambda_arc, app_bar_gauge_color2(app), LV_PART_MAIN);
        lv_obj_set_style_arc_color(app->alt_boost_combo.lambda_arc, app_bar_gauge_color1(app), LV_PART_INDICATOR);
    }
    if (app->data.s4_inst_fe_arc) {
        lv_obj_set_style_bg_color(app->data.s4_inst_fe_arc, app_bar_gauge_color2(app), LV_PART_MAIN);
        lv_obj_set_style_bg_color(app->data.s4_inst_fe_arc, app_bar_gauge_color1(app), LV_PART_INDICATOR);
    }

    gauge_refresh_theme(app);
    altboost_refresh_theme(app);

    if (app->gauge.needle_obj) lv_obj_invalidate(app->gauge.needle_obj);
    if (app->gauge.needle_pivot) lv_obj_set_style_bg_color(app->gauge.needle_pivot, app_gauge_needle_color(app), 0);
    if (app->alt_boost.needle_obj) lv_obj_invalidate(app->alt_boost.needle_obj);
    if (app->alt_boost.needle_pivot) lv_obj_set_style_bg_color(app->alt_boost.needle_pivot, app_gauge_needle_color(app), 0);
    if (app->alt_boost.max_marker) lv_obj_invalidate(app->alt_boost.max_marker);
    if (app->boostafr.afr_target_marker) lv_obj_invalidate(app->boostafr.afr_target_marker);
    if (app->boostafr.boost_target_marker) lv_obj_invalidate(app->boostafr.boost_target_marker);
    if (app->alt_boost_combo.boost_target_marker) lv_obj_invalidate(app->alt_boost_combo.boost_target_marker);
    if (app->alt_boost_combo.lambda_target_marker) lv_obj_invalidate(app->alt_boost_combo.lambda_target_marker);
    if (app->alt_boost_combo.ghost_line_obj) lv_obj_invalidate(app->alt_boost_combo.ghost_line_obj);
    if (app->data.s4_trip_fe_marker) lv_obj_invalidate(app->data.s4_trip_fe_marker);

    app->color_cfg.last_applied_theme = app->theme;
    app->color_cfg.has_last_applied_theme = true;
}

void color_config_init(AppContext *app) {
    build_list_screen(app);
    app->color_cfg.visible = false;
    app->color_cfg.picker_visible = false;
}

void color_config_open(AppContext *app) {
    if (!app->color_cfg.list_screen) return;
    app->color_cfg.visible = true;
    app->color_cfg.picker_visible = false;
    lv_scr_load(app->color_cfg.list_screen);
}

void color_config_close(AppContext *app) {
    app->color_cfg.visible = false;
    app->color_cfg.picker_visible = false;

    lv_obj_t *target = current_demo_screen(app);
    if (target) {
        lv_scr_load(target);
    }
}

bool color_config_is_visible(const AppContext *app) {
    return app->color_cfg.visible;
}
