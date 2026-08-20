#pragma once

#include "app_shared.h"

bool platform_init(AppContext *app, void (*touch_read_cb)(lv_indev_drv_t *, lv_indev_data_t *));
void platform_process_ui();