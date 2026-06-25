#include "platform.h"

#include <Wire.h>

#include "navigation.h"

static void my_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area) {
    LV_UNUSED(disp_drv);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    AppContext *app = (AppContext *)disp->user_data;
    uint32_t width = (area->x2 - area->x1 + 1);
    uint32_t height = (area->y2 - area->y1 + 1);
    uint32_t flush_start_us = micros();
    app->platform.gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, width, height);
    app->gauge.flush_count_accum++;
    app->gauge.flush_pixels_accum += (uint64_t)width * (uint64_t)height;
    app->gauge.flush_us_accum += (uint64_t)(micros() - flush_start_us);
    lv_disp_flush_ready(disp);
}

static void my_tick(void *arg) {
    LV_UNUSED(arg);
    lv_tick_inc(2);
}

bool platform_init(AppContext *app) {
    Wire.begin(AppConfig::IIC_SDA, AppConfig::IIC_SCL);
    app->platform.prefs.begin("roundie", false);

    app->platform.touch.setPins(AppConfig::TP_RESET, AppConfig::TP_INT);
    app->platform.touch.begin(Wire, 0x5A, AppConfig::IIC_SDA, AppConfig::IIC_SCL);
    app->platform.touch.setMaxCoordinates(AppConfig::LCD_WIDTH, AppConfig::LCD_HEIGHT);
    app->platform.touch.setMirrorXY(true, true);

    app->platform.imu_ready = app->platform.imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, AppConfig::IIC_SDA, AppConfig::IIC_SCL);
    if (app->platform.imu_ready) {
        bool configured = app->platform.imu.configAccelerometer(
            SensorQMI8658::ACC_RANGE_4G,
            SensorQMI8658::ACC_ODR_250Hz,
            SensorQMI8658::LPF_MODE_3);
        app->platform.imu_ready = configured && app->platform.imu.enableAccelerometer();
    }

    if (app->platform.imu_ready) {
        Serial.println("IMU ready: QMI8658 accelerometer enabled");
    } else {
        Serial.println("IMU not available: g-force demo sensor input disabled");
    }

    app->platform.bus = new Arduino_ESP32QSPI(
        AppConfig::LCD_CS,
        AppConfig::LCD_SCLK,
        AppConfig::LCD_SDIO0,
        AppConfig::LCD_SDIO1,
        AppConfig::LCD_SDIO2,
        AppConfig::LCD_SDIO3);
    app->platform.gfx = new Arduino_CO5300(
        app->platform.bus,
        AppConfig::LCD_RESET,
        0,
        false,
        AppConfig::LCD_WIDTH,
        AppConfig::LCD_HEIGHT,
        6,
        0,
        0,
        0);

    app->platform.gfx->begin();
    app->platform.gfx->setBrightness(200);
    app->platform.gfx->fillScreen(BLACK);

    lv_init();

    uint32_t buf_size = AppConfig::LCD_WIDTH * AppConfig::LCD_HEIGHT / 2;
    app->platform.buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
    app->platform.buf2 = nullptr;

    if (!app->platform.buf1) {
        buf_size = AppConfig::LCD_WIDTH * AppConfig::LCD_HEIGHT / 4;
        app->platform.buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
        app->platform.buf2 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
    }

    if (!app->platform.buf1) {
        Serial.println("Failed to allocate LVGL draw buffers");
        return false;
    }

    if (app->platform.buf2 == nullptr) {
        snprintf(app->platform.lvgl_buffer_mode, sizeof(app->platform.lvgl_buffer_mode),
                 "buf half 1x %lu", (unsigned long)buf_size);
        Serial.printf("LVGL buffer: half-screen single buffer (%lu px)\n", (unsigned long)buf_size);
    } else {
        snprintf(app->platform.lvgl_buffer_mode, sizeof(app->platform.lvgl_buffer_mode),
                 "buf quarter 2x %lu", (unsigned long)buf_size);
        Serial.printf("LVGL buffer: quarter-screen double buffer fallback (%lu px each)\n", (unsigned long)buf_size);
    }

    lv_disp_draw_buf_init(&app->platform.draw_buf, app->platform.buf1, app->platform.buf2, buf_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = AppConfig::LCD_WIDTH;
    disp_drv.ver_res = AppConfig::LCD_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.rounder_cb = my_rounder_cb;
    disp_drv.draw_buf = &app->platform.draw_buf;
    disp_drv.user_data = app;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = navigation_touchpad_read;
    indev_drv.gesture_limit = 8;
    indev_drv.gesture_min_velocity = 12;
    indev_drv.user_data = app;
    app->platform.touch_indev = lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = &my_tick,
        .name = "lvgl_tick"
    };
    esp_timer_create(&tick_timer_args, &app->platform.tick_timer);
    esp_timer_start_periodic(app->platform.tick_timer, 2000);
    return true;
}

void platform_process_ui() {
    lv_timer_handler();
}