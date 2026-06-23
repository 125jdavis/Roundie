#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "Arduino_GFX_Library.h"
#include "lvgl.h"
#include "TouchDrvCSTXXX.hpp"

namespace AppConfig {
static constexpr int LCD_SDIO0 = 4;
static constexpr int LCD_SDIO1 = 5;
static constexpr int LCD_SDIO2 = 6;
static constexpr int LCD_SDIO3 = 7;
static constexpr int LCD_SCLK = 38;
static constexpr int LCD_CS = 12;
static constexpr int LCD_RESET = 39;
static constexpr int IIC_SDA = 15;
static constexpr int IIC_SCL = 14;
static constexpr int TP_INT = 11;
static constexpr int TP_RESET = 40;
static constexpr int LCD_WIDTH = 466;
static constexpr int LCD_HEIGHT = 466;
static constexpr int CX = 233;
static constexpr int CY = 233;
static constexpr int RADIUS = 220;

static constexpr float BOOST_MIN = -15.0f;
static constexpr float BOOST_MAX = 30.0f;
static constexpr float BOOST_SWEEP_DEG = 270.0f;
static constexpr float BOOST_START_DEG = 135.0f;
static constexpr float BOOST_RANGE = BOOST_MAX - BOOST_MIN;
static constexpr int NEEDLE_CANVAS_SIZE = 360;
static constexpr uint32_t GAUGE_TICK_MS = 5;
static constexpr float NEEDLE_REDRAW_THRESHOLD_DEG = 0.16f;
static constexpr uint32_t PSI_LABEL_UPDATE_MS = 150;
static constexpr float ANTI_TEAR_SOFT_DPS = 60.0f;
static constexpr float ANTI_TEAR_HARD_DPS = 170.0f;
static constexpr float ANTI_TEAR_MAX_VISUAL_DPS = 95.0f;
static constexpr float ANTI_TEAR_TAU_LOW_MS = 18.0f;
static constexpr float ANTI_TEAR_TAU_HIGH_MS = 44.0f;
static constexpr uint32_t DOUBLE_TAP_GAP_MS = 350;
static constexpr uint32_t TAP_MAX_DURATION_MS = 280;
static constexpr int TAP_MAX_MOVE_PX = 18;
static constexpr int DOUBLE_TAP_MAX_DIST_PX = 36;
static constexpr uint32_t BOOST_CAN_TIMEOUT_MS = 500;
static constexpr float ATMOSPHERIC_KPA = 101.325f;
static constexpr float KPA_TO_PSI = 0.1450377f;
static constexpr float STOICH_AFR = 14.7f;
static constexpr float AFR_MIN = 10.0f;
static constexpr float AFR_MAX = 20.0f;
static constexpr float BOOST_ARC_START_DEG = 180.0f;
static constexpr float BOOST_ARC_END_DEG = 0.0f;
static constexpr float AFR_ARC_START_DEG = 37.5f;
static constexpr float AFR_ARC_END_DEG = 142.5f;
static constexpr uint32_t BOOSTAFR_LABEL_TICK_MS = 143;
static constexpr uint32_t BOOSTAFR_ARC_TICK_MS = 16;
static constexpr uint32_t BOOSTAFR_DEMO_BOOST_PERIOD_MS = 2600;
static constexpr uint32_t BOOSTAFR_DEMO_AFR_PERIOD_MS = 3200;
static constexpr float BOOSTAFR_MAP_TAU_MS = 60.0f;
static constexpr float BOOSTAFR_AFR_TAU_MS = 90.0f;
static constexpr uint32_t ALERT_HOLD_MS = 400;
static constexpr float BOOST_ALERT_KPA_DELTA = 20.0f;
static constexpr float LAMBDA_LEAN_RATIO = 1.15f;
static constexpr gpio_num_t CAN_TX_GPIO = GPIO_NUM_44;
static constexpr gpio_num_t CAN_RX_GPIO = GPIO_NUM_43;
static constexpr uint32_t CAN_AUTOSCAN_INTERVAL_MS = 2500;
static constexpr bool CAN_AUTOSCAN_ENABLED = true;
static constexpr uint32_t BOOT_SPLASH_PLAY_MS = 2000;
static constexpr uint32_t BOOT_SPLASH_HOLD_MS = 1500;
static constexpr uint32_t BOOT_SPLASH_MS = BOOT_SPLASH_PLAY_MS + BOOT_SPLASH_HOLD_MS;
}

