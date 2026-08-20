#pragma once

#include "app_shared.h"

lv_obj_t *create_alternate_boost_screen(AppContext *app);
void altboost_toggle_demo(AppContext *app, uint32_t now_ms);
void altboost_refresh_theme(AppContext *app);
