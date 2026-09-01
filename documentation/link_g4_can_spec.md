# Link G4+ CAN Bus Spec (Daniel Ike Gauge mapping)

This describes the exact CAN bus configuration and frame layout that this
firmware's `CAN_DB_DANIEL_IKE_GAUGE` parser (`daniel_ike_process_frame()` in
[src/can_bus.cpp](../src/can_bus.cpp)) expects. Configure the Link G4+ CAN
Setup / Generic Dash stream to match every row below exactly, then verify on
the gauge's CAN debug screen before trusting decoded values.

## Bus configuration

| Setting | Value |
|---|---|
| Bitrate | 500 Kbit/s |
| CAN ID length | 11-bit (standard), except the lambda frame below (29-bit extended) |
| Byte order | Big-endian / Motorola MSB-first for all multi-byte values |
| Termination | 120Ω at both bus ends (ECU + gauge), single twisted pair |

> **Firmware selection note:** `AppConfig::DEFAULT_CAN_DATABASE` in
> [src/app_shared.h](../src/app_shared.h) currently defaults to
> `CAN_DB_HALTECH_PROTOCOL`. It must be changed to `CAN_DB_DANIEL_IKE_GAUGE`
> (and firmware rebuilt/reflashed) for this mapping to be used — the database
> is fixed at boot and is not selectable at runtime.

## Frame layout

| CAN ID | Type | Bytes | Sign | Conversion (raw → physical) | Signal |
|---|---|---|---|---|---|
| 0x360 | std | 0-1 | unsigned | y = x | RPM |
| 0x360 | std | 2-3 | unsigned | y = x × 0.1 | Manifold Pressure (kPa abs) |
| 0x360 | std | 4-5 | unsigned | y = x × 0.1 | Throttle Position (%) |
| 0x361 | std | 0-1 | unsigned | y = x × 0.1 − 101.0 | Fuel Pressure (kPa gauge) |
| 0x370 | std | 0-1 | unsigned | y = x × 0.1 | Vehicle/Wheel Speed (km/h) |
| 0x372 | std | 0-1 | unsigned | y = x × 0.1 | Battery Voltage (V) |
| 0x372 | std | 4-5 | unsigned | y = x × 0.1 | Target Boost Level (kPa) |
| 0x3E0 | std | 0-1 | unsigned | y = x × 0.1 | Coolant Temperature (°C, direct — not Kelvin) |
| 0x3E0 | std | 2-3 | unsigned | y = x × 0.1 | Air/Intake Temperature (°C) |
| 0x3E0 | std | 4-5 | unsigned | y = x × 0.1 | Ambient Air Temperature (°C) |
| 0x3E1 | std | 0-1 | unsigned | y = x × 0.1 | Trip Distance (km) |
| 0x3E1 | std | 2-3 | unsigned | y = x × 0.1 | Instantaneous Fuel Consumption (L/100km) |
| 0x3E1 | std | 4-5 | unsigned | y = x × 0.1 | Fuel Composition (%) |
| 0x3E1 | std | 6-7 | unsigned | y = x × 0.1 | Trip Fuel Consumption (L/100km) |
| 0x3E4 | std | bit 20 (MSB-first) | bool | 0 = Off, 1 = On | Shift Light Active |
| 0x3E9 | std | 4-5 | unsigned | y = x × 0.001 | Target Lambda |
| 0x470 | std | 7 | signed (int8) | y = x | Gear (raw gear number) |
| 0x180 | **extended (29-bit)** | 0-1 | unsigned | y = x × 0.0001 | Lambda 1 |

Notes:
- Bytes not listed for a given CAN ID (e.g. 0x372 bytes 2-3, 0x3E9 bytes 0-3)
  are unused by this parser and can be left as padding/other channels.
- The 101.0 fuel pressure offset here is intentionally different from the
  102.3-based offset used by this firmware's separate Haltech-protocol
  parser — use 101.0 for this mapping.
- 0x180 is the only extended-ID (29-bit) frame; all others are standard
  11-bit IDs.

## How to verify

1. Configure the Link G4+ CAN Setup stream(s) to broadcast the frames above
   at 500 Kbit/s with matching IDs, byte positions, and scaling.
2. On the gauge, open the CAN debug screen and confirm the raw frame IDs and
   byte values arriving on the bus match this table before relying on the
   decoded gauge readings.
3. If frames are missing or a value looks wrong (e.g. wildly out of range),
   double check byte order (must be MSB-first) and the exact raw scale/offset
   per row above.
