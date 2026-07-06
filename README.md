# Cam(p)Bell

An ESP32-CAM based smart doorbell / security camera with Telegram integration.

The device sits armed on your wall, captures photos on trigger, and pushes
them straight into a Telegram chat. Arm/disarm and query status remotely
from the same chat. Field-updatable over WiFi.

Runs on the AI-Thinker ESP32-CAM.

---

## Features

- **Local dashboard** on a 128×64 OLED: armed state, temperature, "time since last event"
- **Telegram bot** for remote control: `/pic`, `/arm`, `/stats`
- **On-device push button** for local arm/disarm and photo capture
- **Runtime provisioning** — no rebuild needed to change WiFi/backend creds
- **OTA firmware updates** — hold the button 5 s, device pulls the latest build
- **Factory reset** — hold 10 s, drops into a WiFi AP with a captive-style setup form
- **Persistent armed state** in NVS, survives reboot

## Hardware

| Component        | Interface | Notes |
|------------------|-----------|-------|
| AI-Thinker ESP32-CAM | -    | 4 MB flash, PSRAM required |
| SH1106 128×64 OLED | I²C (0x3C) | U8g2 driver |
| BMP280           | I²C (0x76 or 0x77) | Temperature + pressure |
| Push button      | GPIO14 → GND | INPUT_PULLUP, single momentary |
| Onboard flash LED | GPIO4 | Used for status blinks and photo flash |

### Pinout

Defined in `include/config.h`:

| GPIO | Role |
|------|------|
| 3    | I²C SDA (shared with UART0 RX — Serial stops working after Wire.begin) |
| 1    | I²C SCL (shared with UART0 TX) |
| 4    | Flash LED |
| 14   | Button |
| 13   | Reserved (was knock sensor input) |
| 0,5,18,19,21–27,32,34–36,39 | Camera |

## Button behavior

| Input                    | Result |
|--------------------------|--------|
| Single click             | Arm ↔ Disarm |
| Double click             | Capture + send photo to Telegram |
| Hold 5 s → release       | OTA update from backend |
| Hold 10 s                | Reboot into setup AP |

Single-click classification is deferred by `DOUBLE_CLICK_MS` (400 ms) to
leave a window for the second click, so arm toggle fires ~0.4 s after
release. Any hold ≥ 3 s replaces the dashboard with a live "hold to X"
progress bar so the user sees what's about to happen.

## Setup (first boot / factory reset)

1. On first boot with no saved config, or after a 10 s button hold:
2. Device becomes a WiFi AP: `Cam(p)Bell` / `password`
3. OLED shows `SETUP MODE` + AP name + `192.168.4.1`
4. Connect a phone/laptop to that AP, open `http://192.168.4.1`
5. Fill in WiFi SSID/password, backend URL, backend username/password,
   Telegram fields
6. Save → device shows `OK`, reboots, and comes up in normal operation

Saved credentials persist in NVS (namespace `cfg`) and survive OTA updates.

## Building

Requires PlatformIO.

```bash
pio run                                 # build only
pio run -t upload                       # build + flash over USB
pio device monitor                      # serial console
```

Board target: `esp32cam` (see `platformio.ini`).

## OTA deployment

The `deploy.sh` script copies the built firmware to the backend's firmware
directory and writes a `meta.json` alongside it with version + md5 + size:

```bash
pio run
./deploy.sh 2.5                         # arg is the version string
```

Then trigger the OTA on the device by holding the button 5 s. The device
reads `/firmware/meta`, downloads `/firmware/latest.bin` if the version
differs, verifies MD5, flashes, and reboots.

---

## Backend contract

