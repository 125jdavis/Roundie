#pragma once

#include "app_shared.h"

void theme_load(AppContext *app);
void theme_apply_runtime(AppContext *app);
void theme_reset_defaults(AppContext *app);
void theme_save(AppContext *app);

void color_config_init(AppContext *app);
void color_config_open(AppContext *app);
void color_config_close(AppContext *app);
bool color_config_is_visible(const AppContext *app);
