# ESP32-S3-Touch-AMOLED-1.75 - Complete Knowledge Base

> **Device**: Waveshare ESP32-S3-Touch-AMOLED-1.75
> **Model variant**: Without GPS, WITH external speaker connected
> **Purpose**: Single-source reference for AI-assisted programming of this specific device
> **Wiki**: http://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75
> **Product page**: https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm
> **GitHub**: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75

---

## 1. HARDWARE OVERVIEW

### 1.1 Processor (ESP32-S3R8)
- **SoC**: ESP32-S3R8 (dual-core Xtensa 32-bit LX7)
- **Max clock**: 240 MHz
- **SRAM**: 512 KB
- **PSRAM**: 8 MB (onboard, octal SPI)
- **Flash**: 16 MB external
- **ROM**: 384 KB
- **WiFi**: 2.4 GHz 802.11 b/g/n
- **Bluetooth**: BLE 5.0
- **Antenna**: Onboard SMD antenna + IPEX Gen 1 connector for external antenna
- **USB**: Native USB-OTG (Type-C connector for programming and serial)

### 1.2 Physical Form Factor
- Watch-style development board (round 1.75-inch AMOLED)
- Buttons: PWR (power/custom function) and BOOT (programming/debugging)
- Connectors: MX1.25 2P for battery, MX1.25 2P for speaker, 8-pin header (3 GPIOs + 1 UART)

---

## 2. COMPLETE PIN MAP (pin_config.h)

```cpp
#pragma once
#define XPOWERS_CHIP_AXP2101

// ========================
// DISPLAY (CO5300 via QSPI)
// ========================
#define LCD_SDIO0    4    // QSPI Data 0
#define LCD_SDIO1    5    // QSPI Data 1
#define LCD_SDIO2    6    // QSPI Data 2
#define LCD_SDIO3    7    // QSPI Data 3
#define LCD_SCLK    38    // QSPI Clock
#define LCD_CS      12    // Chip Select
#define LCD_RESET   39    // Reset
#define LCD_WIDTH  466    // Horizontal resolution (pixels)
#define LCD_HEIGHT 466    // Vertical resolution (pixels)

// ========================
// I2C BUS (shared by Touch, RTC, IMU, PMU, IO Expander)
// ========================
#define IIC_SDA     15    // I2C Data
#define IIC_SCL     14    // I2C Clock

// ========================
// TOUCH (CST9217 via I2C)
// ========================
#define TP_INT      11    // Touch interrupt (active LOW / FALLING edge)
#define TP_RESET    40    // Touch reset

// ========================
// AUDIO CODEC (ES8311 via I2S)
// ========================
#define MCLKPIN     42    // I2S Master Clock (MCLK)
#define BCLKPIN      9    // I2S Bit Clock (BCLK/SCK)
#define WSPIN       45    // I2S Word Select (LRCK)
#define DOPIN       10    // I2S Data Out (to codec DAC input = speaker)
#define DIPIN        8    // I2S Data In (from codec ADC output = microphone)
#define PA          46    // Power Amplifier enable (HIGH = on)

// Legacy aliases (same pins, different naming convention):
#define I2S_MCK_IO  16    // Note: some examples use GPIO16 for MCLK
#define I2S_BCK_IO   9
#define I2S_DI_IO   10
#define I2S_WS_IO   45
#define I2S_DO_IO    8

// ========================
// SD CARD (SDMMC 1-bit mode)
// ========================
const int SDMMC_CLK  =  2;   // SD Clock
const int SDMMC_CMD  =  1;   // SD Command
const int SDMMC_DATA =  3;   // SD Data 0
const int SDMMC_CS   = 41;   // SD Chip Select (for SPI mode, not used in SDMMC)
```

### 2.1 I2C Device Addresses

| Device | Address | Function |
|--------|---------|----------|
| AXP2101 (PMU) | 0x34 | Power management, battery charging |
| PCF85063 (RTC) | 0x51 | Real-time clock |
| QMI8658 (IMU) | 0x6A (QMI8658_L_SLAVE_ADDRESS) | 6-axis accelerometer + gyroscope |
| CST9217 (Touch) | 0x5A | Capacitive touch controller |
| ES8311 (Audio DAC/ADC) | 0x18 (ES8311_ADDRRES_0) | Audio codec (CE pin low) |
| ES7210 (Audio ADC) | Default addr | 4-channel ADC for dual microphones |
| TCA9554 (IO Expander) | 0x20 (ADDRESS_000) | I/O expansion |

---

## 3. DISPLAY - CO5300 AMOLED

