# PlatformIO Agent Notes — Waveshare ESP32-S3-Touch-AMOLED-1.75

This document records hard-won lessons from real development sessions on this hardware.
Read this before writing or debugging any code for this device.

---

## 1. PlatformIO Board ID

The correct board ID for `platformio.ini` is:

```
board = esp32-s3-devkitc-1
```

Do **not** use `esp32-s3-devkitc-1-n8` — PlatformIO will throw `UnknownBoard` even though the
IDE's board picker shows that name. Verify available IDs with:

```
platformio boards | findstr /i "esp32-s3"   # Windows
platformio boards | grep -i "esp32-s3"      # Mac/Linux
```

---

## 2. Recommended platformio.ini

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DLV_CONF_INCLUDE_SIMPLE
    -I"${PROJECT_DIR}"
upload_speed = 921600
monitor_speed = 115200
lib_deps =
    moononournation/GFX Library for Arduino@1.4.9
    lvgl/lvgl@^8.4.0
```

Pin the GFX library to exactly `1.4.9`. Version 1.6.x requires a newer Arduino core header
(`esp32-hal-periman.h`) that is not present in the current PlatformIO ESP32 toolchain and will
fail to compile.

---

## 3. Serial Monitor on Windows

The ESP32-S3 uses native USB-CDC. On Windows, `Serial.begin()` requires these build flags or
output will not appear in the serial monitor:

```ini
-DARDUINO_USB_CDC_ON_BOOT=1
-DARDUINO_USB_MODE=1
```

Use `Serial` (not `USBSerial`) in code. Only one application can hold the COM port at a time —
close Arduino IDE or any other serial monitor before opening PlatformIO's monitor.

---

## 4. CO5300 Display: Single-Pixel Rendering Is Broken

The CO5300 AMOLED driver IC requires pixel data to be written in even-aligned coordinate blocks.
As a result, when using the Arduino GFX library directly:

- `drawPixel()` — does not work
- `drawLine()` — does not work
- `drawRect()` (outline only) — does not work

**Only filled shape functions work reliably:** `fillRect()`, `fillCircle()`, `fillTriangle()`, etc.

### Workaround for borders and outlines (GFX library)

Use layered filled rectangles. Draw the border color first at a slightly larger size, then draw
the fill color on top:

```cpp
// Red border around a white square
gfx->fillRect(x - 4, y - 4, size + 8, size + 8, RED);   // border layer
gfx->fillRect(x,     y,     size,     size,     WHITE);  // fill layer
```

### Proper fix: use LVGL

LVGL's rounder callback corrects coordinate alignment before any draw call reaches the hardware.
With LVGL, all drawing functions including lines, outlines, and single pixels work correctly.
**LVGL is the recommended rendering path for this display.**

---

## 5. LVGL Setup Requirements

### lv_conf.h placement

Place `lv_conf.h` in the project root (same folder as `platformio.ini`). Add these to
`build_flags` so LVGL finds it:

```ini
-DLV_CONF_INCLUDE_SIMPLE
-I"${PROJECT_DIR}"
```

The quotes around `${PROJECT_DIR}` are required on Windows if the project path contains spaces.
Without quotes, the linker will fail with `cannot find -lC:\path\with spaces\...`.

### Rounder callback — REQUIRED

The CO5300 requires even-aligned pixel coordinates. Register this callback or rendering
artifacts will appear:

```cpp
void my_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area) {
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
// Register:
disp_drv.rounder_cb = my_rounder_cb;
```

### Display flush callback

```cpp
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}
```

### LVGL tick timer

```cpp
void my_tick(void *arg) { lv_tick_inc(2); }

