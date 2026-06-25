#include "can_bus.h"

static twai_timing_config_t can_timing_config_for_profile(CanBitrateProfile profile) {
    switch (profile) {
        case CAN_RATE_1M: return TWAI_TIMING_CONFIG_1MBITS();
        case CAN_RATE_500K: return TWAI_TIMING_CONFIG_500KBITS();
        case CAN_RATE_250K: return TWAI_TIMING_CONFIG_250KBITS();
        default: return TWAI_TIMING_CONFIG_1MBITS();
    }
}

static inline uint16_t be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static inline int16_t sbe16(const uint8_t *data) {
    return (int16_t)be16(data);
}

static inline float kelvin_to_c(float kelvin) {
    return kelvin - 273.15f;
}

const char *can_rate_profile_text(CanBitrateProfile profile) {
    switch (profile) {
        case CAN_RATE_1M: return "1M";
        case CAN_RATE_500K: return "500K";
        case CAN_RATE_250K: return "250K";
        default: return "?";
    }
}

const char *can_database_text(CanDatabase database) {
    switch (database) {
        case CAN_DB_HALTECH_PROTOCOL: return "Haltech Protocol";
        case CAN_DB_DANIEL_IKE_GAUGE: return "Daniel Ike Gauge";
        case CAN_DB_MEGASQUIRT_PLACEHOLDER: return "Megasquirt (TODO)";
        case CAN_DB_OBD2_PLACEHOLDER: return "OBD2 (TODO)";
        default: return "Unknown";
    }
}

static CanBitrateProfile can_default_rate_for_database(CanDatabase database) {
    switch (database) {
        case CAN_DB_DANIEL_IKE_GAUGE: return CAN_RATE_500K;
        case CAN_DB_HALTECH_PROTOCOL:
        case CAN_DB_MEGASQUIRT_PLACEHOLDER:
        case CAN_DB_OBD2_PLACEHOLDER:
        default:
            return CAN_RATE_1M;
    }
}

static bool haltech_process_frame(AppContext *app, const twai_message_t &msg, uint32_t now_ms) {
    HaltechData &ht = app->can.ht;

    if (msg.extd || msg.rtr) return false;

    switch (msg.identifier) {
        case 0x360:
            if (msg.data_length_code < 6) return false;
            ht.rpm = be16(msg.data + 0);
            ht.map_kpa_abs = be16(msg.data + 2) * 0.1f;
            ht.tps_pct = be16(msg.data + 4) * 0.1f;
            ht.last_0x360_ms = now_ms;
            return true;

        case 0x361:
            if (msg.data_length_code < 4) return false;
            ht.fuel_press_kpa = be16(msg.data + 0) * 0.1f - AppConfig::ATMOSPHERIC_KPA;
            ht.oil_press_kpa = be16(msg.data + 2) * 0.1f - AppConfig::ATMOSPHERIC_KPA;
            ht.last_0x361_ms = now_ms;
            break;

        case 0x371:
            if (msg.data_length_code < 2) return false;
            ht.fuel_flow_cc_min = be16(msg.data + 0);
            ht.last_0x371_ms = now_ms;
            break;

        case 0x368:
            if (msg.data_length_code < 4) return false;
            ht.lambda1 = be16(msg.data + 0) * 0.001f;
            ht.lambda2 = be16(msg.data + 2) * 0.001f;
            ht.last_0x368_ms = now_ms;
            break;

        case 0x370:
            if (msg.data_length_code < 2) return false;
            ht.wheel_speed_kph = be16(msg.data + 0) * 0.1f;
            ht.last_0x370_ms = now_ms;
            break;

        case 0x372:
            if (msg.data_length_code < 8) return false;
            ht.battery_volts = be16(msg.data + 0) * 0.1f;
            ht.target_boost_kpa = be16(msg.data + 4) * 0.1f;
            ht.baro_kpa_abs = be16(msg.data + 6) * 0.1f;
            ht.last_0x372_ms = now_ms;
            break;

        case 0x376:
            if (msg.data_length_code < 2) return false;
            ht.ambient_temp_c = kelvin_to_c(be16(msg.data + 0) * 0.1f);
            ht.last_0x376_ms = now_ms;
            break;

        case 0x36B:
            if (msg.data_length_code < 8) return false;
            ht.lateral_g_ms2 = sbe16(msg.data + 6) * 0.1f;
            ht.last_0x36B_ms = now_ms;
            break;

        case 0x36E:
            if (msg.data_length_code < 8) return false;
            ht.longitudinal_g_ms2 = sbe16(msg.data + 6) * 0.1f;
            ht.last_0x36E_ms = now_ms;
            break;

        case 0x3E0:
            if (msg.data_length_code < 8) return false;
            ht.coolant_temp_c = kelvin_to_c(be16(msg.data + 0) * 0.1f);
            ht.air_temp_c = kelvin_to_c(be16(msg.data + 2) * 0.1f);
            ht.fuel_temp_c = kelvin_to_c(be16(msg.data + 4) * 0.1f);
            ht.oil_temp_c = kelvin_to_c(be16(msg.data + 6) * 0.1f);
            ht.last_0x3E0_ms = now_ms;
            break;

        case 0x3E9:
            if (msg.data_length_code < 6) return false;
            ht.target_lambda = be16(msg.data + 4) * 0.001f;
            ht.last_0x3E9_ms = now_ms;
            break;

        default:
            break;
    }

    return false;
}

