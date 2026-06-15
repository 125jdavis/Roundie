#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "Arduino_GFX_Library.h"
#include "lvgl.h"
#include "TouchDrvCSTXXX.hpp"

#define LCD_SDIO0   4
#define LCD_SDIO1   5
#define LCD_SDIO2   6
#define LCD_SDIO3   7
#define LCD_SCLK   38
#define LCD_CS     12
#define LCD_RESET  39
#define IIC_SDA    15
#define IIC_SCL    14
#define TP_INT     11
#define TP_RESET   40
#define LCD_WIDTH  466
#define LCD_HEIGHT 466

#define CX 233
#define CY 233
#define RADIUS 220

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, false, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;
static lv_indev_t *touch_indev;
static Preferences prefs;

TouchDrvCST92xx touch;
int16_t tx[5], ty[5];

// Clock state
static int clk_hour  = 10;
static int clk_min   = 10;
static int clk_sec   = 0;
static int clk_day   = 20;
static int clk_month = 3;
static int clk_year  = 2026;

// Canvases
static lv_obj_t *hand_canvas;
static lv_color_t *canvas_buf;
static lv_obj_t *date_label;
static lv_timer_t *watch_timer;

// Screens
static lv_obj_t *watch_scr;
static lv_obj_t *gauge_scr;
static lv_obj_t *boostafr_scr;
static lv_obj_t *splash_scr;
static lv_timer_t *splash_timer;
static uint8_t splash_target_screen = 0;
static bool splash_countdown_synced = false;

// Gauge objects
static lv_color_t *gauge_bg_buf;
static lv_obj_t *psi_value_label;
static lv_obj_t *psi_unit_label;
static lv_timer_t *gauge_timer;
static lv_timer_t *perf_timer;
static lv_obj_t *needle_obj;
static lv_obj_t *needle_pivot;
static lv_obj_t *perf_label;
static lv_coord_t needle_pivot_x_local;
static lv_coord_t needle_pivot_y_local;
static uint32_t psi_label_last_ms = 0;
static uint32_t needle_frame_last_ms = 0;
static float needle_last_drawn_angle_deg = NAN;
static bool needle_last_area_valid = false;
static lv_area_t needle_last_area;
static char lvgl_buffer_mode[64] = "unknown";

// Runtime performance counters
static uint32_t perf_window_start_ms = 0;
static uint32_t flush_count_accum = 0;
static uint32_t needle_invalidate_accum = 0;
static uint32_t needle_paint_accum = 0;
static uint64_t flush_pixels_accum = 0;
static uint64_t flush_us_accum = 0;
static bool profiler_visible = false;

// Gauge animation state
static float boost_psi = 0.0f;
static float boost_psi_visual = 0.0f;

// CAN state
static bool twai_ready = false;
static uint32_t boost_can_last_valid_ms = 0;

// Haltech V2 decoded CAN data (all fields zero-initialised)
struct HaltechData {
    // 0x360 @ 50 Hz
    uint16_t rpm;            // raw rpm
    float    map_kpa_abs;    // absolute manifold pressure, kPa
    float    tps_pct;        // throttle position, 0-100 %
    // 0x361 @ 50 Hz
    float    fuel_press_kpa; // gauge fuel pressure, kPa
    float    oil_press_kpa;  // gauge oil pressure, kPa
    // 0x368 @ 20 Hz
    float    lambda1;
    float    lambda2;
    // 0x370 @ 20 Hz
    float    wheel_speed_kph;
    int16_t  gear;
    // 0x372 @ 10 Hz
    float    target_boost_kpa;
    // 0x3E0 @ 5 Hz
    float    coolant_temp_c; // Celsius
    float    air_temp_c;
    float    fuel_temp_c;
    float    oil_temp_c;
    // 0x3E9 @ 20 Hz (target lambda bytes 4-5)
    float    target_lambda;
    // per-frame freshness timestamps
    uint32_t last_0x360_ms;
    uint32_t last_0x361_ms;
    uint32_t last_0x368_ms;
    uint32_t last_0x370_ms;
    uint32_t last_0x372_ms;
    uint32_t last_0x3E0_ms;
    uint32_t last_0x3E9_ms;
};
static HaltechData ht = {};

// Boost/AFR visual screen objects
static lv_timer_t *boostafr_arc_timer = nullptr;
static lv_timer_t *boostafr_label_timer = nullptr;
static lv_obj_t *boostafr_map_arc = nullptr;
static lv_obj_t *boostafr_afr_arc = nullptr;
static lv_obj_t *boostafr_psi_label = nullptr;
static lv_obj_t *boostafr_afr_label = nullptr;
static lv_obj_t *boostafr_psi_title_label = nullptr;
static lv_obj_t *boostafr_afr_title_label = nullptr;
static lv_obj_t *boostafr_afr_target_marker = nullptr;
static lv_obj_t *boostafr_boost_target_marker = nullptr;
static bool boostafr_demo_mode = false;
static uint32_t boostafr_demo_start_ms = 0;
static float boostafr_map_visual_psi = 0.0f;
static float boostafr_afr_visual = 14.7f;
static uint32_t boostafr_visual_last_ms = 0;
static bool boostafr_visual_initialized = false;
static uint32_t boost_over_target_start_ms = 0;
static uint32_t lambda_lean_start_ms = 0;
static bool boost_over_target_alert = false;
static bool lambda_lean_alert = false;

// Data screen objects
static lv_obj_t *data_scr      = nullptr;
static lv_timer_t *data_timer  = nullptr;
static lv_obj_t *data_rpm_val_label       = nullptr;
static lv_obj_t *data_tps_val_label       = nullptr;
static lv_obj_t *data_coolant_val_label   = nullptr;
static lv_obj_t *data_speed_val_label     = nullptr;
static lv_obj_t *data_fuelpsi_val_label   = nullptr;

// CAN debug screen objects
static lv_obj_t   *candbg_scr         = nullptr;
static lv_timer_t *candbg_timer       = nullptr;
static lv_obj_t   *candbg_fps_label   = nullptr;
static lv_obj_t   *candbg_state_label = nullptr;
static lv_obj_t   *candbg_ids_label   = nullptr;
// Frame counter incremented by the receive loop; sampled every 200 ms by tick_candbg.
static volatile uint32_t can_frames_since_last_sample = 0;
static float can_fps_display = 0.0f;

// Raw touch swipe state (independent of LVGL gesture events)
static bool touch_down = false;
static int touch_start_x = 0;
static int touch_start_y = 0;
static int touch_last_x = 0;
static int touch_last_y = 0;
static uint32_t touch_start_ms = 0;
static uint32_t last_tap_ms = 0;
static int last_tap_x = 0;
static int last_tap_y = 0;
static int pending_screen = 0;
static bool pending_screen_change = false;

enum DemoScreen {
    DEMO_GAUGE    = 0,
    DEMO_BOOSTAFR = 1,
    DEMO_DATA     = 2,
    DEMO_WATCH    = 3,
    DEMO_CANDBG   = 4,
    DEMO_SCREEN_COUNT = 5
};

static DemoScreen current_demo = DEMO_GAUGE;

void set_demo_screen(DemoScreen target, lv_scr_load_anim_t anim);

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
static constexpr float BOOST_ARC_START_DEG = 180.0f;  // 180-degree arc (top half)
static constexpr float BOOST_ARC_END_DEG = 0.0f;
static constexpr float AFR_ARC_START_DEG = 37.5f;     // 105-degree arc (bottom)
static constexpr float AFR_ARC_END_DEG = 142.5f;
static constexpr uint32_t BOOSTAFR_LABEL_TICK_MS = 143; // about 7 Hz
static constexpr uint32_t BOOSTAFR_ARC_TICK_MS = 16;    // smooth arc updates
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

enum CanBitrateProfile {
    CAN_RATE_1M = 0,
    CAN_RATE_500K,
    CAN_RATE_250K,
    CAN_RATE_COUNT
};

static CanBitrateProfile can_rate_profile = CAN_RATE_1M;
static bool twai_driver_installed = false;
static uint32_t can_last_bus_error_count = 0;
static uint32_t can_last_autoscan_ms = 0;

float psi_to_angle_rad(float psi);
void compute_needle_area(float psi, lv_area_t *area);
void set_profiler_visible(bool visible);
LV_FONT_DECLARE(lv_font_montserrat_64);
LV_FONT_DECLARE(lv_font_montserrat_72);
LV_FONT_DECLARE(lv_font_montserrat_medium_60);
LV_FONT_DECLARE(lv_font_montserrat_medium_72);
LV_IMG_DECLARE(supra_light_sweep_v3_gif);
lv_obj_t * lv_gif_create(lv_obj_t * parent);
void lv_gif_set_src(lv_obj_t * obj, const void * src);