The firmware assumes a self-hosted HTTPS backend (Node/Express in this
project's case, but any implementation of the endpoints below will work).
The device authenticates with a JWT obtained from `POST /login`, then
attaches `Authorization: Bearer <token>` to every subsequent call.

Every endpoint except `/login`, `/telegram/webhook`, and the firmware
endpoints (which are pulled after JWT anyway) is auth-protected.

### Firmware → backend

#### `POST /login`
```json
Request:  { "username": "...", "password": "..." }
Response: { "token": "..." }
```
Called once at boot. Silently retried on any subsequent 401/403.

#### `POST /telegram`
```json
Request:  { "message": "..." }
```
Free-form text message relayed to the configured Telegram chat. Used by
the firmware to send arm/disarm confirmations and boot notices.

#### `POST /telegram/photo`
```
Request:  raw JPEG bytes, Content-Type: image/jpeg
Optional query: ?caption=<text>
```
Photo relayed to Telegram. Used on double-click, on `/pic` command, and
on any physical trigger (when re-enabled).

#### `GET /firmware/meta`
```json
Response: { "version": "2.5", "size": 968157, "md5": "..." }
```
Read on every OTA attempt. Firmware compares `version` to its own
compiled `FIRMWARE_VERSION`; if they differ it proceeds to download.

#### `GET /firmware/latest.bin`
```
Response: raw firmware binary (application/octet-stream)
```
Streamed with `Content-Length` set; firmware verifies against the
`meta.json` md5 before finalizing the flash.

#### `GET /device/command`
```json
Response: { "cmd": "pic" | "arm" | "stats" | null }
```
**Polled every `COMMAND_POLL_INTERVAL_MS` (500 ms)**. Backend pops the
next queued command off its internal FIFO (populated by the Telegram
webhook). Return `{"cmd": null}` when the queue is empty.

Recognized commands:
- `pic`    → firmware captures + POSTs `/telegram/photo`
- `arm`    → firmware toggles armed state, POSTs `/telegram` with the new mode
- `stats`  → firmware POSTs `/device/info`

#### `POST /device/info`
```json
Request:
{
  "armed": true,
  "tempC": 23.5,
  "pressureHpa": 1013.2,
  "voltageV": 0.00,
  "uptimeSeconds": 12345,
  "hadEvent": true,
  "lastEventSecondsAgo": 320,
  "firmware": "2.5"
}
```
Backend formats this into a human-readable message and pushes it to
Telegram. Called in response to `/stats`.

`voltageV` is always `0.00` in the current firmware — there is no voltage
divider wired to a free ADC pin. Reserved field.

### Telegram → backend

#### `POST /telegram/webhook`  *(public — no auth)*
Called by Telegram once per user message (registered via `setWebhook`).
Backend must:
1. Verify `message.chat.id` matches the configured `TELEGRAM_CHAT_ID`
   (silently drop otherwise — this is the actual auth boundary).
2. Extract `message.text`, normalize (strip `/`, lowercase, strip `@botname`).
3. Map to a device command:
   - `pic` or `photo`  → `pic`
   - `arm`             → `arm`
   - `stats` or `info` → `stats`
   - anything else     → optionally reply with help text.
4. Push mapped command onto the queue drained by `/device/command`.
5. Always respond `200` promptly — Telegram retries slow webhooks.

Reference implementation lives in `marexdevserver/backend/src/index.js`.

---

## Directory layout

```
include/
  config.h            Pin map + timing constants + FIRMWARE_VERSION
  credentials.h       NVS-backed Credentials struct + loader
  factory_mode.h      Setup AP + provisioning form
  display.h           OLED dashboard, boot log, hold-progress, OTA screens
  state.h             Persistent armed flag + last-event tracker
  server_api.h        JWT + Telegram + command polling + info push
  bmp280_helper.h     I²C wrapper
  wifi_mgr.h          STA connect
  ota.h               Meta check + download + Update.h flash
  knock.h             Unused — ISR-based knock detection (hardware retired)
  motion.h            Unused — camera frame differencing (too slow on-device)
src/
  main.cpp            Setup + main loop + button state machine
  factory_mode.cpp    AP + captive form + NVS persist
  display.cpp
  state.cpp
  server_api.cpp
  bmp280_helper.cpp
  wifi_mgr.cpp
  ota.cpp
  camera.cpp          esp_camera init + JPEG capture
  knock.cpp           Unused
  motion.cpp          Unused
platformio.ini
deploy.sh             Copies firmware.bin + writes meta.json for OTA
```

`knock.cpp` / `motion.cpp` are compiled but not called — kept for the
next hardware iteration.
