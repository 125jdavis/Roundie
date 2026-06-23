#include "app_ui.h"

#include "boostafr_screen.h"
#include "candbg_screen.h"
#include "data_screen.h"
#include "gauge_screen.h"
#include "navigation.h"
#include "splash_screen.h"
#include "watch_screen.h"

void app_create_ui(AppContext *app) {
    create_gauge_screen(app);
    create_boostafr_screen(app);
    create_data_screen(app);
    create_watch_screen(app);
    create_candbg_screen(app);
    app->nav.splash_screen = create_splash_screen(app);

    lv_timer_t *gauge_timers[] = {app->gauge.timer, app->gauge.perf_timer};
    navigation_register_screen(app, DEMO_GAUGE, app->gauge.screen, gauge_timers, 2);

    lv_timer_t *boostafr_timers[] = {app->boostafr.arc_timer, app->boostafr.label_timer};
    navigation_register_screen(app, DEMO_BOOSTAFR, app->boostafr.screen, boostafr_timers, 2);

    lv_timer_t *data_timers[] = {app->data.timer};
    navigation_register_screen(app, DEMO_DATA, app->data.screen, data_timers, 1);

    lv_timer_t *watch_timers[] = {app->watch.timer};
    navigation_register_screen(app, DEMO_WATCH, app->watch.screen, watch_timers, 1);

    lv_timer_t *candbg_timers[] = {app->candbg.timer};
    navigation_register_screen(app, DEMO_CANDBG, app->candbg.screen, candbg_timers, 1);

    navigation_begin_boot_splash(app);
}