### 3.1 Specifications
- **Size**: 1.75 inches (round)
- **Resolution**: 466 x 466 pixels
- **Colors**: 16.7M (RGB888 capable, typically used as RGB565)
- **Interface**: QSPI (Quad SPI)
- **Driver IC**: CO5300
- **Color depth used**: 16-bit RGB565 (LV_COLOR_DEPTH 16)
- **Color swap**: LV_COLOR_16_SWAP = 0
- **Visible area offset**: X gap = 6, Y gap = 0
- **DPI**: ~130 (LV_DPI_DEF 130)
- **Brightness control**: Software via command 0x51 (0-255 range)

### 3.2 Display Initialization (Arduino - GFX Library)

```cpp
#include "Arduino_GFX_Library.h"

// Create QSPI data bus
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS,    // CS  = GPIO 12
    LCD_SCLK,  // SCK = GPIO 38
    LCD_SDIO0, // D0  = GPIO 4
    LCD_SDIO1, // D1  = GPIO 5
    LCD_SDIO2, // D2  = GPIO 6
    LCD_SDIO3  // D3  = GPIO 7
);

// Create CO5300 display driver
// Parameters: bus, RST, rotation, width, height, col_offset, row_offset, col_offset_extra, row_offset_extra
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0
);

void setup() {
    Wire.begin(IIC_SDA, IIC_SCL);  // I2C MUST be initialized before display
    gfx->begin();                    // Initialize display
    gfx->setBrightness(200);         // Set brightness (0-255)
    gfx->fillScreen(RGB565_BLACK);   // Clear screen
}
```

### 3.3 Display Initialization Commands (ESP-IDF, low-level)

```c
static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},   // Pixel format: RGB565
    {0x35, (uint8_t[]){0x00}, 1, 0},   // Tearing effect line ON
    {0x53, (uint8_t[]){0x20}, 1, 0},   // Brightness control
    {0x51, (uint8_t[]){0xFF}, 1, 0},   // Max brightness
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},  // Column address (6 to 471)
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600}, // Row address (0 to 465)
    {0x11, NULL, 0, 600},  // Sleep out (600ms delay)
    {0x29, NULL, 0, 0},    // Display ON
};
```

### 3.4 Rotation via MADCTL (command 0x36)
```
0x00 = 0 degrees (normal)
0x60 = 90 degrees
0xC0 = 180 degrees
0xA0 = 270 degrees
```

### 3.5 CRITICAL: Rounder Callback

The CO5300 display requires pixel-aligned coordinates. You MUST implement a rounder callback for LVGL to avoid rendering artifacts:

**LVGL v8:**
```cpp
void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area) {
    // Round start coordinates DOWN to nearest even number
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    // Round end coordinates UP to nearest odd number
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
// Register: disp_drv.rounder_cb = example_lvgl_rounder_cb;
```

**LVGL v9:**
```c
static void rounder_event_cb(lv_event_t *e) {
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
// Register: lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
```

---

## 4. LVGL CONFIGURATION & INTEGRATION

### 4.1 LVGL Version & Key Settings
- **Version**: 8.4.0 (Arduino examples), v9 supported in ESP-IDF
- **Color depth**: 16-bit (LV_COLOR_DEPTH 16)
- **Color swap**: LV_COLOR_16_SWAP 0
- **Memory pool**: 48 KB (LV_MEM_SIZE)
- **DPI**: 130
- **Tick period**: 2 ms
- **Display refresh**: 10 ms
- **Input device read**: 10 ms
- **Default font**: lv_font_montserrat_14
- **Fonts enabled**: Montserrat 8-48 (all sizes)
- **All widgets enabled**: arc, bar, btn, btnmatrix, canvas, checkbox, dropdown, img, label, line, roller, slider, switch, textarea, table
- **Extra widgets**: animimg, calendar, chart, colorwheel, imgbtn, keyboard, led, list, menu, meter, msgbox, span, spinbox, spinner, tabview, tileview, win
- **Layouts**: Flex (flexbox) and Grid enabled
- **Themes**: Default, Basic, Mono enabled
- **Demos**: widgets, benchmark, stress, music

### 4.2 LVGL v8 Complete Setup Template (Arduino)