enum DemoScreen : uint8_t {
    DEMO_GAUGE = 0,
    DEMO_BOOSTAFR = 1,
    DEMO_DATA = 2,
    DEMO_WATCH = 3,
    DEMO_CANDBG = 4,
    DEMO_SCREEN_COUNT = 5
};

enum CanBitrateProfile : uint8_t {
    CAN_RATE_1M = 0,
    CAN_RATE_500K,
    CAN_RATE_250K,
    CAN_RATE_COUNT
};

struct HaltechData {
    uint16_t rpm = 0;
    float map_kpa_abs = 0.0f;
    float tps_pct = 0.0f;
    float fuel_press_kpa = 0.0f;
    float oil_press_kpa = 0.0f;
    float lambda1 = 0.0f;
    float lambda2 = 0.0f;
    float wheel_speed_kph = 0.0f;
    int16_t gear = 0;
    float target_boost_kpa = 0.0f;
    float coolant_temp_c = 0.0f;
    float air_temp_c = 0.0f;
    float fuel_temp_c = 0.0f;
    float oil_temp_c = 0.0f;
    float target_lambda = 0.0f;
    uint32_t last_0x360_ms = 0;
    uint32_t last_0x361_ms = 0;
    uint32_t last_0x368_ms = 0;
    uint32_t last_0x370_ms = 0;
    uint32_t last_0x372_ms = 0;
    uint32_t last_0x3E0_ms = 0;
    uint32_t last_0x3E9_ms = 0;
};

struct PlatformState {
    Arduino_DataBus *bus = nullptr;
    Arduino_CO5300 *gfx = nullptr;
    lv_disp_draw_buf_t draw_buf = {};
    lv_color_t *buf1 = nullptr;
    lv_color_t *buf2 = nullptr;
    lv_indev_t *touch_indev = nullptr;
    Preferences prefs;
    TouchDrvCST92xx touch;
    int16_t tx[5] = {};
    int16_t ty[5] = {};
    char lvgl_buffer_mode[64] = {};
    esp_timer_handle_t tick_timer = nullptr;
};

struct WatchScreenState {
    lv_obj_t *screen = nullptr;
    int clk_hour = 10;
    int clk_min = 10;
    int clk_sec = 0;
    int clk_day = 20;
    int clk_month = 3;
    int clk_year = 2026;
    lv_obj_t *hand_canvas = nullptr;
    lv_color_t *canvas_buf = nullptr;
    lv_obj_t *date_label = nullptr;
    lv_timer_t *timer = nullptr;
};

struct GaugeScreenState {
    lv_obj_t *screen = nullptr;
    lv_color_t *gauge_bg_buf = nullptr;
    lv_obj_t *psi_value_label = nullptr;
    lv_obj_t *psi_unit_label = nullptr;
    lv_timer_t *timer = nullptr;
    lv_timer_t *perf_timer = nullptr;
    lv_obj_t *needle_obj = nullptr;
    lv_obj_t *needle_pivot = nullptr;
    lv_obj_t *perf_label = nullptr;
    lv_coord_t needle_pivot_x_local = 0;
    lv_coord_t needle_pivot_y_local = 0;
    uint32_t psi_label_last_ms = 0;
    uint32_t needle_frame_last_ms = 0;
    float needle_last_drawn_angle_deg = NAN;
    bool needle_last_area_valid = false;
    lv_area_t needle_last_area = {};
    uint32_t perf_window_start_ms = 0;
    uint32_t flush_count_accum = 0;
    uint32_t needle_invalidate_accum = 0;
    uint32_t needle_paint_accum = 0;
    uint64_t flush_pixels_accum = 0;
    uint64_t flush_us_accum = 0;
    bool profiler_visible = false;
    float boost_psi_visual = 0.0f;
};

