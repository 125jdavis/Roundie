#pragma once

#include "app_shared.h"

void can_init(AppContext *app);
void can_poll(AppContext *app, uint32_t now_ms);
const char *can_rate_profile_text(CanBitrateProfile profile);
const char *can_database_text(CanDatabase database);