```cpp
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "lv_conf.h"
#include "TouchDrvCSTXXX.hpp"
#include <Wire.h>

#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

static lv_disp_draw_buf_t draw_buf;
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

// Touch
TouchDrvCST92xx touch;
int16_t tx[5], ty[5];

// Display flush callback
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

// Rounder callback (REQUIRED for CO5300)
void my_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area) {
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

// Touch read callback
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint8_t touched = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
    if (touched > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = tx[0];
        data->point.y = ty[0];
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// LVGL tick callback
void my_tick(void *arg) {
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void setup() {
    Serial.begin(115200);
    Wire.begin(IIC_SDA, IIC_SCL);

    // Initialize touch
    touch.setPins(TP_RESET, TP_INT);
    touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    touch.setMirrorXY(true, true);  // Mirror both axes for correct orientation

    // Initialize display
    gfx->begin();
    gfx->setBrightness(200);

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers from PSRAM (double buffering for best performance)
    uint32_t bufSize = LCD_WIDTH * LCD_HEIGHT / 4;
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufSize);

    // Register display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.rounder_cb = my_rounder_cb;  // REQUIRED!
    disp_drv.draw_buf = &draw_buf;
    disp_drv.sw_rotate = 1;  // Enable software rotation if needed
    lv_disp_drv_register(&disp_drv);

    // Register input (touch) driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Create LVGL tick timer
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &my_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

    // --- Your LVGL UI code here ---
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

### 4.3 Display Buffer Sizing Guidelines
- **Minimum** (low RAM): `LCD_WIDTH * LCD_HEIGHT / 10` = ~21,740 pixels (single buffer)
- **Recommended** (good performance): `LCD_WIDTH * LCD_HEIGHT / 4` = ~54,289 pixels (double buffer from PSRAM)
- **Maximum** (best performance): Full framebuffer from PSRAM

### 4.4 Design Considerations for Round Display
- The display is 466x466 pixels but **physically round** (1.75 inch circular)
- Content in the corners will be clipped by the circular bezel
- For circular UI designs, the usable inscribed circle has a diameter of 466 pixels (radius 233px from center 233,233)
- Use `lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0)` for circular elements
- Center point: (233, 233)

---

## 5. TOUCH - CST9217

### 5.1 Specifications
- **Controller**: CST9217 (or CST92xx family)
- **Interface**: I2C
- **I2C Address**: 0x5A
- **I2C Speed**: 10 kHz - 400 kHz (fast mode recommended)
- **Interrupt**: GPIO 11 (active LOW, use FALLING edge trigger)
- **Reset**: GPIO 40
- **Max touch points**: 5-point multitouch
- **Coordinate range**: Configurable, set to 466x466

### 5.2 Touch Initialization (Arduino)

```cpp
#include "TouchDrvCSTXXX.hpp"

TouchDrvCST92xx touch;
int16_t x[5], y[5];

void setup() {
    Wire.begin(IIC_SDA, IIC_SCL);  // GPIO 15, 14

    // Reset sequence
    digitalWrite(TP_RESET, LOW);
    delay(30);
    digitalWrite(TP_RESET, HIGH);
    delay(50);

    // Initialize
    touch.setPins(TP_RESET, TP_INT);
    touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    touch.setMaxCoordinates(466, 466);
    touch.setMirrorXY(true, true);  // Required for correct orientation

    // Optional: cover screen detection
    touch.setCoverScreenCallback([](void *ptr) {
        Serial.println("Screen is covered");
    }, NULL);

    // Interrupt-driven reading (recommended)
    volatile bool isPressed = false;
    attachInterrupt(TP_INT, []() { isPressed = true; }, FALLING);
}
```

### 5.3 Touch in ESP-IDF

```c
#include "esp_lcd_touch_cst9217.h"

esp_lcd_touch_config_t tp_cfg = {
    .x_max = 466,
    .y_max = 466,
    .rst_gpio_num = GPIO_NUM_40,
    .int_gpio_num = GPIO_NUM_11,
    .levels = { .reset = 0, .interrupt = 0 },
    .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 1 },
};
```

---

## 6. IMU - QMI8658 (6-Axis)

### 6.1 Specifications
- **Chip**: QMI8658C
- **Axes**: 6 (3-axis accelerometer + 3-axis gyroscope)
- **Interface**: I2C
- **I2C Address**: 0x6A (QMI8658_L_SLAVE_ADDRESS, SA0 = LOW)
- **Accelerometer ranges**: 2G, 4G, 8G, 16G
- **Gyroscope ranges**: 16/32/64/128/256/512/1024/2048 dps
- **ODR**: Up to 8000 Hz

### 6.2 Usage (Arduino)

```cpp
#include "SensorQMI8658.hpp"

SensorQMI8658 qmi;
IMUdata acc;
IMUdata gyr;

