# Zprovoznění OTA aktualizací — srovnání s projektem Dilna a návrh úprav

## 1. Aktuální stav

Tento projekt (Rádio Terasa) už **má kompletně napsaný** pull-OTA mechanismus (`src/github_ota.cpp`, `include/github_ota.h`) i oba GitHub Actions workflow (`radio-terasa-ci.yml`, `radio-terasa-release.yml` v `C:\ESP32\ESP32_projekty\.github\workflows\`). Jde v podstatě o port stejného principu, jaký už prokazatelně funguje v sourozeneckém projektu `ESP32-S3-InternetRadioDilna` (tam už vyšlo 6 reálných releasů `v1.0.0`–`v1.0.5` a OTA cyklus byl ověřen end-to-end).

Rozdíl je v tom, že u Terasy **kód nikdy nebyl aktivován ani vyzkoušen**:
- `platformio.ini` → `[extra] GITHUB_TOKEN` je pořád placeholder `"VLOZ_SEM_GITHUB_TOKEN"`.
- Nebyl pushnutý žádný tag `radio-terasa-v*`, takže neexistuje release `radio-terasa-latest`, který zařízení kontroluje.
- Zařízení nikdy neběželo s jinou verzí než `dev`, takže `gitHubOtaCheck()` se vždy jen přeskočí (`strcmp(FIRMWARE_VERSION, "dev") == 0`).

Toto přesně odpovídá nedokončenému checklistu ve stávajícím `TODO.md`. Níže je ten checklist doplněný o postřehy z Dilnina reálného bring-upu (viz bod 4).

## 2. Srovnání s Dilnou

Tabulka je čistě informativní — žádný z rozdílů níže není chyba, jde o architektonické odlišnosti dané tím, že Terasa běží v jiném repu (sdílený `ESP32_projekty` s více projekty) a má audio na samostatném FreeRTOS tasku:

| Aspekt | Dilna (ověřeno, 6 releasů) | Terasa (napsáno, neaktivováno) |
|---|---|---|
| Umístění OTA kódu | přímo v `src/main.cpp` | samostatné `src/github_ota.cpp` + `include/github_ota.h` |
| Parsování JSON odpovědi GitHubu | ruční string parsing (`jsonHodnota()`) | ArduinoJson v7 s filtrem (`bblanchon/ArduinoJson@^7`) |
| Zápis firmwaru | `httpUpdate.update()` (vysokoúrovňové) | ruční `Update.begin()` / `writeStream()` / `end()` |
| Sentinel "lokální/dev build" | prázdný `FIRMWARE_VERSION` | `FIRMWARE_VERSION == "dev"` |
| Název build-flagu s tokenem | `OTA_GITHUB_TOKEN` | `GITHUB_TOKEN` |
| Cílový release na GitHubu | `releases/latest` (nativní GitHub koncept) | vlastní pohyblivý alias `radio-terasa-latest` — nutné, protože sdílený repo `ESP32_projekty` obsahuje releasy i dalších projektů a nativní "latest" by je míchal |
| Interval kontroly | 24 h | 6 h |
| CI build-check workflow | žádný | `radio-terasa-ci.yml` (build na push/PR) |

Žádnou z těchto položek není potřeba sjednocovat — obě řešení jsou funkčně rovnocenná, jen odlišně implementovaná kvůli jinému kontextu projektu.

> **Aktualizováno:** Terasa dříve měla navíc záložní cestu ArduinoOTA (push OTA, port 3232, heslem chráněné přes `OTA_PASSWORD`). Ta byla odstraněna, aby projekt odpovídal Dilně 1:1 — jediná cesta aktualizace teď je pull OTA z GitHub Releases. Bootstrap zařízení (viz bod 5) proto musí jít vždy sériově, `espota.py` už není podporované.

## 3. Specifika Terasy (displej TJC, hlasitost potenciometrem) — beze změny

Terasa používá displej **TJC**, ne pravý Nextion, a hlasitost zesilovače se nastavuje **fyzickým potenciometrem**, ne softwarově z displeje. Toto už je v současném kódu správně zohledněno a **pro OTA to nevyžaduje žádnou úpravu**:

- Component názvy použité při OTA (`NEX_TXT_INTERPRET`, `NEX_TXT_PISNICKA`) odpovídají TJC variantě z `nextion-tjc/nextion-tjc-mappings.txt` (sekce „TJC display“: `interpret` id 4, `pisnicka` id 5).
- TJC sekce mapovacího souboru nemá žádnou položku `hlasitost` (na rozdíl od Nextion sekce, kde `hlasitost` = id 2) — odpovídá tomu, že displej žádný ovládací prvek hlasitosti nemá.
- Makro `NEX_TXT_HLASITOST` v kódu vůbec neexistuje a funkce `nastav_hlasitost()` v `main.cpp` je nevolaný mrtvý kód — to je v pořádku, hlasitost neřídí firmware.

Není tedy co portovat ani upravovat kvůli displeji nebo hlasitosti.

## 4. Navrhovaná úprava kódu — progress feedback na displeji během aktualizace

> **Hotovo:** tato úprava byla implementovaná (`otaProgress()` v `main.cpp` + `Update.onProgress()` v `stahniAInstaluj()`, `src/github_ota.cpp`).

Jediný reálný rozdíl v UX: Dilnino `provedOTA()` ukazuje uživateli postup stahování/flashování v 10% krocích na poli `pisnicka`, zatímco Terasino `stahniAInstaluj()` (v `src/github_ota.cpp`, řádky 24–77) po zobrazení jedné statické zprávy ("Aktualizuji firmware na X") už nic dalšího na displej neposílá, dokud update neskončí (nebo neselže) — u pomalejšího WiFi to může vypadat jako zaseknutý displej.

Dilna to řeší takto (`main.cpp`, `provedOTA()`):

```cpp
send_text(NEX_TXT_INTERPRET, "Aktualizuji firmware...");
send_text(NEX_TXT_PISNICKA, "");
Update.onProgress([](size_t hotovo, size_t celkem) {
  static uint8_t posledniDesitka = 255;
  uint8_t procent = celkem ? (hotovo * 100) / celkem : 0;
  if (procent / 10 != posledniDesitka) {
    posledniDesitka = procent / 10;
    send_text(NEX_TXT_PISNICKA, String(procent) + " %");
  }
});
```

`Update.onProgress()` je součástí `Update` API bez ohledu na to, jestli se pak volá `httpUpdate.update()` nebo ručně `Update.begin()/writeStream()/end()` — dá se tedy stejně použít i v Terasině stávajícím kódu. Návrh úpravy `stahniAInstaluj()` v `src/github_ota.cpp`:

```cpp
static bool stahniAInstaluj(uint32_t assetId, size_t velikost) {
    // ... beze zmeny az po ziskani podepsaneUrl ...

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, podepsanaUrl)) return false;
    nastavHlavicky(http, false);
    int kod = http.GET();
    if (kod != HTTP_CODE_OK) {
        log_e("stazeni firmwaru: HTTP %d", kod);
        http.end();
        return false;
    }

    if (!Update.begin(velikost)) {
        log_e("Update.begin: %s", Update.errorString());
        http.end();
        return false;
    }

    send_text(NEX_TXT_PISNICKA, "");           // NOVE: vycistit pole pred zobrazenim procent
    Update.onProgress([](size_t hotovo, size_t celkem) {   // NOVE
        static uint8_t posledniDesitka = 255;
        uint8_t procent = celkem ? (hotovo * 100) / celkem : 0;
        if (procent / 10 != posledniDesitka) {
            posledniDesitka = procent / 10;
            send_text(NEX_TXT_PISNICKA, String(procent) + " %");
        }
    });

    size_t zapsano = Update.writeStream(http.getStream());
    http.end();
    if (zapsano != velikost) {
        log_e("zapsano %u z %u bajtu", zapsano, velikost);
        Update.abort();
        return false;
    }
    if (!Update.end()) {
        log_e("Update.end: %s", Update.errorString());
        return false;
    }
    return true;
}
```

(Vyžaduje jen `#include "main.h"` nebo přímý include `send_text`/`NEX_TXT_PISNICKA` deklarací do `github_ota.cpp` — dnes tam `send_text` volá jen nepřímo přes `otaStatus()` v `main.cpp`, takže buď se volání `send_text(NEX_TXT_PISNICKA, ...)` přidá tam a `otaStatus()`/nová funkce se zavolá z `stahniAInstaluj()`, nebo se přidá `extern void send_text(String, String);` deklarace. Toto je detail k doladění při samotné implementaci, ne teď.)

