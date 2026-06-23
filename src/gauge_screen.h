#pragma once

#include "app_shared.h"

lv_obj_t *create_gauge_screen(AppContext *app);
void gauge_set_profiler_visible(AppContext *app, bool visible);