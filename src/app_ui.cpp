#include "app_ui.h"

#include "altboost_combo_screen.h"
#include "alternate_boost_screen.h"
#include "boostafr_screen.h"
#include "candbg_screen.h"
#include "color_config.h"
#include "data_screen.h"
#include "gauge_screen.h"
#include "gforce_screen.h"
#include "navigation.h"
#include "splash_screen.h"
#include "watch_screen.h"

struct ScreenDescriptor {
    DemoScreen demo;
};

static void on_long_press_gauge(AppContext *app, uint32_t now_ms) {
    (void)now_ms;
    (void)app;
}

static void on_long_press_boostafr(AppContext *app, uint32_t now_ms) {
    boostafr_toggle_demo(app, now_ms);
}

static void on_long_press_altboost(AppContext *app, uint32_t now_ms) {
    altboost_toggle_demo(app, now_ms);
}

static void on_long_press_altboost_combo(AppContext *app, uint32_t now_ms) {
    altboost_combo_toggle_demo(app, now_ms);
}

static void on_long_press_data(AppContext *app, uint32_t now_ms) {
    data_toggle_demo(app, now_ms);
}

static void on_long_press_gforce(AppContext *app, uint32_t now_ms) {
    gforce_set_demo_mode(app, !app->gforce.demo_mode, now_ms);
}

static void on_double_tap_gforce(AppContext *app, uint32_t now_ms) {
    (void)now_ms;
    gforce_handle_double_tap(app);
}

static const ScreenDescriptor kScreenDescriptors[] = {
    {DEMO_GAUGE},
    {DEMO_BOOSTAFR},
    {DEMO_ALTBOOST},
    {DEMO_ALTBOOST_COMBO},
    {DEMO_DATA1},
    {DEMO_DATA2},
    {DEMO_DATA3},
    {DEMO_DATA4},
    {DEMO_WATCH},
    {DEMO_CANDBG},
    {DEMO_GFORCE},
};

static const DemoScreen kActiveScreenOrder[] = {
    DEMO_ALTBOOST,
    DEMO_ALTBOOST_COMBO,
    DEMO_DATA4,
    DEMO_DATA3,
    DEMO_DATA2,
    DEMO_CANDBG,
    DEMO_GFORCE,
};

static lv_obj_t *create_screen_for_demo(AppContext *app, DemoScreen demo) {
    switch (demo) {
        case DEMO_GAUGE: return create_gauge_screen(app);
        case DEMO_BOOSTAFR: return create_boostafr_screen(app);
        case DEMO_ALTBOOST: return create_alternate_boost_screen(app);
        case DEMO_ALTBOOST_COMBO: return create_altboost_combo_screen(app);
        case DEMO_DATA1: return create_data_screen1(app);
        case DEMO_DATA2: return create_data_screen2(app);
        case DEMO_DATA3: return create_data_screen3(app);
        case DEMO_DATA4: return create_data_screen4(app);
        case DEMO_WATCH: return create_watch_screen(app);
        case DEMO_CANDBG: return create_candbg_screen(app);
        case DEMO_GFORCE: return create_gforce_screen(app);
        default: return nullptr;
    }
}

static lv_timer_t *timer_for_demo_slot(AppContext *app, DemoScreen demo, uint8_t slot) {
    switch (demo) {
        case DEMO_GAUGE:
            if (slot == 0) return app->gauge.timer;
            if (slot == 1) return app->gauge.perf_timer;
            return nullptr;
        case DEMO_BOOSTAFR:
            if (slot == 0) return app->boostafr.arc_timer;
            if (slot == 1) return app->boostafr.label_timer;
            return nullptr;
        case DEMO_ALTBOOST:
            return slot == 0 ? app->alt_boost.timer : nullptr;
        case DEMO_ALTBOOST_COMBO:
            return slot == 0 ? app->alt_boost_combo.timer : nullptr;
        case DEMO_DATA1:
            return slot == 0 ? app->data.timer1 : nullptr;
        case DEMO_DATA2:
            return slot == 0 ? app->data.timer2 : nullptr;
        case DEMO_DATA3:
            return slot == 0 ? app->data.timer3 : nullptr;
        case DEMO_DATA4:
            return slot == 0 ? app->data.timer4 : nullptr;
        case DEMO_WATCH:
            return slot == 0 ? app->watch.timer : nullptr;
        case DEMO_CANDBG:
            return slot == 0 ? app->candbg.timer : nullptr;
        case DEMO_GFORCE:
            return slot == 0 ? app->gforce.timer : nullptr;
        default:
            return nullptr;
    }
}

static ScreenDoubleTapHandler double_tap_handler_for_demo(DemoScreen demo) {
    switch (demo) {
        case DEMO_GFORCE:
            return on_double_tap_gforce;
        default:
            return nullptr;
    }
}

static ScreenLongPressHandler long_press_handler_for_demo(DemoScreen demo) {
    switch (demo) {
        case DEMO_GAUGE: return on_long_press_gauge;
        case DEMO_BOOSTAFR: return on_long_press_boostafr;
        case DEMO_ALTBOOST: return on_long_press_altboost;
        case DEMO_ALTBOOST_COMBO: return on_long_press_altboost_combo;
        case DEMO_DATA1:
        case DEMO_DATA2:
        case DEMO_DATA3:
        case DEMO_DATA4:
            return on_long_press_data;
        case DEMO_GFORCE: return on_long_press_gforce;
        default: return nullptr;
    }
}

static void register_screen_with_timers(AppContext *app,
                                        DemoScreen demo,
                                        lv_obj_t *screen,
                                        lv_timer_t *timer0,
                                        lv_timer_t *timer1 = nullptr,
                                        lv_timer_t *timer2 = nullptr) {
    lv_timer_t *timers[3] = {timer0, timer1, timer2};
    uint8_t count = 0;
    for (uint8_t i = 0; i < 3; i++) {
        if (timers[i]) count++;
    }
    navigation_register_screen(app,
                               demo,
                               screen,
                               timers,
                               count,
                               double_tap_handler_for_demo(demo),
                               long_press_handler_for_demo(demo));
}

void app_create_ui(AppContext *app) {
    theme_load(app);

    for (const ScreenDescriptor &descriptor : kScreenDescriptors) {
        lv_obj_t *screen = create_screen_for_demo(app, descriptor.demo);
        register_screen_with_timers(app,
                                    descriptor.demo,
                                    screen,
                                    timer_for_demo_slot(app, descriptor.demo, 0),
                                    timer_for_demo_slot(app, descriptor.demo, 1),
                                    timer_for_demo_slot(app, descriptor.demo, 2));
    }

    navigation_set_active_screens(
        app,
        kActiveScreenOrder,
        (uint8_t)(sizeof(kActiveScreenOrder) / sizeof(kActiveScreenOrder[0])));

    app->nav.splash_screen = create_splash_screen(app);
    color_config_init(app);
    theme_apply_runtime(app);

    navigation_begin_boot_splash(app);
}