static float clamp_boost_psi(float psi) {
    if (psi < BOOST_MIN) return BOOST_MIN;
    if (psi > BOOST_MAX) return BOOST_MAX;
    return psi;
}

static const char *can_rate_profile_text(CanBitrateProfile profile) {
    switch (profile) {
        case CAN_RATE_1M:   return "1M";
        case CAN_RATE_500K: return "500K";
        case CAN_RATE_250K: return "250K";
        default:            return "?";
    }
}

static twai_timing_config_t can_timing_config_for_profile(CanBitrateProfile profile) {
    switch (profile) {
        case CAN_RATE_1M:   return TWAI_TIMING_CONFIG_1MBITS();
        case CAN_RATE_500K: return TWAI_TIMING_CONFIG_500KBITS();
        case CAN_RATE_250K: return TWAI_TIMING_CONFIG_250KBITS();
        default:            return TWAI_TIMING_CONFIG_1MBITS();
    }
}

static inline uint16_t be16(const uint8_t *d) {
    return (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
}
static inline int16_t sbe16(const uint8_t *d) {
    return (int16_t)be16(d);
}
static inline float kelvin_to_c(float k) { return k - 273.15f; }

static float clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float arc_angle_for_t(float start_deg, float end_deg, float t) {
    t = clampf(t, 0.0f, 1.0f);
    float span = end_deg - start_deg;
    if (span < 0.0f) span += 360.0f;
    float angle = start_deg + span * t;
    while (angle >= 360.0f) angle -= 360.0f;
    while (angle < 0.0f) angle += 360.0f;
    return angle;
}

static float triangle_wave_01(float phase) {
    phase = phase - floorf(phase);
    if (phase < 0.5f) return phase * 2.0f;
    return (1.0f - phase) * 2.0f;
}

static void get_boostafr_demo_values(uint32_t now, float *map_psi, float *afr_actual) {
    float boost_phase = (float)((now - boostafr_demo_start_ms) % BOOSTAFR_DEMO_BOOST_PERIOD_MS) /
                        (float)BOOSTAFR_DEMO_BOOST_PERIOD_MS;
    float afr_phase = (float)((now - boostafr_demo_start_ms) % BOOSTAFR_DEMO_AFR_PERIOD_MS) /
                      (float)BOOSTAFR_DEMO_AFR_PERIOD_MS;

    float boost_t = triangle_wave_01(boost_phase);
    float afr_t = triangle_wave_01(afr_phase + 0.25f);

    *map_psi = BOOST_MIN + boost_t * (BOOST_MAX - BOOST_MIN);
    *afr_actual = AFR_MIN + afr_t * (AFR_MAX - AFR_MIN);
}

static bool font_has_glyph(const lv_font_t *font, uint32_t codepoint) {
    lv_font_glyph_dsc_t dsc;
    return lv_font_get_glyph_dsc(font, &dsc, codepoint, 0);
}

static void on_splash_gif_first_draw(lv_event_t *e) {
    LV_UNUSED(e);
    if (splash_countdown_synced || !splash_timer) return;

    splash_countdown_synced = true;
    lv_timer_reset(splash_timer);
}

static void tick_boot_splash(lv_timer_t *timer) {
    LV_UNUSED(timer);
    if (splash_timer) {
        lv_timer_del(splash_timer);
        splash_timer = nullptr;
    }

    current_demo = DEMO_SCREEN_COUNT;
    set_demo_screen((DemoScreen)splash_target_screen, LV_SCR_LOAD_ANIM_NONE);
}

// Decode one Haltech V2 frame into ht and update boost_psi target.
// Returns true if the frame updated the MAP/boost target.
static bool haltech_process_frame(const twai_message_t &msg, uint32_t now_ms) {
    if (msg.extd || msg.rtr) return false;

    switch (msg.identifier) {

        case 0x360: {
            // 0-1: RPM, 2-3: MAP 0.1kPa abs, 4-5: TPS 0.1%
            if (msg.data_length_code < 6) return false;
            ht.rpm         = be16(msg.data + 0);
            ht.map_kpa_abs = be16(msg.data + 2) * 0.1f;
            ht.tps_pct     = be16(msg.data + 4) * 0.1f;
            ht.last_0x360_ms = now_ms;
            return true;  // caller updates boost_psi
        }

        case 0x361: {
            // 0-1: Fuel Pressure 0.1kPa abs, 2-3: Oil Pressure 0.1kPa abs
            if (msg.data_length_code < 4) return false;
            ht.fuel_press_kpa = be16(msg.data + 0) * 0.1f - ATMOSPHERIC_KPA;
            ht.oil_press_kpa  = be16(msg.data + 2) * 0.1f - ATMOSPHERIC_KPA;
            ht.last_0x361_ms = now_ms;
            break;
        }

        case 0x368: {
            // 0-1: Lambda1 x0.001, 2-3: Lambda2 x0.001
            if (msg.data_length_code < 4) return false;
            ht.lambda1 = be16(msg.data + 0) * 0.001f;
            ht.lambda2 = be16(msg.data + 2) * 0.001f;
            ht.last_0x368_ms = now_ms;
            break;
        }

        case 0x370: {
            // 0-1: Wheel Speed 0.1 km/h, 2-3: Gear, 4-5: Intake Cam 1, 6-7: Intake Cam 2
            if (msg.data_length_code < 4) return false;
            ht.wheel_speed_kph = be16(msg.data + 0) * 0.1f;
            ht.gear            = sbe16(msg.data + 2);
            ht.last_0x370_ms = now_ms;
            break;
        }

        case 0x372: {
            // 4-5: Target Boost 0.1kPa
            if (msg.data_length_code < 6) return false;
            ht.target_boost_kpa = be16(msg.data + 4) * 0.1f;
            ht.last_0x372_ms = now_ms;
            break;
        }

        case 0x3E0: {
            // 0-1: Coolant 0.1K, 2-3: Air 0.1K, 4-5: Fuel 0.1K, 6-7: Oil 0.1K
            if (msg.data_length_code < 8) return false;
            ht.coolant_temp_c = kelvin_to_c(be16(msg.data + 0) * 0.1f);
            ht.air_temp_c     = kelvin_to_c(be16(msg.data + 2) * 0.1f);
            ht.fuel_temp_c    = kelvin_to_c(be16(msg.data + 4) * 0.1f);
            ht.oil_temp_c     = kelvin_to_c(be16(msg.data + 6) * 0.1f);
            ht.last_0x3E0_ms = now_ms;
            break;
        }

        case 0x3E9: {
            // Target lambda: bytes 4-5, scale 0.001 lambda
            if (msg.data_length_code < 6) return false;
            ht.target_lambda = be16(msg.data + 4) * 0.001f;
            ht.last_0x3E9_ms = now_ms;
            break;
        }

        default: break;
    }
    return false;
}

static void can_try_autoscan(uint32_t now_ms);

static void update_boost_target_from_can(uint32_t now_ms) {
    if (!twai_ready) {
        boost_psi = 0.0f;
        return;
    }

    twai_message_t msg;
    bool got_boost = false;
    float latest_psi = 0.0f;

    // Drain all queued frames; dispatch each one.
    while (twai_receive(&msg, 0) == ESP_OK) {
        can_frames_since_last_sample++;
        if (haltech_process_frame(msg, now_ms)) {
            // MAP updated — derive gauge PSI from absolute kPa
            float kpa_gauge = ht.map_kpa_abs - ATMOSPHERIC_KPA;
            float psi = kpa_gauge * KPA_TO_PSI;
            latest_psi = clamp_boost_psi(psi);
            got_boost = true;
        }
    }

    if (got_boost) {
        boost_psi = latest_psi;
        boost_can_last_valid_ms = now_ms;
        return;
    }

    if (boost_can_last_valid_ms == 0 || (now_ms - boost_can_last_valid_ms) > BOOST_CAN_TIMEOUT_MS) {
        boost_psi = 0.0f;
    }

    can_try_autoscan(now_ms);
}

static void init_twai_can() {
    if (twai_driver_installed) {
        twai_stop();
        twai_driver_uninstall();
        twai_driver_installed = false;
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = can_timing_config_for_profile(can_rate_profile);
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        Serial.printf("TWAI install failed (%d)\n", (int)err);
        twai_ready = false;
        return;
    }
    twai_driver_installed = true;

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("TWAI start failed (%d)\n", (int)err);
        twai_ready = false;
        twai_driver_uninstall();
        twai_driver_installed = false;
        return;
    }

    Serial.printf("TWAI ready: %s TX=%d RX=%d\n", can_rate_profile_text(can_rate_profile), (int)CAN_TX_GPIO, (int)CAN_RX_GPIO);
    twai_ready = true;
    boost_can_last_valid_ms = 0;
    can_last_bus_error_count = 0;
    can_last_autoscan_ms = lv_tick_get();
    can_frames_since_last_sample = 0;
}

