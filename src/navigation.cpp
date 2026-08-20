#include "navigation.h"

#include <stdlib.h>

#include "color_config.h"
#include "gforce_screen.h"

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

static void handle_double_tap_for_current_screen(AppContext *app, uint32_t now_ms) {
    if (app->nav.current_demo >= DEMO_SCREEN_COUNT) return;
    ScreenSlot &slot = app->nav.slots[app->nav.current_demo];
    if (slot.on_double_tap) {
        slot.on_double_tap(app, now_ms);
        return;
    }
    color_config_open(app);
}

static void handle_long_press_for_current_screen(AppContext *app, uint32_t now_ms) {
    if (app->nav.current_demo >= DEMO_SCREEN_COUNT) return;
    ScreenSlot &slot = app->nav.slots[app->nav.current_demo];
    if (slot.on_long_press) {
        slot.on_long_press(app, now_ms);
    }
}

static DemoScreen first_active_demo(const AppContext *app) {
    if (app->nav.active_count > 0) {
        return app->nav.active_order[0];
    }
    return DEMO_GAUGE;
}

static int active_index_of_demo(const AppContext *app, DemoScreen demo) {
    for (uint8_t i = 0; i < app->nav.active_count; i++) {
        if (app->nav.active_order[i] == demo) return (int)i;
    }
    return -1;
}

static bool is_active_demo(const AppContext *app, DemoScreen demo) {
    return active_index_of_demo(app, demo) >= 0;
}

enum TouchReleaseGesture {
    TOUCH_RELEASE_SUPPRESS,
    TOUCH_RELEASE_SWIPE_LEFT,
    TOUCH_RELEASE_SWIPE_RIGHT,
    TOUCH_RELEASE_TAP,
    TOUCH_RELEASE_LONG_PRESS,
    TOUCH_RELEASE_OTHER
};

struct TouchReleaseMetrics {
    uint32_t release_ms = 0;
    int dx = 0;
    int dy = 0;
    int adx = 0;
    int ady = 0;
    uint32_t press_ms = 0;
};

static void reset_tap_streak(AppContext *app) {
    app->nav.last_tap_ms = 0;
    app->nav.tap_streak_count = 0;
}

static TouchReleaseGesture recognize_touch_release_gesture(AppContext *app, const TouchReleaseMetrics &m) {
    if (color_config_is_visible(app)) {
        return TOUCH_RELEASE_SUPPRESS;
    }

    if (app->nav.current_demo == DEMO_GFORCE && gforce_is_config_visible(app)) {
        return TOUCH_RELEASE_SUPPRESS;
    }

    bool is_hold = (m.press_ms >= AppConfig::DEMO_HOLD_DURATION_MS) &&
                   (m.adx <= AppConfig::HOLD_MAX_MOVE_PX) &&
                   (m.ady <= AppConfig::HOLD_MAX_MOVE_PX);
    if (is_hold) {
        return TOUCH_RELEASE_LONG_PRESS;
    }

    if (m.adx > 70 && m.adx > (m.ady + 20)) {
        return (m.dx < 0) ? TOUCH_RELEASE_SWIPE_LEFT : TOUCH_RELEASE_SWIPE_RIGHT;
    }

    bool is_tap = (m.press_ms <= AppConfig::TAP_MAX_DURATION_MS) &&
                  (m.adx <= AppConfig::TAP_MAX_MOVE_PX) &&
                  (m.ady <= AppConfig::TAP_MAX_MOVE_PX);
    return is_tap ? TOUCH_RELEASE_TAP : TOUCH_RELEASE_OTHER;
}

static void queue_swipe_for_gesture(AppContext *app, TouchReleaseGesture gesture) {
    if (app->nav.active_count == 0) return;

    int current_idx = active_index_of_demo(app, app->nav.current_demo);
    if (current_idx < 0) current_idx = 0;

    int next_idx = current_idx;
    if (gesture == TOUCH_RELEASE_SWIPE_LEFT) {
        next_idx = (current_idx + 1) % app->nav.active_count;
    } else {
        next_idx = (current_idx + app->nav.active_count - 1) % app->nav.active_count;
    }

    app->nav.pending_screen = (int)app->nav.active_order[next_idx];
    app->nav.pending_screen_change = true;
    reset_tap_streak(app);
}

static void handle_tap_release(AppContext *app, uint32_t release_ms) {
    uint32_t tap_gap = release_ms - app->nav.last_tap_ms;
    int tdx = app->nav.touch_last_x - app->nav.last_tap_x;
    int tdy = app->nav.touch_last_y - app->nav.last_tap_y;
    int tap_dist_sq = tdx * tdx + tdy * tdy;
    int max_dist_sq = AppConfig::DOUBLE_TAP_MAX_DIST_PX * AppConfig::DOUBLE_TAP_MAX_DIST_PX;

    bool in_streak = app->nav.last_tap_ms > 0 &&
                     app->nav.last_tap_screen == app->nav.current_demo &&
                     tap_gap <= AppConfig::DOUBLE_TAP_GAP_MS &&
                     tap_dist_sq <= max_dist_sq;
    app->nav.tap_streak_count = in_streak ? (uint8_t)(app->nav.tap_streak_count + 1) : 1;

    if (app->nav.tap_streak_count == 2) {
        handle_double_tap_for_current_screen(app, release_ms);
        reset_tap_streak(app);
    } else {
        app->nav.last_tap_ms = release_ms;
        app->nav.last_tap_x = app->nav.touch_last_x;
        app->nav.last_tap_y = app->nav.touch_last_y;
        app->nav.last_tap_screen = app->nav.current_demo;
    }
}

