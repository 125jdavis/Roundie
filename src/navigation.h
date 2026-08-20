#pragma once

#include "app_shared.h"

void navigation_register_screen(AppContext *app,
								DemoScreen demo,
								lv_obj_t *screen,
								lv_timer_t **timers,
								uint8_t timer_count,
								ScreenDoubleTapHandler on_double_tap = nullptr,
								ScreenLongPressHandler on_long_press = nullptr);
void navigation_set_active_screens(AppContext *app, const DemoScreen *order, uint8_t count);
void navigation_set_demo_screen(AppContext *app, DemoScreen target, lv_scr_load_anim_t anim);
void navigation_handle_pending(AppContext *app);
void navigation_begin_boot_splash(AppContext *app);
void navigation_sync_splash_countdown(AppContext *app);
void navigation_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void navigation_apply_shift_light_bg(AppContext *app);