const esp_timer_create_args_t tick_timer_args = {
    .callback = &my_tick,
    .name = "lvgl_tick"
};
esp_timer_handle_t tick_timer = NULL;
esp_timer_create(&tick_timer_args, &tick_timer);
esp_timer_start_periodic(tick_timer, 2000);  // every 2ms
```

### Draw buffers — allocate from PSRAM

```cpp
uint32_t bufSize = LCD_WIDTH * LCD_HEIGHT / 4;
lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufSize);
```

---

## 6. LVGL Canvas Buffer Sizing

When using `lv_canvas_create()`, the buffer size depends on the image format:

| Format | Bytes per pixel | Buffer size formula |
|--------|----------------|---------------------|
| `LV_IMG_CF_TRUE_COLOR` | 2 | `W * H * sizeof(lv_color_t)` |
| `LV_IMG_CF_TRUE_COLOR_ALPHA` | 4 | `W * H * sizeof(lv_color_t) * 2` |

Using the wrong size causes silent crashes and a blank screen. Allocate canvas buffers from
PSRAM:

```cpp
// True color with alpha (for transparent overlays like clock hands)
lv_color_t *canvas_buf = (lv_color_t *)heap_caps_malloc(
    LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t) * 2, MALLOC_CAP_SPIRAM);
lv_canvas_set_buffer(canvas, canvas_buf, LCD_WIDTH, LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR_ALPHA);
```

---

## 7. LVGL Canvas Line Drawing

When passing point arrays to `lv_canvas_draw_line()`, declare them as named variables first.
Passing temporary array literals causes a compile error (`taking address of temporary array`):

```cpp
// WRONG — will not compile
lv_canvas_draw_line(canvas, (lv_point_t[]){{x1, y1}, {x2, y2}}, 2, &dsc);

// CORRECT
lv_point_t pts[] = {{(lv_coord_t)x1, (lv_coord_t)y1}, {(lv_coord_t)x2, (lv_coord_t)y2}};
lv_canvas_draw_line(canvas, pts, 2, &dsc);
```

---

## 8. Display Initialization Order

This order is critical. Deviating from it can cause a blank display or I2C device failures:

```
1. Serial.begin(115200)
2. Wire.begin(IIC_SDA, IIC_SCL)   // Must come before gfx->begin()
3. gfx->begin()
4. gfx->setBrightness(200)
5. lv_init()
6. Allocate LVGL buffers
7. Register display driver (with rounder_cb)
8. Start LVGL tick timer
9. Build UI
```

---

## 9. Display Constructor

The correct constructor for the CO5300 with this board includes a `false` parameter for the
IPS flag. Omitting it or using a different signature may compile but produce incorrect output:

```cpp
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, false, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
//                   ^^^^^
//                   IPS = false, required for correct colors
```

Without `false`, the display may show a pink tint instead of black backgrounds.

---

## 10. Pin Reference (Quick Summary)

| Function | GPIO |
|----------|------|
| LCD SDIO0–3 | 4, 5, 6, 7 |
| LCD SCLK | 38 |
| LCD CS | 12 |
| LCD RESET | 39 |
| I2C SDA | 15 |
| I2C SCL | 14 |
| Touch INT | 11 |
| Touch RESET | 40 |

Full pin map: see `ESP32-S3-Touch-AMOLED-1.75-KNOWLEDGE-BASE.md`

---

## 11. LVGL Font Lessons Learned (Custom TTF)

When custom LVGL fonts compile but text is invisible, use this sequence:

1. Verify glyph lookup at runtime before assuming layout issues.
    - Use `lv_font_get_glyph_dsc(font, &dsc, '0', 0)` for all required symbols.
    - If this returns false for any required character, regenerate with correct `--symbols`.

2. Prefer uncompressed generated fonts on this target when diagnosing rendering failures.
    - Generate with `--no-compress` in `lv_font_conv`.
    - Confirm generated file has `.bitmap_format = 0`.
    - This fixed a case where glyph probes passed but rendered numbers were still blank.

3. Keep include style consistent with the project.
    - Normalize generated files to `#include "lvgl.h"`.
    - Mixed include forms can work, but standardizing removes one variable while debugging.

4. Enable large font offsets in LVGL config for custom large fonts.
    - `lv_conf.h` must contain `#define LV_FONT_FMT_TXT_LARGE 1`.

5. Do controlled A/B tests on-screen.
    - Put fallback (`lv_font_montserrat_48`) and custom font side-by-side on the same screen.
    - This separates font decode problems from z-order/position/style issues quickly.