static void handle_swipe(lv_event_t *event) {
    AppContext *app = (AppContext *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    if (app->nav.active_count == 0) return;
    int current_idx = active_index_of_demo(app, app->nav.current_demo);
    if (current_idx < 0) current_idx = 0;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        int next_idx = (current_idx + 1) % app->nav.active_count;
        navigation_set_demo_screen(app, app->nav.active_order[next_idx], LV_SCR_LOAD_ANIM_MOVE_LEFT);
    } else if (dir == LV_DIR_RIGHT) {
        int next_idx = (current_idx + app->nav.active_count - 1) % app->nav.active_count;
        navigation_set_demo_screen(app, app->nav.active_order[next_idx], LV_SCR_LOAD_ANIM_MOVE_RIGHT);
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

void navigation_apply_shift_light_bg(AppContext *app) {
    if (color_config_is_visible(app)) return;
    if (app->nav.current_demo >= DEMO_SCREEN_COUNT) return;

    ScreenSlot &slot = app->nav.slots[app->nav.current_demo];
    if (!slot.screen) return;

    lv_color_t bg = lv_color_hex(0x000000);
    if (app->can.shift_light_active) {
        bg = app_shift_light_color(app);
    }
    lv_obj_set_style_bg_color(slot.screen, bg, 0);
}

void navigation_register_screen(AppContext *app,
                                DemoScreen demo,
                                lv_obj_t *screen,
                                lv_timer_t **timers,
                                uint8_t timer_count,
                                ScreenDoubleTapHandler on_double_tap,
                                ScreenLongPressHandler on_long_press) {
    ScreenSlot &slot = app->nav.slots[demo];
    slot.screen = screen;
    slot.timer_count = timer_count;
    slot.on_double_tap = on_double_tap;
    slot.on_long_press = on_long_press;
    for (uint8_t i = 0; i < timer_count; i++) {
        slot.timers[i] = timers[i];
    }

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, handle_swipe, LV_EVENT_GESTURE, app);
    enable_gesture_bubble_recursive(screen);
}

void navigation_set_active_screens(AppContext *app, const DemoScreen *order, uint8_t count) {
    app->nav.active_count = 0;

    if (!order || count == 0) {
        app->nav.active_order[0] = DEMO_GAUGE;
        app->nav.active_count = 1;
        return;
    }

    for (uint8_t i = 0; i < count && i < DEMO_SCREEN_COUNT; i++) {
        DemoScreen demo = order[i];
        if (demo >= DEMO_SCREEN_COUNT) continue;
        if (is_active_demo(app, demo)) continue;
        app->nav.active_order[app->nav.active_count++] = demo;
    }

    if (app->nav.active_count == 0) {
        app->nav.active_order[0] = DEMO_GAUGE;
        app->nav.active_count = 1;
    }
}

void navigation_set_demo_screen(AppContext *app, DemoScreen target, lv_scr_load_anim_t anim) {
    if (!is_active_demo(app, target)) {
        target = first_active_demo(app);
    }

    if (target == app->nav.current_demo) return;

    app->nav.last_tap_ms = 0;
    app->nav.tap_streak_count = 0;
    app->nav.last_tap_screen = DEMO_SCREEN_COUNT;

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
    int cur_idx = active_index_of_demo(app, app->nav.current_demo);
    int next_idx = active_index_of_demo(app, next);
    if (cur_idx < 0 || next_idx < 0 || app->nav.active_count == 0) return;

    bool forward = (next_idx == (cur_idx + 1) % app->nav.active_count);
    lv_scr_load_anim_t anim = forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    navigation_set_demo_screen(app, next, anim);
}

void navigation_begin_boot_splash(AppContext *app) {
    for (int i = 0; i < DEMO_SCREEN_COUNT; i++) {
        pause_slot_timers(app->nav.slots[i]);
    }

    DemoScreen default_demo = first_active_demo(app);
    uint8_t persisted = app->platform.prefs.getUChar("last_screen", (uint8_t)default_demo);
    if (persisted >= (uint8_t)DEMO_SCREEN_COUNT || !is_active_demo(app, (DemoScreen)persisted)) {
        persisted = (uint8_t)default_demo;
    }
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
            TouchReleaseMetrics metrics;
            metrics.release_ms = lv_tick_get();
            metrics.dx = app->nav.touch_last_x - app->nav.touch_start_x;
            metrics.dy = app->nav.touch_last_y - app->nav.touch_start_y;
            metrics.adx = abs(metrics.dx);
            metrics.ady = abs(metrics.dy);
            metrics.press_ms = metrics.release_ms - app->nav.touch_start_ms;

            TouchReleaseGesture gesture = recognize_touch_release_gesture(app, metrics);
            switch (gesture) {
                case TOUCH_RELEASE_SUPPRESS:
                    reset_tap_streak(app);
                    break;
                case TOUCH_RELEASE_SWIPE_LEFT:
                case TOUCH_RELEASE_SWIPE_RIGHT:
                    queue_swipe_for_gesture(app, gesture);
                    break;
                case TOUCH_RELEASE_TAP:
                    handle_tap_release(app, metrics.release_ms);
                    break;
                case TOUCH_RELEASE_LONG_PRESS:
                    handle_long_press_for_current_screen(app, metrics.release_ms);
                    reset_tap_streak(app);
                    break;
                case TOUCH_RELEASE_OTHER:
                default:
                    reset_tap_streak(app);
                    app->nav.last_tap_screen = DEMO_SCREEN_COUNT;
                    break;
            }
        }

        app->nav.touch_down = false;
    }
}