void setup() {
    Wire.begin(IIC_SDA, IIC_SCL);

    if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("QMI8658 not found!");
        while (1) delay(1000);
    }

    // Configure accelerometer
    qmi.configAccelerometer(
        SensorQMI8658::ACC_RANGE_4G,     // Range: 2G, 4G, 8G, 16G
        SensorQMI8658::ACC_ODR_1000Hz,   // Output data rate
        SensorQMI8658::LPF_MODE_0        // Low-pass filter mode
    );
    qmi.enableAccelerometer();

    // Configure gyroscope (optional)
    qmi.configGyroscope(
        SensorQMI8658::GYR_RANGE_512DPS,
        SensorQMI8658::GYR_ODR_896_8Hz,
        SensorQMI8658::LPF_MODE_3
    );
    qmi.enableGyroscope();
}

void loop() {
    if (qmi.getDataReady()) {
        if (qmi.getAccelerometer(acc.x, acc.y, acc.z)) {
            // acc.x, acc.y, acc.z are in G units (float)
        }
        if (qmi.getGyroscope(gyr.x, gyr.y, gyr.z)) {
            // gyr.x, gyr.y, gyr.z are in dps (float)
        }
    }
}
```

### 6.3 Auto-Rotation Using Accelerometer

```cpp
float angleX = acc.x;
float angleY = acc.y;

if (angleX > 0.8)       lv_disp_set_rotation(NULL, LV_DISP_ROT_NONE);
else if (angleX < -0.8)  lv_disp_set_rotation(NULL, LV_DISP_ROT_180);
else if (angleY < -0.8)  lv_disp_set_rotation(NULL, LV_DISP_ROT_270);
else if (angleY > 0.8)   lv_disp_set_rotation(NULL, LV_DISP_ROT_90);

// Note: disp_drv.sw_rotate must be set to 1 for software rotation
```

---

## 7. RTC - PCF85063

### 7.1 Specifications
- **Chip**: PCF85063A
- **Interface**: I2C
- **I2C Address**: 0x51
- **Features**: Real-time clock/calendar, alarm, timer, clock output
- **Battery backup**: Supports external battery for timekeeping during power off

### 7.2 Usage (Arduino)

```cpp
#include "SensorPCF85063.hpp"

SensorPCF85063 rtc;

void setup() {
    if (!rtc.begin(Wire, IIC_SDA, IIC_SCL)) {
        Serial.println("PCF85063 not found!");
        while (1) delay(1000);
    }

    // Set time
    rtc.setDateTime(2024, 9, 24, 11, 9, 41);
}

void loop() {
    RTC_DateTime datetime = rtc.getDateTime();
    int year   = datetime.getYear();
    int month  = datetime.getMonth();
    int day    = datetime.getDay();
    int hour   = datetime.getHour();
    int minute = datetime.getMinute();
    int second = datetime.getSecond();

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d\n%02d-%02d-%04d",
             hour, minute, second, day, month, year);
}
```

---

## 8. POWER MANAGEMENT - AXP2101

### 8.1 Specifications
- **Chip**: AXP2101
- **Interface**: I2C
- **I2C Address**: 0x34
- **Features**:
  - Li-Po battery charging (3.7V via MX1.25 2P connector)
  - Multiple voltage regulators
  - Temperature sensor
  - Battery voltage/current monitoring
  - Charge status detection
  - IRQ support (power key short press, etc.)
  - ADC for battery, VBUS, system voltage measurement

### 8.2 Usage (Arduino)

```cpp
#include "XPowersLib.h"
#define XPOWERS_CHIP_AXP2101  // Must be defined before include or in pin_config.h

XPowersPMU power;

void setup() {
    Wire.begin(IIC_SDA, IIC_SCL);

    if (!power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("AXP2101 not found!");
        while (1) delay(50);
    }

    // Configure interrupts
    power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    power.clearIrqStatus();
    power.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);  // Power key short press

    // Configure charging
    power.setChargeTargetVoltage(3);  // Target voltage index

    // Enable ADC measurements
    power.enableTemperatureMeasure();
    power.enableBattDetection();
    power.enableVbusVoltageMeasure();
    power.enableBattVoltageMeasure();
    power.enableSystemVoltageMeasure();
}

