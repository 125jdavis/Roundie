#include "navigation.h"

#include <stdlib.h>

#include "boostafr_screen.h"
#include "gauge_screen.h"

static void enable_gesture_bubble_recursive(lv_obj_t *obj) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    uint32_t child_count = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        enable_gesture_bubble_recursive(lv_obj_get_child(obj, i));
    }
}

static void pause_slot_timers(ScreenSlot &slot) {
    for (uint8_t i = 0; i < slot.timer_count; i++) {
        if (slot.timers[i]) lv_timer_pause(slot.timers[i]);
    }
}

static void resume_slot_timers(ScreenSlot &slot) {
    for (uint8_t i = 0; i < slot.timer_count; i++) {
        if (slot.timers[i]) lv_timer_resume(slot.timers[i]);
    }
}

static void handle_swipe(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        int next = ((int)app->nav.current_demo + 1) % DEMO_SCREEN_COUNT;
        navigation_set_demo_screen(app, (DemoScreen)next, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    } else if (dir == LV_DIR_RIGHT) {
        int next = ((int)app->nav.current_demo + DEMO_SCREEN_COUNT - 1) % DEMO_SCREEN_COUNT;
        navigation_set_demo_screen(app, (DemoScreen)next, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }
}

static void tick_boot_splash(lv_timer_t *timer) {
    AppContext *app = (AppContext *)timer->user_data;
    if (app->nav.splash_timer) {
        lv_timer_del(app->nav.splash_timer);
        app->nav.splash_timer = nullptr;
    }

    app->nav.current_demo = DEMO_SCREEN_COUNT;
    navigation_set_demo_screen(app, (DemoScreen)app->nav.splash_target_screen, LV_SCR_LOAD_ANIM_NONE);
}

void navigation_register_screen(AppContext *app, DemoScreen demo, lv_obj_t *screen, lv_timer_t **timers, uint8_t timer_count) {
    ScreenSlot &slot = app->nav.slots[demo];
    slot.screen = screen;
    slot.timer_count = timer_count;
    for (uint8_t i = 0; i < timer_count; i++) {
        slot.timers[i] = timers[i];
    }

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, handle_swipe, LV_EVENT_GESTURE, app);
    enable_gesture_bubble_recursive(screen);
}

void navigation_set_demo_screen(AppContext *app, DemoScreen target, lv_scr_load_anim_t anim) {
    if (target == app->nav.current_demo) return;

    for (int i = 0; i < DEMO_SCREEN_COUNT; i++) {
        pause_slot_timers(app->nav.slots[i]);
    }

    ScreenSlot &slot = app->nav.slots[target];
    if (slot.screen) {
        resume_slot_timers(slot);
        lv_scr_load_anim(slot.screen, anim, 220, 0, false);
    }

    app->nav.current_demo = target;
    app->platform.prefs.putUChar("last_screen", (uint8_t)target);
}

void navigation_handle_pending(AppContext *app) {
    if (!app->nav.pending_screen_change) return;

    app->nav.pending_screen_change = false;
    DemoScreen next = (DemoScreen)app->nav.pending_screen;
    int cur_idx = (int)app->nav.current_demo;
    int next_idx = (int)next;
    bool forward = (next_idx == (cur_idx + 1) % DEMO_SCREEN_COUNT);
    lv_scr_load_anim_t anim = forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    navigation_set_demo_screen(app, next, anim);
}

void navigation_begin_boot_splash(AppContext *app) {
    for (int i = 0; i < DEMO_SCREEN_COUNT; i++) {
        pause_slot_timers(app->nav.slots[i]);
    }

    uint8_t persisted = app->platform.prefs.getUChar("last_screen", (uint8_t)DEMO_GAUGE);
    if (persisted >= (uint8_t)DEMO_SCREEN_COUNT) persisted = (uint8_t)DEMO_GAUGE;
    app->nav.splash_target_screen = persisted;
    app->nav.splash_countdown_synced = false;
    app->nav.current_demo = DEMO_SCREEN_COUNT;
    lv_scr_load(app->nav.splash_screen);
    app->nav.splash_timer = lv_timer_create(tick_boot_splash, AppConfig::BOOT_SPLASH_MS, app);
}

void navigation_sync_splash_countdown(AppContext *app) {
    if (app->nav.splash_countdown_synced || !app->nav.splash_timer) return;
    app->nav.splash_countdown_synced = true;
    lv_timer_reset(app->nav.splash_timer);
}

void navigation_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    AppContext *app = (AppContext *)indev_driver->user_data;
    uint8_t touched = app->platform.touch.getPoint(app->platform.tx, app->platform.ty, app->platform.touch.getSupportTouchPoint());
    if (touched > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = app->platform.tx[0];
        data->point.y = app->platform.ty[0];

        if (!app->nav.touch_down) {
            app->nav.touch_down = true;
            app->nav.touch_start_x = app->platform.tx[0];
            app->nav.touch_start_y = app->platform.ty[0];
            app->nav.touch_start_ms = lv_tick_get();
        }
        app->nav.touch_last_x = app->platform.tx[0];
        app->nav.touch_last_y = app->platform.ty[0];
    } else {
        data->state = LV_INDEV_STATE_REL;

        if (app->nav.touch_down) {
            uint32_t release_ms = lv_tick_get();
            int dx = app->nav.touch_last_x - app->nav.touch_start_x;
            int dy = app->nav.touch_last_y - app->nav.touch_start_y;
            int adx = abs(dx);
            int ady = abs(dy);
            uint32_t press_ms = release_ms - app->nav.touch_start_ms;

            if (adx > 70 && adx > (ady + 20)) {
                if (dx < 0) {
                    app->nav.pending_screen = ((int)app->nav.current_demo + 1) % DEMO_SCREEN_COUNT;
                } else {
                    app->nav.pending_screen = ((int)app->nav.current_demo + DEMO_SCREEN_COUNT - 1) % DEMO_SCREEN_COUNT;
                }
                app->nav.pending_screen_change = true;
            } else {
                bool is_tap = (press_ms <= AppConfig::TAP_MAX_DURATION_MS) &&
                              (adx <= AppConfig::TAP_MAX_MOVE_PX) &&
                              (ady <= AppConfig::TAP_MAX_MOVE_PX);
                if (is_tap) {
                    uint32_t tap_gap = release_ms - app->nav.last_tap_ms;
                    int tdx = app->nav.touch_last_x - app->nav.last_tap_x;
                    int tdy = app->nav.touch_last_y - app->nav.last_tap_y;
                    int tap_dist_sq = tdx * tdx + tdy * tdy;
                    int max_dist_sq = AppConfig::DOUBLE_TAP_MAX_DIST_PX * AppConfig::DOUBLE_TAP_MAX_DIST_PX;

                    if (app->nav.last_tap_ms > 0 && tap_gap <= AppConfig::DOUBLE_TAP_GAP_MS && tap_dist_sq <= max_dist_sq) {
                        if (app->nav.current_demo == DEMO_BOOSTAFR) {
                            boostafr_toggle_demo(app, release_ms);
                        } else {
                            gauge_set_profiler_visible(app, !app->gauge.profiler_visible);
                        }
                        app->nav.last_tap_ms = 0;
                    } else {
                        app->nav.last_tap_ms = release_ms;
                        app->nav.last_tap_x = app->nav.touch_last_x;
                        app->nav.last_tap_y = app->nav.touch_last_y;
                    }
                }
            }
        }

        app->nav.touch_down = false;
    }
}