struct BoostAfrScreenState {
    lv_obj_t *screen = nullptr;
    lv_timer_t *arc_timer = nullptr;
    lv_timer_t *label_timer = nullptr;
    lv_obj_t *map_arc = nullptr;
    lv_obj_t *afr_arc = nullptr;
    lv_obj_t *psi_label = nullptr;
    lv_obj_t *afr_label = nullptr;
    lv_obj_t *psi_title_label = nullptr;
    lv_obj_t *afr_title_label = nullptr;
    lv_obj_t *afr_target_marker = nullptr;
    lv_obj_t *boost_target_marker = nullptr;
    bool demo_mode = false;
    uint32_t demo_start_ms = 0;
    float map_visual_psi = 0.0f;
    float afr_visual = 14.7f;
    uint32_t visual_last_ms = 0;
    bool visual_initialized = false;
    uint32_t boost_over_target_start_ms = 0;
    uint32_t lambda_lean_start_ms = 0;
    bool boost_over_target_alert = false;
    bool lambda_lean_alert = false;
};

struct DataScreenState {
    lv_obj_t *screen = nullptr;
    lv_timer_t *timer = nullptr;
    lv_obj_t *rpm_val_label = nullptr;
    lv_obj_t *tps_val_label = nullptr;
    lv_obj_t *coolant_val_label = nullptr;
    lv_obj_t *speed_val_label = nullptr;
    lv_obj_t *fuelpsi_val_label = nullptr;
};

struct CanDebugScreenState {
    lv_obj_t *screen = nullptr;
    lv_timer_t *timer = nullptr;
    lv_obj_t *fps_label = nullptr;
    lv_obj_t *state_label = nullptr;
    lv_obj_t *ids_label = nullptr;
};

struct ScreenSlot {
    lv_obj_t *screen = nullptr;
    lv_timer_t *timers[3] = {};
    uint8_t timer_count = 0;
};

struct NavigationState {
    ScreenSlot slots[DEMO_SCREEN_COUNT] = {};
    lv_obj_t *splash_screen = nullptr;
    lv_timer_t *splash_timer = nullptr;
    uint8_t splash_target_screen = 0;
    bool splash_countdown_synced = false;
    bool touch_down = false;
    int touch_start_x = 0;
    int touch_start_y = 0;
    int touch_last_x = 0;
    int touch_last_y = 0;
    uint32_t touch_start_ms = 0;
    uint32_t last_tap_ms = 0;
    int last_tap_x = 0;
    int last_tap_y = 0;
    int pending_screen = 0;
    bool pending_screen_change = false;
    DemoScreen current_demo = DEMO_GAUGE;
};

struct CanState {
    bool twai_ready = false;
    uint32_t boost_can_last_valid_ms = 0;
    CanBitrateProfile rate_profile = CAN_RATE_1M;
    bool driver_installed = false;
    uint32_t last_bus_error_count = 0;
    uint32_t last_autoscan_ms = 0;
    volatile uint32_t frames_since_last_sample = 0;
    float fps_display = 0.0f;
    float boost_psi = 0.0f;
    HaltechData ht = {};
};

struct AppContext {
    PlatformState platform;
    NavigationState nav;
    CanState can;
    WatchScreenState watch;
    GaugeScreenState gauge;
    BoostAfrScreenState boostafr;
    DataScreenState data;
    CanDebugScreenState candbg;
};

inline float clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline float clamp_boost_psi(float psi) {
    return clampf(psi, AppConfig::BOOST_MIN, AppConfig::BOOST_MAX);
}

inline float arc_angle_for_t(float start_deg, float end_deg, float t) {
    t = clampf(t, 0.0f, 1.0f);
    float span = end_deg - start_deg;
    if (span < 0.0f) span += 360.0f;
    float angle = start_deg + span * t;
    while (angle >= 360.0f) angle -= 360.0f;
    while (angle < 0.0f) angle += 360.0f;
    return angle;
}

LV_FONT_DECLARE(lv_font_montserrat_64);
LV_FONT_DECLARE(lv_font_montserrat_72);
LV_FONT_DECLARE(lv_font_montserrat_medium_60);
LV_FONT_DECLARE(lv_font_montserrat_medium_72);
LV_IMG_DECLARE(supra_light_sweep_v3_gif);
lv_obj_t *lv_gif_create(lv_obj_t *parent);
void lv_gif_set_src(lv_obj_t *obj, const void *src);