static void can_try_autoscan(uint32_t now_ms) {
    if (!CAN_AUTOSCAN_ENABLED) return;
    if (!twai_ready) return;
    if ((now_ms - can_last_autoscan_ms) < CAN_AUTOSCAN_INTERVAL_MS) return;

    can_last_autoscan_ms = now_ms;

    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) return;

    bool no_valid_map = (boost_can_last_valid_ms == 0) || ((now_ms - boost_can_last_valid_ms) > CAN_AUTOSCAN_INTERVAL_MS);
    bool bus_errors_rising = (status.bus_error_count > (can_last_bus_error_count + 20));
    bool severe_bus_state = (status.state == TWAI_STATE_BUS_OFF) || (status.state == TWAI_STATE_RECOVERING);

    can_last_bus_error_count = status.bus_error_count;

    if (no_valid_map && (bus_errors_rising || severe_bus_state)) {
        CanBitrateProfile next = (CanBitrateProfile)(((int)can_rate_profile + 1) % CAN_RATE_COUNT);
        Serial.printf("CAN autoscan: %s -> %s (bus err %lu, state %d)\n",
                      can_rate_profile_text(can_rate_profile),
                      can_rate_profile_text(next),
                      (unsigned long)status.bus_error_count,
                      (int)status.state);
        can_rate_profile = next;
        init_twai_can();
    }
}

void build_static_needle_sprite() {
    LV_UNUSED(needle_obj);
}

void enable_gesture_bubble_recursive(lv_obj_t *obj) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    uint32_t child_count = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        enable_gesture_bubble_recursive(child);
    }
}

void my_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area) {
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    uint32_t flush_start_us = micros();
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    flush_count_accum++;
    flush_pixels_accum += (uint64_t)w * (uint64_t)h;
    flush_us_accum += (uint64_t)(micros() - flush_start_us);
    lv_disp_flush_ready(disp);
}

void my_tick(void *arg) { lv_tick_inc(2); }

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    LV_UNUSED(indev_driver);
    uint8_t touched = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
    if (touched > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = tx[0];
        data->point.y = ty[0];

        if (!touch_down) {
            touch_down = true;
            touch_start_x = tx[0];
            touch_start_y = ty[0];
            touch_start_ms = lv_tick_get();
        }
        touch_last_x = tx[0];
        touch_last_y = ty[0];
    } else {
        data->state = LV_INDEV_STATE_REL;

        if (touch_down) {
            uint32_t release_ms = lv_tick_get();
            int dx = touch_last_x - touch_start_x;
            int dy = touch_last_y - touch_start_y;
            int adx = abs(dx);
            int ady = abs(dy);
            uint32_t press_ms = release_ms - touch_start_ms;

            // Horizontal swipe with enough travel and clear dominance over vertical movement.
            if (adx > 70 && adx > (ady + 20)) {
                if (dx < 0) {
                    // Swipe left = forward in screen order
                    pending_screen = ((int)current_demo + 1) % DEMO_SCREEN_COUNT;
                } else {
                    // Swipe right = backward in screen order
                    pending_screen = ((int)current_demo + DEMO_SCREEN_COUNT - 1) % DEMO_SCREEN_COUNT;
                }
                pending_screen_change = true;
            } else {
                bool is_tap = (press_ms <= TAP_MAX_DURATION_MS) && (adx <= TAP_MAX_MOVE_PX) && (ady <= TAP_MAX_MOVE_PX);
                if (is_tap) {
                    uint32_t tap_gap = release_ms - last_tap_ms;
                    int tdx = touch_last_x - last_tap_x;
                    int tdy = touch_last_y - last_tap_y;
                    int tap_dist_sq = tdx * tdx + tdy * tdy;
                    int max_dist_sq = DOUBLE_TAP_MAX_DIST_PX * DOUBLE_TAP_MAX_DIST_PX;

                    if (last_tap_ms > 0 && tap_gap <= DOUBLE_TAP_GAP_MS && tap_dist_sq <= max_dist_sq) {
                        if (current_demo == DEMO_BOOSTAFR) {
                            boostafr_demo_mode = !boostafr_demo_mode;
                            if (boostafr_demo_mode) {
                                boostafr_demo_start_ms = release_ms;
                            }
                        } else {
                            set_profiler_visible(!profiler_visible);
                        }
                        last_tap_ms = 0;
                    } else {
                        last_tap_ms = release_ms;
                        last_tap_x = touch_last_x;
                        last_tap_y = touch_last_y;
                    }
                }
            }
        }

        touch_down = false;
    }
}