Tato úprava je čistě volitelná (kosmetická) a jde ji přeskočit bez vlivu na to, jestli OTA vůbec funguje.

## 5. Kroky k aktivaci OTA (aktualizovaný checklist)

Toto je stávající `TODO.md`, doplněné o kritický detail zjištěný z Dilnina reálného bring-upu (dřívější `TODO-OTA.md`, smazaný po úspěšném ověření): **lokální build s `_ota` prostředím nikdy nedostane skutečné `FIRMWARE_VERSION`** — to vkládá výhradně CI z git tagu (`PLATFORMIO_BUILD_FLAGS: -DFIRMWARE_VERSION=...` v `radio-terasa-release.yml`). Takže i po nahrání `_ota` buildu lokálně přes `pio run -e seeed_xiao_esp32s3_ota -t upload` zůstane zařízení na verzi `"dev"` a `gitHubOtaCheck()` se bude furt jen přeskakovat — samo-aktualizace nikdy nenaskočí. Proto je krok 4 (bootstrap) nutně přes stažený `firmware.bin` z releasu, ne přes lokální build.

1. **Vygenerovat GitHub token** — GitHub → Settings → Developer settings → Fine-grained tokens → nový token, oprávnění *Contents: Read* pro repo `snooker52/ESP32_projekty`. Vložit do `platformio.ini` → `[extra] GITHUB_TOKEN` (nahradit `VLOZ_SEM_GITHUB_TOKEN`) a commitnout (repo je privátní, je to záměrné — stejně jako u Dilny, kde je token committěný přímo v `platformio.ini`).
2. **Pushnout na GitHub** — zkontrolovat v záložce Actions zelený běh `radio-terasa-ci.yml`.
3. **Vydat první verzi**:
   ```
   git tag radio-terasa-v1.0.0
   git push origin radio-terasa-v1.0.0
   ```
   V Actions zkontrolovat `radio-terasa-release.yml` a že vznikly dva release: `radio-terasa-v1.0.0` a `radio-terasa-latest`, oba s přílohou `firmware.bin`.
