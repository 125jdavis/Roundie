#pragma once

#include "app_shared.h"

lv_obj_t *create_gforce_screen(AppContext *app);
void gforce_clear_peak_memory(AppContext *app);
void gforce_set_demo_mode(AppContext *app, bool enabled, uint32_t now_ms);
void gforce_handle_double_tap(AppContext *app);
bool gforce_is_config_visible(const AppContext *app);