void draw_thick_line(lv_obj_t *canvas, int x1, int y1, int x2, int y2,
                     lv_color_t color, int thickness) {
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

void redraw_hands() {
    // Fill canvas with transparent black to clear it
    lv_canvas_fill_bg(hand_canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    float hour_angle = ((clk_hour % 12) * 30.0f + clk_min * 0.5f + clk_sec * (0.5f / 60.0f) - 90.0f) * M_PI / 180.0f;
    float min_angle  = (clk_min * 6.0f + clk_sec * 0.1f - 90.0f) * M_PI / 180.0f;
    float sec_angle  = (clk_sec * 6.0f - 90.0f) * M_PI / 180.0f;

    // Hour hand (white, thick, short)
    int hx  = CX + (int)(cos(hour_angle) * 80);
    int hy  = CY + (int)(sin(hour_angle) * 80);
    int hxt = CX - (int)(cos(hour_angle) * 20);
    int hyt = CY - (int)(sin(hour_angle) * 20);
    draw_thick_line(hand_canvas, hxt, hyt, hx, hy, lv_color_hex(0xffffff), 8);

    // Minute hand (white, medium)
    int mx  = CX + (int)(cos(min_angle) * 120);
    int my  = CY + (int)(sin(min_angle) * 120);
    int mxt = CX - (int)(cos(min_angle) * 20);
    int myt = CY - (int)(sin(min_angle) * 20);
    draw_thick_line(hand_canvas, mxt, myt, mx, my, lv_color_hex(0xffffff), 6);

    // Second hand (red, thin)
    int sx  = CX + (int)(cos(sec_angle) * 140);
    int sy  = CY + (int)(sin(sec_angle) * 140);
    int sxt = CX - (int)(cos(sec_angle) * 30);
    int syt = CY - (int)(sin(sec_angle) * 30);
    draw_thick_line(hand_canvas, sxt, syt, sx, sy, lv_color_hex(0xff3333), 3);

    // Center dot (gold)
    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.bg_color = lv_color_hex(0xB8960C);
    dot_dsc.bg_opa = LV_OPA_COVER;
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.border_width = 0;
    lv_canvas_draw_rect(hand_canvas, CX - 7, CY - 7, 14, 14, &dot_dsc);
}

void draw_needle_event(lv_event_t *e) {
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    float angle = psi_to_angle_rad(boost_psi_visual);
    float dx = cosf(angle);
    float dy = sinf(angle);
    float nx = -dy;
    float ny = dx;

    const float tip_r = 172.0f;
    const float tail_r = 34.0f;
    const float w_base = 10.0f;
    const float w_tip = 5.0f;
    const float w_tail = 10.0f;

    float cx = (float)coords.x1 + (float)needle_pivot_x_local;
    float cy = (float)coords.y1 + (float)needle_pivot_y_local;

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
    needle_paint_accum++;
}

void draw_triangle_marker_event(lv_event_t *e) {
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);
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

void update_perf_overlay(lv_timer_t *timer) {
    LV_UNUSED(timer);

    if (!perf_label) return;

    uint32_t now = lv_tick_get();
    if (perf_window_start_ms == 0) {
        perf_window_start_ms = now;
        return;
    }

    uint32_t elapsed_ms = now - perf_window_start_ms;
    if (elapsed_ms < 1000) return;

    float elapsed_s = (float)elapsed_ms / 1000.0f;
    float flushes_per_s = (float)flush_count_accum / elapsed_s;
    float invalidates_per_s = (float)needle_invalidate_accum / elapsed_s;
    float paints_per_s = (float)needle_paint_accum / elapsed_s;
    float avg_flush_ms = flush_count_accum > 0
        ? ((float)flush_us_accum / 1000.0f) / (float)flush_count_accum
        : 0.0f;
    float mpix_per_s = (float)flush_pixels_accum / (elapsed_s * 1000000.0f);

    char perf_buf[192];
    snprintf(perf_buf, sizeof(perf_buf),
             "%s\nneedle req %.1f/s draw %.1f/s\nflush %.1f/s avg %.2f ms\npixels %.2f MPix/s",
             lvgl_buffer_mode,
             invalidates_per_s,
             paints_per_s,
             flushes_per_s,
             avg_flush_ms,
             mpix_per_s);
    lv_label_set_text(perf_label, perf_buf);

    perf_window_start_ms = now;
    flush_count_accum = 0;
    needle_invalidate_accum = 0;
    needle_paint_accum = 0;
    flush_pixels_accum = 0;
    flush_us_accum = 0;
}

void set_profiler_visible(bool visible) {
    profiler_visible = visible;

    if (perf_label) {
        if (visible) {
            lv_obj_clear_flag(perf_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(perf_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (perf_timer) {
        if (visible) {
            perf_window_start_ms = lv_tick_get();
            flush_count_accum = 0;
            needle_invalidate_accum = 0;
            needle_paint_accum = 0;
            flush_pixels_accum = 0;
            flush_us_accum = 0;
            lv_timer_resume(perf_timer);
        } else {
            lv_timer_pause(perf_timer);
        }
    }
}

void compute_needle_area(float psi, lv_area_t *area) {
    if (!needle_obj || !area) return;

    lv_area_t coords;
    lv_obj_get_coords(needle_obj, &coords);

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

    float cx = (float)coords.x1 + (float)needle_pivot_x_local;
    float cy = (float)coords.y1 + (float)needle_pivot_y_local;

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

float psi_to_angle_rad(float psi) {
    float norm = (psi - BOOST_MIN) / BOOST_RANGE;
    float deg = BOOST_START_DEG + norm * BOOST_SWEEP_DEG;
    return deg * M_PI / 180.0f;
}

void redraw_gauge_needle() {
    if (!needle_obj) return;

    lv_area_t next_area;
    compute_needle_area(boost_psi_visual, &next_area);

    if (needle_last_area_valid) {
        lv_area_t union_area = {
            (lv_coord_t)LV_MIN(needle_last_area.x1, next_area.x1),
            (lv_coord_t)LV_MIN(needle_last_area.y1, next_area.y1),
            (lv_coord_t)LV_MAX(needle_last_area.x2, next_area.x2),
            (lv_coord_t)LV_MAX(needle_last_area.y2, next_area.y2)
        };
        lv_obj_invalidate_area(needle_obj, &union_area);
    } else {
        lv_obj_invalidate_area(needle_obj, &next_area);
    }

    needle_last_area = next_area;
    needle_last_area_valid = true;
    needle_invalidate_accum++;
}

void set_demo_screen(DemoScreen target, lv_scr_load_anim_t anim) {
    if (target == current_demo) return;

    // Pause all screen timers before switching.
    if (gauge_timer)  lv_timer_pause(gauge_timer);
    if (perf_timer)   lv_timer_pause(perf_timer);
    if (boostafr_arc_timer) lv_timer_pause(boostafr_arc_timer);
    if (boostafr_label_timer) lv_timer_pause(boostafr_label_timer);
    if (watch_timer)  lv_timer_pause(watch_timer);
    if (data_timer)   lv_timer_pause(data_timer);
    if (candbg_timer) lv_timer_pause(candbg_timer);

    lv_obj_t *next_scr = nullptr;
    switch (target) {
        case DEMO_GAUGE:
            next_scr = gauge_scr;
            if (gauge_timer) lv_timer_resume(gauge_timer);
            if (perf_timer)  lv_timer_resume(perf_timer);
            break;
        case DEMO_BOOSTAFR:
            next_scr = boostafr_scr;
            if (boostafr_arc_timer) lv_timer_resume(boostafr_arc_timer);
            if (boostafr_label_timer) lv_timer_resume(boostafr_label_timer);
            break;
        case DEMO_DATA:
            next_scr = data_scr;
            if (data_timer) lv_timer_resume(data_timer);
            break;
        case DEMO_WATCH:
            next_scr = watch_scr;
            if (watch_timer) lv_timer_resume(watch_timer);
            break;
        case DEMO_CANDBG:
            next_scr = candbg_scr;
            if (candbg_timer) lv_timer_resume(candbg_timer);
            break;
        default: break;
    }

    if (next_scr) lv_scr_load_anim(next_scr, anim, 220, 0, false);
    current_demo = target;
    prefs.putUChar("last_screen", (uint8_t)current_demo);
}

void handle_swipe(lv_event_t *e) {
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        // Forward: GAUGE → DATA → WATCH
        int next = ((int)current_demo + 1) % DEMO_SCREEN_COUNT;
        set_demo_screen((DemoScreen)next, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    } else if (dir == LV_DIR_RIGHT) {
        // Backward: WATCH → DATA → GAUGE
        int next = ((int)current_demo + DEMO_SCREEN_COUNT - 1) % DEMO_SCREEN_COUNT;
        set_demo_screen((DemoScreen)next, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }
}

void tick_clock(lv_timer_t *timer) {
    clk_sec++;
    if (clk_sec >= 60) { clk_sec = 0; clk_min++; }
    if (clk_min >= 60) { clk_min = 0; clk_hour++; }
    if (clk_hour >= 24) clk_hour = 0;

    char date_buf[12];
    snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%04d", clk_day, clk_month, clk_year);
    lv_label_set_text(date_label, date_buf);

    redraw_hands();
}

void tick_gauge(lv_timer_t *timer) {
    LV_UNUSED(timer);

    uint32_t now = lv_tick_get();
    float prev_psi = boost_psi;
    update_boost_target_from_can(now);

    // Visual low-pass filter reduces micro-jitter without changing timing/state logic.
    uint32_t dt_ms = (needle_frame_last_ms > 0 && now > needle_frame_last_ms)
        ? (now - needle_frame_last_ms)
        : 4;
    float alpha = 0.30f;
    if (dt_ms > 0) {
        float abs_rate_dps = fabsf((boost_psi - prev_psi) * (BOOST_SWEEP_DEG / BOOST_RANGE) * (1000.0f / (float)dt_ms));
        float anti_tear_mix = 0.0f;
        if (abs_rate_dps > ANTI_TEAR_SOFT_DPS) {
            anti_tear_mix = (abs_rate_dps - ANTI_TEAR_SOFT_DPS) / (ANTI_TEAR_HARD_DPS - ANTI_TEAR_SOFT_DPS);
            if (anti_tear_mix > 1.0f) anti_tear_mix = 1.0f;
        }

        float tau_ms = ANTI_TEAR_TAU_LOW_MS + (ANTI_TEAR_TAU_HIGH_MS - ANTI_TEAR_TAU_LOW_MS) * anti_tear_mix;
        alpha = (float)dt_ms / (tau_ms + (float)dt_ms);
        if (alpha < 0.12f) alpha = 0.12f;
        if (alpha > 0.72f) alpha = 0.72f;
    }

    float prev_visual_psi = boost_psi_visual;
    boost_psi_visual += (boost_psi - boost_psi_visual) * alpha;

    // Clamp visual angular slew rate to reduce tearing during very fast swings.
    float max_visual_psi_step = (ANTI_TEAR_MAX_VISUAL_DPS * (float)dt_ms / 1000.0f) * (BOOST_RANGE / BOOST_SWEEP_DEG);
    float visual_dpsi = boost_psi_visual - prev_visual_psi;
    if (visual_dpsi > max_visual_psi_step) {
        boost_psi_visual = prev_visual_psi + max_visual_psi_step;
    } else if (visual_dpsi < -max_visual_psi_step) {
        boost_psi_visual = prev_visual_psi - max_visual_psi_step;
    }

    needle_frame_last_ms = now;

    float needle_angle_deg = psi_to_angle_rad(boost_psi_visual) * 180.0f / M_PI;
    bool redraw_needed = isnan(needle_last_drawn_angle_deg) ||
                         fabsf(needle_angle_deg - needle_last_drawn_angle_deg) >= NEEDLE_REDRAW_THRESHOLD_DEG;

    if ((now - psi_label_last_ms) >= PSI_LABEL_UPDATE_MS) {
        psi_label_last_ms = now;
        char psi_buf[16];
        snprintf(psi_buf, sizeof(psi_buf), "%.1f", boost_psi);
        lv_label_set_text(psi_value_label, psi_buf);
    }

    if (redraw_needed) {
        redraw_gauge_needle();
        needle_last_drawn_angle_deg = needle_angle_deg;
    }
}

void tick_boostafr_arcs(lv_timer_t *timer) {
    LV_UNUSED(timer);

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
        lv_obj_set_style_arc_color(boostafr_map_arc, arc_color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(boostafr_psi_label, text_color, 0);
        lv_obj_set_style_text_color(boostafr_psi_title_label, text_color, 0);
    };

    auto set_afr_color_mode = [&](uint8_t mode) {
        if (mode == afr_color_mode) return;
        afr_color_mode = mode;
        lv_color_t arc_color = (mode == 1) ? lv_color_hex(0xFF5A00) : lv_color_hex(0x20c870);
        lv_color_t text_color = (mode == 1) ? lv_color_hex(0xFF5A00) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_arc_color(boostafr_afr_arc, arc_color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(boostafr_afr_label, text_color, 0);
        lv_obj_set_style_text_color(boostafr_afr_title_label, text_color, 0);
    };

    auto set_marker_hidden = [&](lv_obj_t *marker, bool hidden, bool *cached_hidden) {
        if (*cached_hidden == hidden) return;
        *cached_hidden = hidden;
        if (hidden) {
            lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        }
    };

    auto set_marker_pos_if_changed = [&](lv_obj_t *marker, int x, int y, int *last_x, int *last_y) {
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
    float afr_actual_target = STOICH_AFR;

    if (boostafr_demo_mode) {
        get_boostafr_demo_values(now, &map_psi_target, &afr_actual_target);
        set_marker_hidden(boostafr_afr_target_marker, true, &afr_marker_hidden);
        set_marker_hidden(boostafr_boost_target_marker, true, &boost_marker_hidden);
        set_boost_color_mode(0);
        set_afr_color_mode(0);

        boost_over_target_start_ms = 0;
        lambda_lean_start_ms = 0;
        boost_over_target_alert = false;
        lambda_lean_alert = false;
    } else {
        set_marker_hidden(boostafr_afr_target_marker, false, &afr_marker_hidden);

        if (ht.last_0x360_ms > 0 && (now - ht.last_0x360_ms) < 1200) {
            map_psi_target = (ht.map_kpa_abs - ATMOSPHERIC_KPA) * KPA_TO_PSI;
        }
        map_psi_target = clampf(map_psi_target, BOOST_MIN, BOOST_MAX);
        float boost_kpa_gauge = map_psi_target / KPA_TO_PSI;

        float lambda_actual = 1.0f;
        if (ht.last_0x368_ms > 0 && (now - ht.last_0x368_ms) < 1200) {
            lambda_actual = ht.lambda1;
        }

        float lambda_target = 1.0f;
        if (ht.last_0x3E9_ms > 0 && (now - ht.last_0x3E9_ms) < 2500 && ht.target_lambda > 0.0f) {
            lambda_target = ht.target_lambda;
        }

        afr_actual_target = clampf(lambda_actual * STOICH_AFR, AFR_MIN, AFR_MAX);
        float afr_target = clampf(lambda_target * STOICH_AFR, AFR_MIN, AFR_MAX);

        bool rpm_active = ht.last_0x360_ms > 0 && (now - ht.last_0x360_ms) < 1200 && ht.rpm >= 1000;

        float t = (afr_target - AFR_MIN) / (AFR_MAX - AFR_MIN);
        float angle_deg = arc_angle_for_t(AFR_ARC_START_DEG, AFR_ARC_END_DEG, t);
        float angle_rad = angle_deg * (float)M_PI / 180.0f;
        float radius = 206.0f;
        int afr_marker_x = (int)lroundf((float)CX + cosf(angle_rad) * radius - 12.0f);
        int afr_marker_y = (int)lroundf((float)CY + sinf(angle_rad) * radius - 12.0f);
        set_marker_pos_if_changed(boostafr_afr_target_marker, afr_marker_x, afr_marker_y,
                                  &last_afr_marker_x, &last_afr_marker_y);

        bool target_boost_valid = ht.last_0x372_ms > 0 && (now - ht.last_0x372_ms) < 2500;
        if (target_boost_valid) {
            float target_psi = clampf(ht.target_boost_kpa * KPA_TO_PSI, BOOST_MIN, BOOST_MAX);
            float bt = (target_psi - BOOST_MIN) / (BOOST_MAX - BOOST_MIN);
            float bangle_deg = arc_angle_for_t(BOOST_ARC_START_DEG, BOOST_ARC_END_DEG, bt);
            float bangle_rad = bangle_deg * (float)M_PI / 180.0f;
            float bradius = 206.0f;
            int boost_marker_x = (int)lroundf((float)CX + cosf(bangle_rad) * bradius - 12.0f);
            int boost_marker_y = (int)lroundf((float)CY + sinf(bangle_rad) * bradius - 12.0f);
            set_marker_pos_if_changed(boostafr_boost_target_marker, boost_marker_x, boost_marker_y,
                                      &last_boost_marker_x, &last_boost_marker_y);
            set_marker_hidden(boostafr_boost_target_marker, false, &boost_marker_hidden);
        } else {
            set_marker_hidden(boostafr_boost_target_marker, true, &boost_marker_hidden);
        }

        bool boost_cond = rpm_active && target_boost_valid && ((boost_kpa_gauge - ht.target_boost_kpa) >= BOOST_ALERT_KPA_DELTA);
        if (boost_cond) {
            if (boost_over_target_start_ms == 0) boost_over_target_start_ms = now;
            if ((now - boost_over_target_start_ms) >= ALERT_HOLD_MS) boost_over_target_alert = true;
        } else {
            boost_over_target_start_ms = 0;
            boost_over_target_alert = false;
        }

        bool lambda_cond = rpm_active && (lambda_actual > (lambda_target * LAMBDA_LEAN_RATIO));
        if (lambda_cond) {
            if (lambda_lean_start_ms == 0) lambda_lean_start_ms = now;
            if ((now - lambda_lean_start_ms) >= ALERT_HOLD_MS) lambda_lean_alert = true;
        } else {
            lambda_lean_start_ms = 0;
            lambda_lean_alert = false;
        }

        bool flash_on = (((now / 180U) % 2U) == 0U);
        set_boost_color_mode((boost_over_target_alert && flash_on) ? 1 : 0);
        set_afr_color_mode((lambda_lean_alert && flash_on) ? 1 : 0);
    }

    uint32_t dt_ms = (boostafr_visual_last_ms > 0 && now > boostafr_visual_last_ms)
        ? (now - boostafr_visual_last_ms)
        : BOOSTAFR_ARC_TICK_MS;
    boostafr_visual_last_ms = now;

    if (!boostafr_visual_initialized) {
        boostafr_map_visual_psi = map_psi_target;
        boostafr_afr_visual = afr_actual_target;
        boostafr_visual_initialized = true;
    } else {
        float map_alpha = (float)dt_ms / (BOOSTAFR_MAP_TAU_MS + (float)dt_ms);
        float afr_alpha = (float)dt_ms / (BOOSTAFR_AFR_TAU_MS + (float)dt_ms);
        map_alpha = clampf(map_alpha, 0.10f, 0.85f);
        afr_alpha = clampf(afr_alpha, 0.08f, 0.80f);

        boostafr_map_visual_psi += (map_psi_target - boostafr_map_visual_psi) * map_alpha;
        boostafr_afr_visual += (afr_actual_target - boostafr_afr_visual) * afr_alpha;
    }

    int map_arc_value = (int)lroundf(((boostafr_map_visual_psi - BOOST_MIN) / (BOOST_MAX - BOOST_MIN)) * 1000.0f);
    map_arc_value = (int)clampf((float)map_arc_value, 0.0f, 1000.0f);
    set_arc_value_if_changed(boostafr_map_arc, map_arc_value, &last_map_arc_value);

    int afr_arc_value = (int)lroundf(((boostafr_afr_visual - AFR_MIN) / (AFR_MAX - AFR_MIN)) * 1000.0f);
    afr_arc_value = (int)clampf((float)afr_arc_value, 0.0f, 1000.0f);
    set_arc_value_if_changed(boostafr_afr_arc, afr_arc_value, &last_afr_arc_value);
}

void tick_boostafr_labels(lv_timer_t *timer) {
    LV_UNUSED(timer);

    uint32_t now = lv_tick_get();
    char buf[24];

    if (boostafr_demo_mode) {
        float map_psi = 0.0f;
        float afr_actual = 14.7f;
        get_boostafr_demo_values(now, &map_psi, &afr_actual);

        snprintf(buf, sizeof(buf), "%.1f", map_psi);
        lv_label_set_text(boostafr_psi_label, buf);

        snprintf(buf, sizeof(buf), "%.1f", afr_actual);
        lv_label_set_text(boostafr_afr_label, buf);
        return;
    }

    float map_psi = 0.0f;
    if (ht.last_0x360_ms > 0 && (now - ht.last_0x360_ms) < 1200) {
        map_psi = (ht.map_kpa_abs - ATMOSPHERIC_KPA) * KPA_TO_PSI;
    }
    map_psi = clampf(map_psi, BOOST_MIN, BOOST_MAX);
    snprintf(buf, sizeof(buf), "%.1f", map_psi);
    lv_label_set_text(boostafr_psi_label, buf);

    float lambda_actual = 1.0f;
    if (ht.last_0x368_ms > 0 && (now - ht.last_0x368_ms) < 1200) {
        lambda_actual = ht.lambda1;
    }
    float afr_actual = clampf(lambda_actual * STOICH_AFR, AFR_MIN, AFR_MAX);
    snprintf(buf, sizeof(buf), "%.1f", afr_actual);
    lv_label_set_text(boostafr_afr_label, buf);
}

void draw_boostafr_face(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *top_arc = lv_arc_create(scr);
    boostafr_map_arc = top_arc;
    lv_obj_set_size(top_arc, 420, 420);
    lv_obj_center(top_arc);
    lv_arc_set_rotation(top_arc, 0);
    lv_arc_set_bg_angles(top_arc, (int)BOOST_ARC_START_DEG, (int)BOOST_ARC_END_DEG);
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

    lv_obj_t *bottom_arc = lv_arc_create(scr);
    boostafr_afr_arc = bottom_arc;
    lv_obj_set_size(bottom_arc, 420, 420);
    lv_obj_center(bottom_arc);
    lv_arc_set_rotation(bottom_arc, 0);
    lv_arc_set_bg_angles(bottom_arc, (int)AFR_ARC_START_DEG, (int)AFR_ARC_END_DEG);
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

    boostafr_psi_label = lv_label_create(scr);
    lv_obj_set_style_text_color(boostafr_psi_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(boostafr_psi_label, &lv_font_montserrat_medium_72, 0);
    lv_obj_set_width(boostafr_psi_label, 360);
    lv_obj_set_style_text_align(boostafr_psi_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(boostafr_psi_label, CX - 180, 112);
    lv_label_set_text(boostafr_psi_label, "0.0");

    boostafr_psi_title_label = lv_label_create(scr);
    lv_obj_set_style_text_color(boostafr_psi_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(boostafr_psi_title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(boostafr_psi_title_label, 320);
    lv_obj_set_style_text_align(boostafr_psi_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(boostafr_psi_title_label, CX - 160, 182);
    lv_label_set_text(boostafr_psi_title_label, "PSI BOOST");

    boostafr_afr_title_label = lv_label_create(scr);
    lv_obj_set_style_text_color(boostafr_afr_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(boostafr_afr_title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(boostafr_afr_title_label, 320);
    lv_obj_set_style_text_align(boostafr_afr_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(boostafr_afr_title_label, CX - 160, 277);
    lv_label_set_text(boostafr_afr_title_label, "AFR");

    boostafr_afr_label = lv_label_create(scr);
    lv_obj_set_style_text_color(boostafr_afr_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(boostafr_afr_label, &lv_font_montserrat_medium_72, 0);
    lv_obj_set_width(boostafr_afr_label, 360);
    lv_obj_set_style_text_align(boostafr_afr_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(boostafr_afr_label, CX - 180, 322);
    lv_label_set_text(boostafr_afr_label, "14.7");

    // Filled gray triangle marker for target AFR.
    boostafr_afr_target_marker = lv_obj_create(scr);
    lv_obj_remove_style_all(boostafr_afr_target_marker);
    lv_obj_set_size(boostafr_afr_target_marker, 24, 24);
    lv_obj_set_pos(boostafr_afr_target_marker, CX - 12, CY + 180);
    lv_obj_add_event_cb(boostafr_afr_target_marker, draw_triangle_marker_event, LV_EVENT_DRAW_MAIN, nullptr);

    // Filled gray triangle marker for target boost.
    boostafr_boost_target_marker = lv_obj_create(scr);
    lv_obj_remove_style_all(boostafr_boost_target_marker);
    lv_obj_set_size(boostafr_boost_target_marker, 24, 24);
    lv_obj_set_pos(boostafr_boost_target_marker, CX - 12, CY - 200);
    lv_obj_add_event_cb(boostafr_boost_target_marker, draw_triangle_marker_event, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_flag(boostafr_boost_target_marker, LV_OBJ_FLAG_HIDDEN);

    boostafr_arc_timer = lv_timer_create(tick_boostafr_arcs, BOOSTAFR_ARC_TICK_MS, NULL);
    boostafr_label_timer = lv_timer_create(tick_boostafr_labels, BOOSTAFR_LABEL_TICK_MS, NULL);
    tick_boostafr_arcs(nullptr);
    tick_boostafr_labels(nullptr);

    enable_gesture_bubble_recursive(scr);
}

void draw_watch_face(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Outer bezel (gold ring)
    lv_obj_t *bezel = lv_obj_create(scr);
    lv_obj_set_size(bezel, RADIUS * 2 + 10, RADIUS * 2 + 10);
    lv_obj_center(bezel);
    lv_obj_set_style_radius(bezel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bezel, lv_color_hex(0xB8960C), 0);
    lv_obj_set_style_border_width(bezel, 0, 0);
    lv_obj_set_style_pad_all(bezel, 0, 0);

    // Watch face (dark navy circle)
    lv_obj_t *face = lv_obj_create(scr);
    lv_obj_set_size(face, RADIUS * 2, RADIUS * 2);
    lv_obj_center(face);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_pad_all(face, 0, 0);

    // Marker canvas - TRUE_COLOR (no alpha), navy background matching face
    // Size: LCD_WIDTH * LCD_HEIGHT * 2 bytes (16-bit color)
    lv_color_t *marker_buf = (lv_color_t *)heap_caps_malloc(
        LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_obj_t *marker_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(marker_canvas, marker_buf, LCD_WIDTH, LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(marker_canvas, 0, 0);
    // Fill with the face background color so non-marker areas match
    lv_canvas_fill_bg(marker_canvas, lv_color_hex(0x0a0a1a), LV_OPA_COVER);

    // Draw hour markers
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30.0f - 90.0f) * M_PI / 180.0f;
        bool major = (i % 3 == 0);
        int inner = major ? RADIUS - 28 : RADIUS - 16;
        int outer_r = RADIUS - 6;
        int w = major ? 6 : 3;

        int x1 = CX + (int)(cos(angle) * inner);
        int y1 = CY + (int)(sin(angle) * inner);
        int x2 = CX + (int)(cos(angle) * outer_r);
        int y2 = CY + (int)(sin(angle) * outer_r);

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = major ? lv_color_hex(0xB8960C) : lv_color_hex(0x888888);
        dsc.width = w;
        dsc.opa = LV_OPA_COVER;
        lv_point_t pts[] = {{(lv_coord_t)x1, (lv_coord_t)y1}, {(lv_coord_t)x2, (lv_coord_t)y2}};
        lv_canvas_draw_line(marker_canvas, pts, 2, &dsc);
    }

    // Date box
    lv_obj_t *date_box = lv_obj_create(scr);
    lv_obj_set_size(date_box, 110, 32);
    lv_obj_set_pos(date_box, CX + 40, CY - 16);
    lv_obj_set_style_bg_color(date_box, lv_color_hex(0x1a1a3a), 0);
    lv_obj_set_style_border_color(date_box, lv_color_hex(0xB8960C), 0);
    lv_obj_set_style_border_width(date_box, 2, 0);
    lv_obj_set_style_radius(date_box, 4, 0);
    lv_obj_set_style_pad_all(date_box, 0, 0);

    date_label = lv_label_create(date_box);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(date_label);

    // Hand canvas - TRUE_COLOR_ALPHA so hands can be transparent over the face
    // 4 bytes per pixel for ARGB
    canvas_buf = (lv_color_t *)heap_caps_malloc(
        LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t) * 2, MALLOC_CAP_SPIRAM);
    hand_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(hand_canvas, canvas_buf, LCD_WIDTH, LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_set_pos(hand_canvas, 0, 0);

    // Initial draw
    char date_buf[12];
    snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%04d", clk_day, clk_month, clk_year);
    lv_label_set_text(date_label, date_buf);
    redraw_hands();

    watch_timer = lv_timer_create(tick_clock, 1000, NULL);

    enable_gesture_bubble_recursive(scr);
}

void draw_gauge_face(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *face = lv_obj_create(scr);
    lv_obj_set_size(face, RADIUS * 2, RADIUS * 2);
    lv_obj_center(face);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_pad_all(face, 0, 0);

    gauge_bg_buf = (lv_color_t *)heap_caps_malloc(
        LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_obj_t *bg_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(bg_canvas, gauge_bg_buf, LCD_WIDTH, LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(bg_canvas, 0, 0);
    lv_canvas_fill_bg(bg_canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    for (int psi = (int)BOOST_MIN; psi <= (int)BOOST_MAX; psi++) {
        float angle = psi_to_angle_rad((float)psi);
        bool major = (psi % 5 == 0);

        int outer_r = 218;
        int inner_r = major ? 182 : 196;
        int tick_w = major ? 5 : 2;

        int x1 = CX + (int)(cos(angle) * inner_r);
        int y1 = CY + (int)(sin(angle) * inner_r);
        int x2 = CX + (int)(cos(angle) * outer_r);
        int y2 = CY + (int)(sin(angle) * outer_r);

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.color = lv_color_hex(0xFFFFFF);
        dsc.width = tick_w;
        dsc.opa = LV_OPA_COVER;
        lv_point_t pts[] = {{(lv_coord_t)x1, (lv_coord_t)y1}, {(lv_coord_t)x2, (lv_coord_t)y2}};
        lv_canvas_draw_line(bg_canvas, pts, 2, &dsc);

        if (major) {
            lv_obj_t *num = lv_label_create(scr);
            lv_obj_set_style_text_color(num, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(num, &lv_font_montserrat_40, 0);

            char nbuf[8];
            snprintf(nbuf, sizeof(nbuf), "%d", psi);
            lv_label_set_text(num, nbuf);
            lv_obj_update_layout(num);

            int label_r = 154;
            int nx = CX + (int)(cos(angle) * label_r);
            int ny = CY + (int)(sin(angle) * label_r);
            lv_obj_set_pos(num, nx - lv_obj_get_width(num) / 2, ny - lv_obj_get_height(num) / 2);
        }
    }

    psi_value_label = lv_label_create(scr);
    lv_obj_set_style_text_color(psi_value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(psi_value_label, &lv_font_montserrat_48, 0);
    lv_obj_align(psi_value_label, LV_ALIGN_BOTTOM_MID, 0, -72);
    lv_label_set_text(psi_value_label, "0.0");

    psi_unit_label = lv_label_create(scr);
    lv_obj_set_style_text_color(psi_unit_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(psi_unit_label, &lv_font_montserrat_32, 0);
    lv_obj_align(psi_unit_label, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text(psi_unit_label, "PSI");

    needle_pivot_x_local = NEEDLE_CANVAS_SIZE / 2;
    needle_pivot_y_local = NEEDLE_CANVAS_SIZE / 2;
    needle_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(needle_obj);
    lv_obj_set_size(needle_obj, NEEDLE_CANVAS_SIZE, NEEDLE_CANVAS_SIZE);
    lv_obj_set_pos(needle_obj, CX - needle_pivot_x_local, CY - needle_pivot_y_local);
    lv_obj_add_flag(needle_obj, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(needle_obj, draw_needle_event, LV_EVENT_DRAW_MAIN, NULL);

    needle_pivot = lv_obj_create(scr);
    lv_obj_set_size(needle_pivot, 40, 40);
    lv_obj_set_pos(needle_pivot, CX - 20, CY - 20);
    lv_obj_set_style_radius(needle_pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(needle_pivot, lv_color_hex(0xFF1020), 0);
    lv_obj_set_style_border_width(needle_pivot, 0, 0);
    lv_obj_set_style_pad_all(needle_pivot, 0, 0);

    perf_label = lv_label_create(scr);
    lv_obj_set_style_text_color(perf_label, lv_color_hex(0x7f7f7f), 0);
    lv_obj_set_style_text_font(perf_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(perf_label, 220);
    lv_obj_set_style_text_align(perf_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(perf_label, LV_ALIGN_TOP_MID, 0, 105);
    lv_label_set_text(perf_label, "profiling...");
    lv_obj_add_flag(perf_label, LV_OBJ_FLAG_HIDDEN);

    uint32_t now = lv_tick_get();
    boost_psi_visual = boost_psi;
    needle_frame_last_ms = now;
    needle_last_drawn_angle_deg = NAN;
    psi_label_last_ms = now;
    needle_last_area_valid = false;
    perf_window_start_ms = now;
    flush_count_accum = 0;
    needle_invalidate_accum = 0;
    needle_paint_accum = 0;
    flush_pixels_accum = 0;
    flush_us_accum = 0;

    redraw_gauge_needle();
    gauge_timer = lv_timer_create(tick_gauge, GAUGE_TICK_MS, NULL);
    perf_timer = lv_timer_create(update_perf_overlay, 1000, NULL);
    set_profiler_visible(false);

    enable_gesture_bubble_recursive(scr);
}

// ─── Data screen ─────────────────────────────────────────────────────────────

static lv_obj_t *make_data_name_label(lv_obj_t *scr, const char *text, int y) {
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x7ab8f5), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl, 360);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl, CX - 180, y);
    lv_label_set_text(lbl, text);
    return lbl;
}

static lv_obj_t *make_data_val_label(lv_obj_t *scr, const char *init_text, int y) {
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_width(lbl, 360);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl, CX - 180, y);
    lv_label_set_text(lbl, init_text);
    return lbl;
}

void tick_data(lv_timer_t *timer) {
    LV_UNUSED(timer);
    uint32_t now = lv_tick_get();
    char buf[24];

    // RPM
    if (ht.last_0x360_ms > 0 && (now - ht.last_0x360_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%u RPM", (unsigned)ht.rpm);
    } else {
        snprintf(buf, sizeof(buf), "-- RPM");
    }
    lv_label_set_text(data_rpm_val_label, buf);

    // TPS
    if (ht.last_0x360_ms > 0 && (now - ht.last_0x360_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%.1f %%", ht.tps_pct);
    } else {
        snprintf(buf, sizeof(buf), "--.-  %%");
    }
    lv_label_set_text(data_tps_val_label, buf);

    // Coolant temp
    if (ht.last_0x3E0_ms > 0 && (now - ht.last_0x3E0_ms) < 3000) {
        snprintf(buf, sizeof(buf), "%.0f C", ht.coolant_temp_c);
    } else {
        snprintf(buf, sizeof(buf), "-- C");
    }
    lv_label_set_text(data_coolant_val_label, buf);

    // Vehicle speed (km/h)
    if (ht.last_0x370_ms > 0 && (now - ht.last_0x370_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%.0f km/h", ht.wheel_speed_kph);
    } else {
        snprintf(buf, sizeof(buf), "-- km/h");
    }
    lv_label_set_text(data_speed_val_label, buf);

    // Fuel pressure (kPa gauge)
    if (ht.last_0x361_ms > 0 && (now - ht.last_0x361_ms) < 1000) {
        snprintf(buf, sizeof(buf), "%.0f kPa", ht.fuel_press_kpa);
    } else {
        snprintf(buf, sizeof(buf), "-- kPa");
    }
    lv_label_set_text(data_fuelpsi_val_label, buf);
}

// ─── CAN debug screen ────────────────────────────────────────────────────────

void tick_candbg(lv_timer_t *timer) {
    LV_UNUSED(timer);

    // Snapshot and reset the frame counter accumulated since last call (200 ms window).
    uint32_t frames = can_frames_since_last_sample;
    can_frames_since_last_sample = 0;
    // 200 ms period → multiply by 5 to get frames/second.
    can_fps_display = (float)frames * 5.0f;

    char buf[64];

    // Frames/second
    snprintf(buf, sizeof(buf), "%.0f frames/s", can_fps_display);
    lv_label_set_text(candbg_fps_label, buf);

    // TWAI driver state
    twai_status_info_t status;
    if (twai_ready && twai_get_status_info(&status) == ESP_OK) {
        const char *state_str = "UNKNOWN";
        switch (status.state) {
            case TWAI_STATE_STOPPED:    state_str = "STOPPED";    break;
            case TWAI_STATE_RUNNING:    state_str = "RUNNING";    break;
            case TWAI_STATE_BUS_OFF:    state_str = "BUS-OFF";    break;
            case TWAI_STATE_RECOVERING: state_str = "RECOVERING"; break;
        }
        snprintf(buf, sizeof(buf), "State: %s @%s  RX err: %lu",
                 state_str, can_rate_profile_text(can_rate_profile), (unsigned long)status.rx_error_counter);
        lv_label_set_text(candbg_state_label, buf);

        snprintf(buf, sizeof(buf), "TX err: %lu  Arb lost: %lu\nRX miss: %lu  Bus err: %lu",
                 (unsigned long)status.tx_error_counter,
                 (unsigned long)status.arb_lost_count,
                 (unsigned long)status.rx_missed_count,
                 (unsigned long)status.bus_error_count);
        lv_label_set_text(candbg_ids_label, buf);
    } else {
        lv_label_set_text(candbg_state_label, "TWAI not ready");
        lv_label_set_text(candbg_ids_label, "");
    }
}

void draw_candbg_face(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf5a623), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_width(title, 360);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, CX - 180, 90);
    lv_label_set_text(title, "CAN BUS DEBUG");

    // Frames/second — large
    candbg_fps_label = lv_label_create(scr);
    lv_obj_set_style_text_color(candbg_fps_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(candbg_fps_label, &lv_font_montserrat_48, 0);
    lv_obj_set_width(candbg_fps_label, 360);
    lv_obj_set_style_text_align(candbg_fps_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(candbg_fps_label, CX - 180, 115);
    lv_label_set_text(candbg_fps_label, "-- frames/s");

    // Driver state
    candbg_state_label = lv_label_create(scr);
    lv_obj_set_style_text_color(candbg_state_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(candbg_state_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(candbg_state_label, 360);
    lv_obj_set_style_text_align(candbg_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(candbg_state_label, CX - 180, 210);
    lv_label_set_text(candbg_state_label, "State: --");

    // Error counters
    candbg_ids_label = lv_label_create(scr);
    lv_obj_set_style_text_color(candbg_ids_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(candbg_ids_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(candbg_ids_label, 360);
    lv_obj_set_style_text_align(candbg_ids_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(candbg_ids_label, CX - 180, 240);
    lv_label_set_text(candbg_ids_label, "");

    candbg_timer = lv_timer_create(tick_candbg, 200, NULL);  // 5 Hz

    enable_gesture_bubble_recursive(scr);
}

void draw_data_face(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Layout: 5 rows of (name label + value label), centered in the round display.
    // Row positions (y for name, y+20 for value), rows spaced 75px apart.
    // Starts at y=60 so the block is visually balanced in the 466-px circle.
    const int ROW_STRIDE = 75;
    const int NAME_Y0    = 60;

    make_data_name_label(scr, "RPM",            NAME_Y0 + 0 * ROW_STRIDE);
    data_rpm_val_label   = make_data_val_label(scr, "--",  NAME_Y0 + 0 * ROW_STRIDE + 20);

    make_data_name_label(scr, "THROTTLE",       NAME_Y0 + 1 * ROW_STRIDE);
    data_tps_val_label   = make_data_val_label(scr, "--",  NAME_Y0 + 1 * ROW_STRIDE + 20);

    make_data_name_label(scr, "COOLANT TEMP",   NAME_Y0 + 2 * ROW_STRIDE);
    data_coolant_val_label = make_data_val_label(scr, "--", NAME_Y0 + 2 * ROW_STRIDE + 20);

    make_data_name_label(scr, "SPEED",          NAME_Y0 + 3 * ROW_STRIDE);
    data_speed_val_label = make_data_val_label(scr, "--",   NAME_Y0 + 3 * ROW_STRIDE + 20);

    make_data_name_label(scr, "FUEL PRESSURE",  NAME_Y0 + 4 * ROW_STRIDE);
    data_fuelpsi_val_label = make_data_val_label(scr, "--", NAME_Y0 + 4 * ROW_STRIDE + 20);

    data_timer = lv_timer_create(tick_data, 200, NULL);  // 5 Hz

    enable_gesture_bubble_recursive(scr);
}

void draw_splash_face(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *gif = lv_gif_create(scr);
    lv_gif_set_src(gif, &supra_light_sweep_v3_gif);
    lv_obj_add_event_cb(gif, on_splash_gif_first_draw, LV_EVENT_DRAW_POST, NULL);
    lv_obj_center(gif);
}

void setup() {
    Serial.begin(115200);
    Wire.begin(IIC_SDA, IIC_SCL);
    prefs.begin("roundie", false);

    touch.setPins(TP_RESET, TP_INT);
    touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    touch.setMirrorXY(true, true);

    init_twai_can();

    gfx->begin();
    gfx->setBrightness(200);
    gfx->fillScreen(BLACK);

    lv_init();

    uint32_t bufSize = LCD_WIDTH * LCD_HEIGHT / 2;
    buf1 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buf2 = NULL;

    if (!buf1) {
        bufSize = LCD_WIDTH * LCD_HEIGHT / 4;
        buf1 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
        buf2 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
    }

    if (!buf1) {
        Serial.println("Failed to allocate LVGL draw buffers");
        while (true) {
            delay(1000);
        }
    }

    if (buf2 == NULL) {
        snprintf(lvgl_buffer_mode, sizeof(lvgl_buffer_mode), "buf half 1x %lu", (unsigned long)bufSize);
        Serial.printf("LVGL buffer: half-screen single buffer (%lu px)\n", (unsigned long)bufSize);
    } else {
        snprintf(lvgl_buffer_mode, sizeof(lvgl_buffer_mode), "buf quarter 2x %lu", (unsigned long)bufSize);
        Serial.printf("LVGL buffer: quarter-screen double buffer fallback (%lu px each)\n", (unsigned long)bufSize);
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufSize);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.rounder_cb = my_rounder_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    indev_drv.gesture_limit = 8;
    indev_drv.gesture_min_velocity = 12;
    touch_indev = lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = &my_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 2000);

    watch_scr  = lv_obj_create(NULL);
    gauge_scr  = lv_obj_create(NULL);
    boostafr_scr = lv_obj_create(NULL);
    splash_scr = lv_obj_create(NULL);
    data_scr   = lv_obj_create(NULL);
    candbg_scr = lv_obj_create(NULL);

    lv_obj_clear_flag(watch_scr,  LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(gauge_scr,  LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(boostafr_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(splash_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(data_scr,   LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(candbg_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(watch_scr,  handle_swipe, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(gauge_scr,  handle_swipe, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(boostafr_scr, handle_swipe, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(data_scr,   handle_swipe, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(candbg_scr, handle_swipe, LV_EVENT_GESTURE, NULL);

    draw_gauge_face(gauge_scr);
    draw_boostafr_face(boostafr_scr);
    draw_data_face(data_scr);
    draw_watch_face(watch_scr);
    draw_candbg_face(candbg_scr);
    draw_splash_face(splash_scr);

    // Pause all timers while the splash plays.
    if (gauge_timer)  lv_timer_pause(gauge_timer);
    if (perf_timer)   lv_timer_pause(perf_timer);
    if (boostafr_arc_timer) lv_timer_pause(boostafr_arc_timer);
    if (boostafr_label_timer) lv_timer_pause(boostafr_label_timer);
    if (watch_timer)  lv_timer_pause(watch_timer);
    if (data_timer)   lv_timer_pause(data_timer);
    if (candbg_timer) lv_timer_pause(candbg_timer);

    uint8_t persisted = prefs.getUChar("last_screen", (uint8_t)DEMO_GAUGE);
    if (persisted >= (uint8_t)DEMO_SCREEN_COUNT) persisted = (uint8_t)DEMO_GAUGE;
    splash_target_screen = persisted;
    splash_countdown_synced = false;
    current_demo = DEMO_SCREEN_COUNT;
    lv_scr_load(splash_scr);
    splash_timer = lv_timer_create(tick_boot_splash, BOOT_SPLASH_MS, NULL);
}

void loop() {
    // Keep CAN polling alive regardless of active UI screen.
    if (current_demo != DEMO_GAUGE && current_demo != DEMO_SCREEN_COUNT) {
        update_boost_target_from_can(lv_tick_get());
    }

    lv_timer_handler();

    if (pending_screen_change) {
        pending_screen_change = false;
        DemoScreen next = (DemoScreen)pending_screen;
        // Determine animation direction from the cyclic index.
        int cur_idx = (int)current_demo;
        int nxt_idx = (int)next;
        bool forward = (nxt_idx == (cur_idx + 1) % DEMO_SCREEN_COUNT);
        lv_scr_load_anim_t anim = forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        set_demo_screen(next, anim);
    }

    // Intentionally no delay here to minimize frame cadence jitter.
}