static bool daniel_ike_process_frame(AppContext *app, const twai_message_t &msg, uint32_t now_ms) {
    HaltechData &ht = app->can.ht;

    if (msg.rtr) return false;

    if (msg.extd) {
        if (msg.identifier == 0x180) {
            if (msg.data_length_code < 2) return false;
            ht.lambda1 = be16(msg.data + 0) * 0.0001f;
            ht.last_0x368_ms = now_ms;
        }
        return false;
    }

    switch (msg.identifier) {
        case 0x360:
            if (msg.data_length_code < 6) return false;
            ht.rpm = be16(msg.data + 0);
            ht.map_kpa_abs = be16(msg.data + 2) * 0.1f;
            ht.tps_pct = be16(msg.data + 4) * 0.1f;
            ht.last_0x360_ms = now_ms;
            return true;

        case 0x361:
            if (msg.data_length_code < 2) return false;
            ht.fuel_press_kpa = be16(msg.data + 0) * 0.1f - 101.0f;
            ht.last_0x361_ms = now_ms;
            break;

        case 0x370:
            if (msg.data_length_code < 2) return false;
            ht.wheel_speed_kph = be16(msg.data + 0) * 0.1f;
            ht.last_0x370_ms = now_ms;
            break;

        case 0x372:
            if (msg.data_length_code < 6) return false;
            ht.battery_volts = be16(msg.data + 0) * 0.1f;
            ht.target_boost_kpa = be16(msg.data + 4) * 0.1f;
            ht.last_0x372_ms = now_ms;
            break;

        case 0x3E0:
            if (msg.data_length_code < 4) return false;
            ht.coolant_temp_c = be16(msg.data + 0) * 0.1f;
            ht.air_temp_c = be16(msg.data + 2) * 0.1f;
            if (msg.data_length_code >= 6) ht.fuel_temp_c = be16(msg.data + 4) * 0.1f;
            if (msg.data_length_code >= 8) ht.oil_temp_c = be16(msg.data + 6) * 0.1f;
            ht.last_0x3E0_ms = now_ms;
            break;

        case 0x3E1:
            if (msg.data_length_code < 6) return false;
            ht.fuel_comp_pct = be16(msg.data + 4) * 0.1f;
            ht.last_0x3E1_ms = now_ms;
            break;

        case 0x3E9:
            if (msg.data_length_code < 6) return false;
            ht.target_lambda = be16(msg.data + 4) * 0.001f;
            ht.last_0x3E9_ms = now_ms;
            break;

        case 0x470:
            if (msg.data_length_code < 8) return false;
            ht.gear = (int8_t)msg.data[7];
            ht.last_0x470_ms = now_ms;
            break;

        default:
            break;
    }

    return false;
}

static bool megasquirt_process_frame_placeholder(AppContext *app, const twai_message_t &msg, uint32_t now_ms) {
    (void)app;
    (void)msg;
    (void)now_ms;
    return false;
}

static bool obd2_process_frame_placeholder(AppContext *app, const twai_message_t &msg, uint32_t now_ms) {
    (void)app;
    (void)msg;
    (void)now_ms;
    return false;
}

static bool process_frame_by_database(AppContext *app, const twai_message_t &msg, uint32_t now_ms) {
    switch (app->can.can_database) {
        case CAN_DB_HALTECH_PROTOCOL:
            return haltech_process_frame(app, msg, now_ms);
        case CAN_DB_DANIEL_IKE_GAUGE:
            return daniel_ike_process_frame(app, msg, now_ms);
        case CAN_DB_MEGASQUIRT_PLACEHOLDER:
            return megasquirt_process_frame_placeholder(app, msg, now_ms);
        case CAN_DB_OBD2_PLACEHOLDER:
            return obd2_process_frame_placeholder(app, msg, now_ms);
        default:
            return false;
    }
}