4. **Bootstrap zařízení (jednorázově, kriticky přes stažený release, ne lokální build)** — stáhnout `firmware.bin` z vydaného release (`radio-terasa-v1.0.0` nebo `radio-terasa-latest`) a nahrát ho sériově (ArduinoOTA/espota byl z projektu odstraněn, jiná cesta není). Tím zařízení poprvé poběží se skutečnou verzí `v1.0.0` a `gitHubOtaCheck()` se přestane vždy přeskakovat.
5. **Ověřit samo-aktualizaci** — pushnout triviální změnu, vydat `radio-terasa-v1.0.1`, sledovat sériový monitor (`pio device monitor`): detekce nové verze → stažení → instalace → restart → v logu nová verze. Na displeji je průběh v poli `interpret` (a po úpravě z bodu 4 i procenta v `pisnicka`).
6. **Rotace tokenu (pro budoucnost)** — pokud se token bude někdy měnit/obnovovat, další release po výměně vyžaduje jedno ruční přeflashování zařízení s novým tokenem. Zařízení se starým (odvolaným) tokenem dostane při kontrole releasu HTTP 401 a OTA tiše selže bez restartu.
7. **Smazat `TODO.md`** po dokončení všech kroků (stejně jako Dilna smazala svůj `TODO-OTA.md` po úspěšném ověření).
