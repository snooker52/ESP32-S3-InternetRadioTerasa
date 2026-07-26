# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Internet radio for the terrace ("Rádio Terasa") on a Seeed XIAO ESP32-S3, built with PlatformIO and the Arduino framework. Audio streams play through a PCM5102A I2S DAC via the ESP32-audioI2S library; a Nextion/TJC touch display connected over UART shows station, song, and signal info. UI texts, comments, and commit messages are in Czech — keep that convention. `README.md` (in Czech) documents the hardware wiring tables and user-facing overview.

Note: the git repository root is the parent directory `C:/ESP32/ESP32_projekty`, which contains multiple independent projects. Commits touching other projects appear in the shared history.

## Build and Upload Commands

```
pio run                                    # build (default env: seeed_xiao_esp32s3, debug)
pio run -e seeed_xiao_esp32s3 -t upload    # upload over serial (COM4)
pio run -e seeed_xiao_esp32s3_ota -t upload  # upload release build over serial (COM4)
pio device monitor                         # serial monitor, COM4 @ 115200 (logs also to file via log2file filter)
```

There are no tests or linters. The `seeed_xiao_esp32s3` env builds with `CORE_DEBUG_LEVEL=5` (verbose `log_*` output); the `_ota` env is a release build (same build type CI publishes to GitHub Releases, but flashed manually over serial).

WiFi credentials (`[wifi]`) and the GitHub PAT for pull OTA (`[extra] GITHUB_TOKEN`) live in `platformio.ini` and are injected as build flags (`WIFI_SSID`, `WIFI_PASS`, `GITHUB_TOKEN`).

## Architecture

Everything lives in two files: `src/main.cpp` (implementation) and `include/main.h` (declarations **plus definitions** — the station URL/name arrays, the `Audio` object, and the message struct live there; it must only ever be included from `main.cpp`).

**Audio task + queue protocol:** Audio decoding runs in a dedicated FreeRTOS task (`audioTask`) pinned to core 1, which owns the `Audio` object and calls `audio.loop()` continuously. All control from the main loop goes through a pair of queues (`audioSetQueue` / `audioGetQueue`) carrying `audioMessage` structs; `transmitReceive()` implements a blocking request/reply. The queues are created in `audioInit()` **before** the task starts — keep it that way, or early senders crash on a NULL queue. Never call `audio.*` methods directly from the main loop — use the wrappers (`audioSetVolume`, `audioConnecttohost`, ...).

**Deferred-work flag:** WiFi event handlers and audio callbacks must not do blocking work themselves. They set `pozadavekPripojitStream`, and `loop()` performs the actual stream (re)connect once WiFi is connected. This is how the stream starts after boot (from `WiFiGotIP`) and reconnects after `audio_eof_stream`. Follow the same pattern for any new work triggered from event/callback context.

**Library callbacks:** `audio_info`, `audio_showstreamtitle`, `audio_eof_stream` are weak-symbol callbacks invoked by ESP32-audioI2S (from the audio task's context). They parse stream metadata and push it to the display.

**Nextion display:** Attached to `Serial2` (pins `NEX_RX`/`NEX_TX`). Communication is write-only in practice: `send_text()` sends `<component>.txt="..."` commands terminated with `\xFF\xFF\xFF`, replaces `"` with `'` in the payload, and — like `sendCommand()` — serializes writes with the `nexMutex` semaphore, because the main loop, the audio task, and WiFi event handlers all write to the display. Any new code writing to `Serial2` must take that mutex. Component names (`stanice`, `interpret`, `pisnicka`, `signal`) are defined as `NEX_TXT_*` macros; `nextion-tjc-mappings.txt` maps component names to IDs for the Nextion vs. TJC display variants. `radio.HMI` / `radio.tft` are the display editor project and compiled firmware.

**WiFi:** Static IP (192.168.1.99) configured in `main.cpp`; connection is event-driven (`WiFi.onEvent`) with `WiFi.setAutoReconnect(true)` handling reconnects — do not call `WiFi.begin()` from event handlers. `nastavWiFi()` scans for all APs matching `WIFI_SSID` and connects directly to the strongest one by RSSI (`WiFi.begin(ssid, password, channel, bssid)`) instead of a plain `WiFi.begin(ssid, password)`, because letting ESP-IDF pick the AP can choose a weaker one when multiple APs share the same SSID. There is no push-OTA (ArduinoOTA) path anymore — pull OTA from GitHub Releases (below) is the only update mechanism, so a first firmware with the pull-OTA client must be flashed over serial once.

**CI/CD + pull OTA:** Releases are made by pushing a tag `radio-terasa-v*`; `.github/workflows/radio-terasa-release.yml` (repo root) builds with `-DFIRMWARE_VERSION` from the tag and publishes `firmware.bin` both under the pushed tag and to the moving release `radio-terasa-latest`, which the device polls (`src/github_ota.cpp` — on WiFi connect and every 6 h from `loop()`). Local builds get version `"dev"` and skip auto-update, so a dev flash is never overwritten by a release. Private-repo asset download needs the fine-grained PAT in `platformio.ini` `[extra] GITHUB_TOKEN` and a manual 302-redirect hop (no Authorization header on the signed URL). `.github/workflows/radio-terasa-ci.yml` build-checks pushes/PRs touching this project.

Large commented-out blocks (Preferences-based persistence of volume/station/network config, Nextion read-back functions) are intentional leftovers of features not yet re-enabled.