void loop() {
    // Read power status
    float temp = power.getTemperature();        // *C
    bool charging = power.isCharging();
    bool discharging = power.isDischarge();
    bool vbusIn = power.isVbusIn();
    float battV = power.getBattVoltage();        // mV
    float vbusV = power.getVbusVoltage();        // mV
    float sysV = power.getSystemVoltage();       // mV

    if (power.isBatteryConnect()) {
        int percent = power.getBatteryPercent();  // 0-100%
    }

    // Charge status
    uint8_t status = power.getChargerStatus();
    // XPOWERS_AXP2101_CHG_TRI_STATE    - trickle charge
    // XPOWERS_AXP2101_CHG_PRE_STATE    - pre-charge
    // XPOWERS_AXP2101_CHG_CC_STATE     - constant current
    // XPOWERS_AXP2101_CHG_CV_STATE     - constant voltage
    // XPOWERS_AXP2101_CHG_DONE_STATE   - charge done
    // XPOWERS_AXP2101_CHG_STOP_STATE   - not charging

    // Handle power key interrupt
    if (power.isPekeyShortPressIrq()) {
        // Power key was short pressed
    }
    power.clearIrqStatus();
}
```

### 8.3 ESP-IDF Usage

```cpp
// I2C address for AXP2101
#define AXP2101_ADDR 0x34

// Use XPowersLib component with i2c_master API
// Read/write functions must be provided:
int pmu_register_read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len);
int pmu_register_write_byte(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len);
```

---

## 9. AUDIO - ES8311 CODEC + SPEAKER

### 9.1 Specifications
- **DAC/ADC Codec**: ES8311 (I2S interface)
- **I2C Address**: 0x18 (CE pin low)
- **Microphone ADC**: ES7210 (4-channel, for dual digital microphones with echo cancellation)
- **Speaker connector**: MX1.25 2P
- **Power amplifier**: GPIO 46 (PA pin, set HIGH to enable speaker output)
- **Sample rates**: 8kHz - 96kHz
- **Resolution**: 16/18/20/24/32 bit
- **Mic gain**: 0dB to 42dB (7 levels)
- **Volume**: 0-100 (software controlled)

### 9.2 I2S Pin Configuration

```
MCLK (Master Clock) = GPIO 42
BCLK (Bit Clock)    = GPIO 9
WS/LRCK (Word Sel)  = GPIO 45
DOUT (Data Out)      = GPIO 10  (ESP32 -> ES8311 DAC -> Speaker)
DIN  (Data In)       = GPIO 8   (ES8311 ADC -> ESP32, microphone)
PA   (Power Amp)     = GPIO 46  (HIGH = speaker enabled)
```

### 9.3 Audio Playback (Arduino)

```cpp
#include "ESP_I2S.h"
#include "es8311.h"

I2SClass i2s;
#define SAMPLE_RATE     16000
#define VOICE_VOLUME    90
#define MIC_GAIN        (es8311_mic_gain_t)(3)  // ES8311_MIC_GAIN_18DB

esp_err_t es8311_codec_init() {
    es8311_handle_t es_handle = es8311_create(0, ES8311_ADDRRES_0);  // 0x18
    if (!es_handle) return ESP_FAIL;

    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = SAMPLE_RATE * 256,
        .sample_frequency = SAMPLE_RATE
    };

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es_handle, es_clk.mclk_frequency, es_clk.sample_frequency));
    ESP_ERROR_CHECK(es8311_microphone_config(es_handle, false));  // analog mic
    ESP_ERROR_CHECK(es8311_voice_volume_set(es_handle, VOICE_VOLUME, NULL));
    ESP_ERROR_CHECK(es8311_microphone_gain_set(es_handle, MIC_GAIN));
    return ESP_OK;
}

void audio_task(void *param) {
    // Configure I2S pins
    i2s.setPins(BCLKPIN, WSPIN, DIPIN, DOPIN, MCLKPIN);  // 9, 45, 8, 10, 42

    if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("I2S init failed!");
        vTaskDelete(NULL);
    }

    // Initialize ES8311 via I2C (uses separate I2C init on same bus)
    Wire.begin(IIC_SDA, IIC_SCL);  // GPIO 15, 14
    if (es8311_codec_init() != ESP_OK) {
        Serial.println("ES8311 init failed!");
        vTaskDelete(NULL);
    }

    // Play audio data
    while (1) {
        i2s.write((uint8_t *)audio_pcm_data, audio_pcm_len);
        vTaskDelay(1);
    }
}

