#include "app_ui.h"

#include "boostafr_screen.h"
#include "candbg_screen.h"
#include "data_screen.h"
#include "gauge_screen.h"
#include "gforce_screen.h"
#include "navigation.h"
#include "splash_screen.h"
#include "watch_screen.h"

void app_create_ui(AppContext *app) {
    create_gauge_screen(app);
    create_boostafr_screen(app);
    create_data_screen1(app);
    create_data_screen2(app);
    create_data_screen3(app);
    create_data_screen4(app);
    create_watch_screen(app);
    create_candbg_screen(app);
    create_gforce_screen(app);
    app->nav.splash_screen = create_splash_screen(app);

    lv_timer_t *gauge_timers[] = {app->gauge.timer, app->gauge.perf_timer};
    navigation_register_screen(app, DEMO_GAUGE, app->gauge.screen, gauge_timers, 2);

    lv_timer_t *boostafr_timers[] = {app->boostafr.arc_timer, app->boostafr.label_timer};
    navigation_register_screen(app, DEMO_BOOSTAFR, app->boostafr.screen, boostafr_timers, 2);

    lv_timer_t *data1_timers[] = {app->data.timer1};
    navigation_register_screen(app, DEMO_DATA1, app->data.screen1, data1_timers, 1);

    lv_timer_t *data2_timers[] = {app->data.timer2};
    navigation_register_screen(app, DEMO_DATA2, app->data.screen2, data2_timers, 1);

    lv_timer_t *data3_timers[] = {app->data.timer3};
    navigation_register_screen(app, DEMO_DATA3, app->data.screen3, data3_timers, 1);

    lv_timer_t *data4_timers[] = {app->data.timer4};
    navigation_register_screen(app, DEMO_DATA4, app->data.screen4, data4_timers, 1);

    lv_timer_t *watch_timers[] = {app->watch.timer};
    navigation_register_screen(app, DEMO_WATCH, app->watch.screen, watch_timers, 1);

    lv_timer_t *candbg_timers[] = {app->candbg.timer};
    navigation_register_screen(app, DEMO_CANDBG, app->candbg.screen, candbg_timers, 1);

    lv_timer_t *gforce_timers[] = {app->gforce.timer};
    navigation_register_screen(app, DEMO_GFORCE, app->gforce.screen, gforce_timers, 1);

    navigation_begin_boot_splash(app);
}