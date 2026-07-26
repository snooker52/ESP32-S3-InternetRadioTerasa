# ESP32-S3 Internetové rádio na terasu

Internetové rádio postavené na desce **Seeed XIAO ESP32-S3**. Přehrává MP3 streamy z internetu přes I2S DAC **PCM5102A** a zobrazuje informace o stanici, interpretovi, písničce a síle WiFi signálu na dotykovém displeji **Nextion / TJC**. Firmware podporuje aktualizace vzduchem (OTA).

## Hardware

- **Seeed XIAO ESP32-S3** — řídicí deska
- **PCM5102A** — I2S stereo DAC
- **Nextion nebo TJC displej** — připojený přes UART (Serial2)

### Zapojení PCM5102A

| Pin PCM5102A | Připojení |
|---|---|
| VCC, 3.3V | 3.3 V |
| GND | GND |
| FLT | GND |
| DMP | GND |
| SCL | GND |
| BCK | D2 |
| DIN | D1 |
| LCK | D3 |
| FMT | GND |
| XMT | 3.3 V |

### Zapojení displeje

| Signál | Pin ESP32-S3 |
|---|---|
| RX (Serial2) | D9 |
| TX (Serial2) | D10 |

Komunikace s displejem běží na 115200 baud.

## Funkce

- Přehrávání internetových MP3 streamů (knihovna [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S))
- Seznam stanic definovaný v `include/main.h` (Rádio BLANÍK, Radionetz, Das Hitradio, Antenne, Sender KW, Irish Pub Radio)
- Zobrazení stanice, interpreta, písničky a RSSI na displeji
- Automatické znovupřipojení WiFi i streamu po výpadku
- Upozornění na slabý signál (pomalý stream)
- Automatická OTA aktualizace firmwaru z GitHub Releases

## Architektura firmwaru

Celý firmware je ve dvou souborech: `src/main.cpp` (implementace) a `include/main.h` (deklarace + seznam stanic).

- **Audio task** — dekódování audia běží v samostatném FreeRTOS tasku připnutém na jádro 1, které vlastní objekt `Audio`. Veškeré ovládání z hlavní smyčky jde přes dvojici front (`audioSetQueue` / `audioGetQueue`) blokujícím protokolem požadavek/odpověď (`transmitReceive()`). Metody `audio.*` se nikdy nevolají přímo — používají se wrappery `audioSetVolume()`, `audioConnecttohost()` atd.
- **Callbacky knihovny** — `audio_info`, `audio_showstreamtitle`, `audio_eof_stream` volá ESP32-audioI2S z kontextu audio tasku; parsují metadata streamu a posílají je na displej.
- **Displej** — příkazy `<komponenta>.txt="..."` ukončené `\xFF\xFF\xFF` posílá `send_text()` přes Serial2. Zápis je chráněný mutexem (píší na něj hlavní smyčka, audio task i WiFi eventy). Názvy komponent: `stanice`, `interpret`, `pisnicka`, `signal` — mapování na ID pro obě varianty displeje je v `nextion-tjc-mappings.txt`.
- **WiFi** — statická IP `192.168.1.99`, připojení řízené eventy (`WiFi.onEvent`), automatický reconnect. Stream se připojuje až po získání IP adresy (příznak zpracovávaný v `loop()`), stejný mechanismus zajišťuje reconnect po konci streamu.

## Soubory projektu

| Soubor | Popis |
|---|---|
| `src/main.cpp` | hlavní implementace |
| `include/main.h` | deklarace, seznam stanic a jejich názvů |
| `radio.HMI` | projekt displeje pro Nextion/TJC editor |
| `radio.tft` | zkompilovaný firmware displeje |
| `nextion-tjc-mappings.txt` | mapování názvů komponent na ID (Nextion vs. TJC) |
| `platformio.ini` | konfigurace buildu, WiFi přihlašovací údaje, GitHub token pro OTA |

## Konfigurace

WiFi přihlašovací údaje a GitHub token pro stahování OTA aktualizací jsou v `platformio.ini` a do kódu se předávají jako build flagy:

```ini
[extra]
GITHUB_TOKEN = "..."

[wifi]
ssid = "..."
password = "..."
```

Síťové nastavení (statická IP, brána, maska, DNS) je v `src/main.cpp`.

## Kompilace a nahrání

Projekt používá [PlatformIO](https://platformio.org/).

```
pio run                                      # build (výchozí prostředí, debug)
pio run -e seeed_xiao_esp32s3 -t upload      # nahrání přes sériový port (COM4)
pio run -e seeed_xiao_esp32s3_ota -t upload  # nahrání release buildu přes sériový port (COM4)
pio device monitor                           # sériový monitor, 115200 baud (loguje i do souboru)
```

Prostředí `seeed_xiao_esp32s3` je debug build s podrobným logováním (`CORE_DEBUG_LEVEL=5`), prostředí `seeed_xiao_esp32s3_ota` je release build — stejný jako publikuje CI pro GitHub release, ale nahraný ručně po sériové lince.

## CI/CD a vydávání verzí

Firmware se vydává přes GitHub Actions a rádio se aktualizuje samo (pull OTA z GitHub Releases):

1. **Každý push do `main`** (se změnou v tomto projektu) spustí kontrolní build (`.github/workflows/radio-terasa-ci.yml`).
2. **Vydání nové verze** — vytvoř a pushni tag s prefixem projektu:
   ```
   git tag radio-terasa-v1.0.0
   git push origin radio-terasa-v1.0.0
   ```
   Workflow `.github/workflows/radio-terasa-release.yml` zkompiluje firmware s verzí z tagu (`-DFIRMWARE_VERSION`) a publikuje `firmware.bin` do dvou releases: pod pushnutým tagem (historie verzí) a do **pohyblivého release `radio-terasa-latest`**, který polluje zařízení.
3. **Rádio** kontroluje release `radio-terasa-latest` hned po připojení k WiFi a pak každých 6 hodin (`src/github_ota.cpp`). Když se verze release liší od běžící verze, zastaví přehrávání, stáhne `firmware.bin`, nainstaluje ho přes `Update` a restartuje se. Průběh se zobrazuje na displeji.

### Pravidla

- **Lokální build má verzi `dev`** a automatickou aktualizaci přeskakuje — firmware nahraný z PC při vývoji se sám nepřepíše releasem.
- Repo je privátní, proto zařízení potřebuje **fine-grained PAT** (GitHub → Settings → Developer settings → Fine-grained tokens; oprávnění *Contents: Read* pro repo `ESP32_projekty`). Token se vkládá do `platformio.ini` → `[extra] GITHUB_TOKEN`.
- Stahování assetu z privátního repa jde přes 302 redirect na podepsanou URL — na ni se Authorization hlavička neposílá (řeší `github_ota.cpp`).

### Bootstrap (jednorázově)

První firmware s pull OTA klientem je potřeba nahrát ručně sériově. Poté už se zařízení aktualizuje samo z GitHubu.