void setup() {
    Serial.begin(115200);

    // CRITICAL: Enable power amplifier for speaker output
    pinMode(PA, OUTPUT);      // GPIO 46
    digitalWrite(PA, HIGH);   // Turn on speaker amplifier

    // Start audio on separate core
    xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 1, NULL, 1);
}
```

### 9.4 Audio in ESP-IDF (BSP approach)

```c
// Speaker initialization
esp_codec_dev_handle_t spk = bsp_audio_codec_speaker_init();
esp_codec_dev_sample_info_t fs = {
    .sample_rate = 16000,
    .channel = 1,
    .bits_per_sample = 16,
};
esp_codec_dev_set_out_vol(spk, 80);
esp_codec_dev_open(spk, &fs);
esp_codec_dev_write(spk, audio_buffer, buffer_size);
esp_codec_dev_close(spk);

// Microphone initialization
esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
```

### 9.5 ES8311 Key API

```c
es8311_handle_t es8311_create(unsigned int i2c_port, uint16_t addr);  // addr: 0x18 or 0x19
esp_err_t es8311_init(handle, clock_config, res_in, res_out);
esp_err_t es8311_voice_volume_set(handle, int volume_0_100, int *actual);
esp_err_t es8311_voice_volume_get(handle, int *volume);
esp_err_t es8311_voice_mute(handle, bool enable);
esp_err_t es8311_microphone_gain_set(handle, es8311_mic_gain_t gain);
esp_err_t es8311_microphone_config(handle, bool digital_mic);
esp_err_t es8311_sample_frequency_config(handle, int mclk_freq, int sample_freq);
```

### 9.6 Mic Gain Levels
```
ES8311_MIC_GAIN_0DB   = 0
ES8311_MIC_GAIN_6DB   = 1
ES8311_MIC_GAIN_12DB  = 2
ES8311_MIC_GAIN_18DB  = 3
ES8311_MIC_GAIN_24DB  = 4
ES8311_MIC_GAIN_30DB  = 5
ES8311_MIC_GAIN_36DB  = 6
ES8311_MIC_GAIN_42DB  = 7
```

---

## 10. SD CARD (MicroSD / TF Card)

### 10.1 Interface
- **Mode**: SDMMC 1-bit mode (recommended) or SPI mode
- **Pins**: CLK=GPIO2, CMD=GPIO1, DATA0=GPIO3
- **SPI CS**: GPIO 41 (only for SPI mode)

### 10.2 Usage (Arduino - SDMMC mode)

```cpp
#include <SD_MMC.h>

void setup() {
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);  // 2, 1, 3

    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        Serial.println("Card Mount Failed");
        return;
    }

    uint8_t cardType = SD_MMC.cardType();
    // CARD_MMC, CARD_SD, CARD_SDHC, CARD_NONE

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);  // Size in MB

    // Standard file operations
    File file = SD_MMC.open("/test.txt", FILE_WRITE);
    file.println("Hello SD Card");
    file.close();
}
```

### 10.3 Usage (ESP-IDF)

```c
sdmmc_slot_config_t slot_config = {
    .clk = GPIO_NUM_2,
    .cmd = GPIO_NUM_1,
    .d0  = GPIO_NUM_3,
    .d1  = GPIO_NUM_NC,
    .d2  = GPIO_NUM_NC,
    .d3  = GPIO_NUM_NC,
    .width = 1,  // 1-bit mode
};
```

---

## 11. IO EXPANDER - TCA9554

### 11.1 Specifications
- **Chip**: TCA9554
- **Interface**: I2C
- **I2C Address**: 0x20 (A0=A1=A2=LOW)
- **GPIO pins**: 8 additional I/O pins
- **Library**: ESP32_IO_Expander

### 11.2 Usage

```cpp
#include "ESP_IOExpander_Library.h"

// In ESP-IDF:
esp_io_expander_handle_t io_expander = NULL;
esp_io_expander_new_i2c_tca9554(i2c_handle, 0x20, &io_expander);
```

---

## 12. WiFi & BLUETOOTH

### 12.1 WiFi Capabilities
- 2.4 GHz 802.11 b/g/n
- Station (STA) + Access Point (AP) modes simultaneously
- Supports WPA/WPA2/WPA3

### 12.2 WiFi Example

```cpp
#include <WiFi.h>

// Station mode
WiFi.begin("SSID", "password");
while (WiFi.status() != WL_CONNECTED) delay(500);
Serial.println(WiFi.localIP());

