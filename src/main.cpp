#include <Arduino.h>

#include "app_shared.h"
#include "app_ui.h"
#include "can_bus.h"
#include "navigation.h"
#include "platform.h"

static AppContext app;

void setup() {
    Serial.begin(115200);

    if (!platform_init(&app)) {
        while (true) {
            delay(1000);
        }
    }

    can_init(&app);
    app_create_ui(&app);
}

void loop() {
    can_poll(&app, lv_tick_get());
    navigation_apply_shift_light_bg(&app);

    platform_process_ui();
    navigation_handle_pending(&app);
}