static void can_try_autoscan(AppContext *app, uint32_t now_ms);

static void can_restart_driver(AppContext *app) {
    if (app->can.driver_installed) {
        twai_stop();
        twai_driver_uninstall();
        app->can.driver_installed = false;
    }

    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(AppConfig::CAN_TX_GPIO, AppConfig::CAN_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t timing = can_timing_config_for_profile(app->can.rate_profile);
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&general, &timing, &filter);
    if (err != ESP_OK) {
        Serial.printf("TWAI install failed (%d)\n", (int)err);
        app->can.twai_ready = false;
        return;
    }
    app->can.driver_installed = true;

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("TWAI start failed (%d)\n", (int)err);
        app->can.twai_ready = false;
        twai_driver_uninstall();
        app->can.driver_installed = false;
        return;
    }

    Serial.printf("TWAI ready: %s TX=%d RX=%d\n",
                  can_rate_profile_text(app->can.rate_profile),
                  (int)AppConfig::CAN_TX_GPIO,
                  (int)AppConfig::CAN_RX_GPIO);
    Serial.printf("CAN database: %s\n", can_database_text(app->can.can_database));
    app->can.twai_ready = true;
    app->can.boost_can_last_valid_ms = 0;
    app->can.last_bus_error_count = 0;
    app->can.last_autoscan_ms = lv_tick_get();
    app->can.frames_since_last_sample = 0;
}

void can_init(AppContext *app) {
    app->can.can_database = AppConfig::DEFAULT_CAN_DATABASE;
    app->can.rate_profile = can_default_rate_for_database(app->can.can_database);
    can_restart_driver(app);
}

void can_poll(AppContext *app, uint32_t now_ms) {
    if (!app->can.twai_ready) {
        app->can.boost_psi = 0.0f;
        return;
    }

    twai_message_t msg;
    bool got_boost = false;
    float latest_psi = 0.0f;

    while (twai_receive(&msg, 0) == ESP_OK) {
        app->can.frames_since_last_sample++;
        if (process_frame_by_database(app, msg, now_ms)) {
            float kpa_gauge = app->can.ht.map_kpa_abs - AppConfig::ATMOSPHERIC_KPA;
            float psi = kpa_gauge * AppConfig::KPA_TO_PSI;
            latest_psi = clamp_boost_psi(psi);
            got_boost = true;
        }
    }

    if (got_boost) {
        app->can.boost_psi = latest_psi;
        app->can.boost_can_last_valid_ms = now_ms;
        return;
    }

    if (app->can.boost_can_last_valid_ms == 0 ||
        (now_ms - app->can.boost_can_last_valid_ms) > AppConfig::BOOST_CAN_TIMEOUT_MS) {
        app->can.boost_psi = 0.0f;
    }

    can_try_autoscan(app, now_ms);
}

static void can_try_autoscan(AppContext *app, uint32_t now_ms) {
    if (!AppConfig::CAN_AUTOSCAN_ENABLED) return;
    if (!app->can.twai_ready) return;
    if ((now_ms - app->can.last_autoscan_ms) < AppConfig::CAN_AUTOSCAN_INTERVAL_MS) return;

    app->can.last_autoscan_ms = now_ms;

    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) return;

    bool no_valid_map = (app->can.boost_can_last_valid_ms == 0) ||
        ((now_ms - app->can.boost_can_last_valid_ms) > AppConfig::CAN_AUTOSCAN_INTERVAL_MS);
    bool bus_errors_rising = status.bus_error_count > (app->can.last_bus_error_count + 20);
    bool severe_bus_state = (status.state == TWAI_STATE_BUS_OFF) || (status.state == TWAI_STATE_RECOVERING);

    app->can.last_bus_error_count = status.bus_error_count;

    if (no_valid_map && (bus_errors_rising || severe_bus_state)) {
        CanBitrateProfile next = (CanBitrateProfile)(((int)app->can.rate_profile + 1) % CAN_RATE_COUNT);
        Serial.printf("CAN autoscan: %s -> %s (bus err %lu, state %d)\n",
                      can_rate_profile_text(app->can.rate_profile),
                      can_rate_profile_text(next),
                      (unsigned long)status.bus_error_count,
                      (int)status.state);
        app->can.rate_profile = next;
        can_restart_driver(app);
    }
}