// AP mode (can run simultaneously with STA)
WiFi.softAP("ESP32-AP", "password123");
Serial.println(WiFi.softAPIP());
```

### 12.3 Bluetooth
- BLE 5.0 supported
- Use standard ESP32 BLE libraries (BLEDevice, NimBLE, etc.)

---

## 13. DEVELOPMENT ENVIRONMENT SETUP

### 13.1 Arduino IDE Setup

1. **Board Manager**: Install ESP32 board package v3.3.5 (esp32 by Espressif Systems)
2. **Board Selection**: `ESP32S3 Dev Module`
3. **Board Settings**:
   - Flash Mode: QIO 80MHz
   - Flash Size: 16MB (128Mb)
   - PSRAM: OPI PSRAM
   - Partition Scheme: 16M Flash (3MB APP / 9.9MB FATFS) or similar
   - USB CDC On Boot: Enabled
   - USB Mode: Hardware CDC and JTAG

4. **Required Libraries** (place in Arduino libraries folder):
   - `GFX_Library_for_Arduino` - Display driver (includes CO5300 support)
   - `lvgl` v8.4.0 - Graphics framework
   - `lv_conf.h` - LVGL configuration (place next to lvgl folder)
   - `SensorLib` v0.3.1 - For PCF85063, QMI8658, CST9217
   - `XPowersLib` v0.2.6 - For AXP2101
   - `ESP32_IO_Expander` v0.0.3 - For TCA9554
   - `Mylibrary` - Contains `pin_config.h`

### 13.2 ESP-IDF Setup (v5.5)

1. Use standard ESP-IDF v5.5 toolchain
2. Key components:
   - `esp_lcd_co5300` - CO5300 display driver
   - `esp_lcd_touch_cst9217` - Touch driver
   - `esp_codec_dev` - Audio codec abstraction
   - `es8311` / `es7210` - Audio codec drivers
   - `esp_io_expander_tca9554` - IO expander
   - `esp_lv_adapter` - LVGL adapter (v9 compatible)

3. Key sdkconfig settings:
   - `CONFIG_BSP_I2C_NUM=1`
   - `CONFIG_BSP_I2C_CLK_SPEED_HZ=400000`
   - `CONFIG_BSP_I2S_NUM=1`

### 13.3 PlatformIO (platformio.ini example)

```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.flash_mode = qio
board_build.flash_size = 16MB
board_build.psram_type = opi
board_upload.flash_size = 16MB
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
```

---

## 14. COMPLETE INITIALIZATION ORDER

The correct initialization order is critical:

```
1. Serial.begin(115200)
2. Wire.begin(IIC_SDA, IIC_SCL)    // I2C bus - required by everything
3. Power (AXP2101) init             // PMU should be configured early
4. PA pin enable (if using speaker) // pinMode(PA, OUTPUT); digitalWrite(PA, HIGH)
5. Touch controller init            // Reset + begin + configure
6. Display init (gfx->begin())      // Initializes QSPI and CO5300
7. Display brightness               // gfx->setBrightness(200)
8. LVGL init                        // lv_init() + buffers + drivers
9. IMU init (QMI8658)               // Optional, after I2C
10. RTC init (PCF85063)             // Optional, after I2C
11. SD card init                    // Optional
12. Audio init (ES8311 + I2S)       // Optional, can run on separate core
```

---

## 15. MEMORY CONSIDERATIONS

| Resource | Amount |
|----------|--------|
| Internal SRAM | 512 KB |
| PSRAM | 8 MB |
| Flash | 16 MB |
| LVGL heap (default) | 48 KB |
| Display buffer (min) | ~43 KB (single buf, 1/10 screen) |
| Display buffer (recommended) | ~213 KB (double buf, 1/4 screen, PSRAM) |

### Tips:
- Use `heap_caps_malloc(..., MALLOC_CAP_DMA)` for display buffers
- Large assets (images, audio) should be stored on SD card or in PSRAM
- Use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for large allocations from PSRAM
- Monitor free heap: `ESP.getFreeHeap()`, `ESP.getFreePsram()`

---

## 16. GPIO SUMMARY TABLE

| GPIO | Function | Notes |
|------|----------|-------|
| 0 | BOOT button | Active LOW, input |
| 1 | SDMMC CMD | SD card command line |
| 2 | SDMMC CLK | SD card clock |
| 3 | SDMMC DATA0 | SD card data |
| 4 | LCD SDIO0 | QSPI data 0 |
| 5 | LCD SDIO1 | QSPI data 1 |
| 6 | LCD SDIO2 | QSPI data 2 |
| 7 | LCD SDIO3 | QSPI data 3 |
| 8 | I2S DIN | Audio data in (microphone) |
| 9 | I2S BCLK | Audio bit clock |
| 10 | I2S DOUT | Audio data out (speaker) |
| 11 | TP_INT | Touch interrupt (FALLING) |
| 12 | LCD CS | Display chip select |
| 14 | IIC_SCL | I2C clock (shared bus) |
| 15 | IIC_SDA | I2C data (shared bus) |
| 38 | LCD SCLK | QSPI clock |
| 39 | LCD RESET | Display reset |
| 40 | TP_RESET | Touch reset |
| 41 | SDMMC CS | SD card CS (SPI mode only) |
| 42 | I2S MCLK | Audio master clock |
| 45 | I2S WS/LRCK | Audio word select |
| 46 | PA | Power amplifier enable (speaker) |

---

## 17. COMMON PATTERNS & TIPS

### 17.1 Circular Watch Face Design
```cpp
// Draw a circular watch face that fits the round display
#define CENTER_X 233
#define CENTER_Y 233
#define RADIUS   230  // Slightly less than half of 466 to avoid clipping

