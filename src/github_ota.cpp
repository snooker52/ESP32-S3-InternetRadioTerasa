#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "github_ota.h"

// Pohyblivy release, ktery CI aktualizuje pri kazdem vydani (tag radio-terasa-v*)
static const char* GITHUB_API_RELEASE =
    "https://api.github.com/repos/snooker52/ESP32_projekty/releases/tags/radio-terasa-latest";
static const char* GITHUB_API_ASSET =
    "https://api.github.com/repos/snooker52/ESP32_projekty/releases/assets/";
static const char* FIRMWARE_ASSET = "firmware.bin";
static const char* USER_AGENT = "radio-terasa";

static void nastavHlavicky(HTTPClient& http, bool sTokenem) {
    if (sTokenem) http.addHeader("Authorization", "Bearer " GITHUB_TOKEN);
    http.addHeader("User-Agent", USER_AGENT);
}

// Stazeni assetu z privatniho repa: API vrati 302 na podepsanou URL
// (objects.githubusercontent.com), na kterou se Authorization posilat nesmi.
static bool stahniAInstaluj(uint32_t assetId, size_t velikost) {
    String podepsanaUrl;
    {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        if (!http.begin(client, String(GITHUB_API_ASSET) + assetId)) return false;
        nastavHlavicky(http, true);
        http.addHeader("Accept", "application/octet-stream");
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        const char* sledovaneHlavicky[] = {"Location"};
        http.collectHeaders(sledovaneHlavicky, 1);
        int kod = http.GET();
        if (kod == HTTP_CODE_FOUND || kod == HTTP_CODE_MOVED_PERMANENTLY
            || kod == HTTP_CODE_TEMPORARY_REDIRECT) {
            podepsanaUrl = http.header("Location");
        }
        http.end();
        if (podepsanaUrl.isEmpty()) {
            log_e("stazeni assetu: neprisel redirect (HTTP %d)", kod);
            return false;
        }
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, podepsanaUrl)) return false;
    nastavHlavicky(http, false);   // podepsana URL - bez tokenu!
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

    otaProgress("");   // vycistit pole pred zobrazenim procent
    Update.onProgress([](size_t hotovo, size_t celkem) {
        static uint8_t posledniDesitka = 255;
        uint8_t procent = celkem ? (hotovo * 100) / celkem : 0;
        if (procent / 10 != posledniDesitka) {
            posledniDesitka = procent / 10;
            otaProgress(String(procent) + " %");
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

void gitHubOtaCheck() {
    if (strcmp(FIRMWARE_VERSION, "dev") == 0) {
        log_i("vyvojova verze - automaticka aktualizace se preskakuje");
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.useHTTP10(true);   // deserializeJson neumi chunked encoding
    if (!http.begin(client, GITHUB_API_RELEASE)) {
        log_e("http.begin selhalo");
        return;
    }
    nastavHlavicky(http, true);
    http.addHeader("Accept", "application/vnd.github+json");
    int kod = http.GET();
    if (kod != HTTP_CODE_OK) {
        log_e("GitHub API: HTTP %d", kod);
        http.end();
        return;
    }

    JsonDocument filtr;
    filtr["name"] = true;
    filtr["assets"][0]["name"] = true;
    filtr["assets"][0]["id"] = true;
    filtr["assets"][0]["size"] = true;
    JsonDocument doc;
    DeserializationError chyba =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filtr));
    http.end();
    if (chyba) {
        log_e("JSON: %s", chyba.c_str());
        return;
    }

    String verze = doc["name"] | "";
    if (verze.isEmpty()) {
        log_e("release nema jmeno (verzi)");
        return;
    }
    if (verze == FIRMWARE_VERSION) {
        log_i("firmware %s je aktualni", FIRMWARE_VERSION);
        return;
    }

    uint32_t assetId = 0;
    size_t velikost = 0;
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        if (String(asset["name"] | "") == FIRMWARE_ASSET) {
            assetId = asset["id"] | 0;
            velikost = asset["size"] | 0;
            break;
        }
    }
    if (!assetId || !velikost) {
        log_e("release %s neobsahuje %s", verze.c_str(), FIRMWARE_ASSET);
        return;
    }

    log_i("nova verze %s (aktualni %s), zacinam aktualizaci", verze.c_str(), FIRMWARE_VERSION);
    audioStopSong();
    otaStatus(String("Aktualizuji firmware na ") + verze);

    if (stahniAInstaluj(assetId, velikost)) {
        otaStatus("Aktualizace hotová, restartuji.");
        log_i("aktualizace ok, restartuji");
        delay(500);
        ESP.restart();
    } else {
        otaStatus("Aktualizace se nezdařila.");
        pozadavekPripojitStream = true;   // vratit prehravani
    }
}