// In LVGL, create a circular background
lv_obj_t *bg = lv_obj_create(lv_scr_act());
lv_obj_set_size(bg, 466, 466);
lv_obj_set_style_radius(bg, LV_RADIUS_CIRCLE, 0);
lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
lv_obj_center(bg);
```

### 17.2 Power-Efficient Loop
```cpp
void loop() {
    lv_timer_handler();  // Process LVGL tasks
    delay(5);            // 5ms is optimal; lower = smoother but more CPU
}
```

### 17.3 Deep Sleep with RTC Wakeup
```cpp
// The AXP2101 can be used to control power states
// Use power.shutdown() to turn off
// PWR button will wake the device
```

### 17.4 Handling Multiple I2C Devices
All sensors share the same I2C bus (GPIO 14/15). The I2C addresses don't conflict:
- 0x18 = ES8311 (audio)
- 0x20 = TCA9554 (IO expander)
- 0x34 = AXP2101 (power)
- 0x51 = PCF85063 (RTC)
- 0x5A = CST9217 (touch)
- 0x6A = QMI8658 (IMU)

### 17.5 Available Demos for Testing
```cpp
// LVGL built-in demos (all enabled in lv_conf.h):
lv_demo_widgets();    // Widget showcase
lv_demo_benchmark();  // Performance test
lv_demo_stress();     // Stress test
lv_demo_music();      // Music player UI
```

---

## 18. DATASHEETS & REFERENCE DOCUMENTS

| Document | Description |
|----------|-------------|
| ESP32-S3-Touch-AMOLED-1.75.pdf | Product datasheet |
| Esp32-s3_datasheet_en.pdf | ESP32-S3 chip datasheet |
| Esp32-s3_technical_reference_manual_en.pdf | ESP32-S3 full technical reference |
| ES8311.DS.pdf | ES8311 audio codec datasheet |
| ES8311.user.Guide.pdf | ES8311 user guide |
| PCF85063A.pdf | RTC datasheet |
| QMI8658C.pdf | IMU sensor datasheet |
| X-power-AXP2101_SWcharge_V1.0.pdf | AXP2101 PMU datasheet |
| ESP32-S3-Touch-AMOLED-1.75-schematic.pdf | Board schematic |

---

## 19. TROUBLESHOOTING

| Issue | Solution |
|-------|----------|
| Display shows nothing | Ensure `Wire.begin()` is called BEFORE `gfx->begin()` |
| Display shows artifacts | Implement the rounder callback (section 3.5) |
| Touch coordinates wrong | Set `touch.setMirrorXY(true, true)` and `setMaxCoordinates(466, 466)` |
| No audio from speaker | Enable PA: `pinMode(46, OUTPUT); digitalWrite(46, HIGH)` |
| I2C device not found | Check address and verify `Wire.begin(15, 14)` |
| PSRAM not detected | Set board config: PSRAM = OPI PSRAM |
| Upload fails | Hold BOOT button during upload, or enable USB CDC On Boot |
| Display too bright/dark | Use `gfx->setBrightness(0-255)` |
| LVGL crashes | Increase LV_MEM_SIZE, use PSRAM for buffers |
| SD card won't mount | Use `SD_MMC.begin("/sdcard", true)` (1-bit mode) |

---

## 20. LIBRARY VERSIONS (Tested & Working)

| Library | Version |
|---------|---------|
| Arduino ESP32 Board | 3.3.5 |
| ESP-IDF | 5.5 |
| LVGL | 8.4.0 (Arduino) / 9.x (ESP-IDF) |
| GFX Library for Arduino | Latest with CO5300 support |
| SensorLib | 0.3.1 |
| XPowersLib | 0.2.6 |
| ESP32_IO_Expander | 0.0.3 |
