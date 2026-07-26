# Zbývá udělat — zprovoznění CI/CD a pull OTA

- [ ] **Vytvořit GitHub token** — GitHub → Settings → Developer settings → Fine-grained tokens → nový token s oprávněním *Contents: Read* pro repo `snooker52/ESP32_projekty`. Vložit do `platformio.ini` → `[extra] GITHUB_TOKEN` (místo `VLOZ_SEM_GITHUB_TOKEN`).
- [ ] **Pushnout na GitHub** — `git push` a v záložce Actions zkontrolovat, že proběhl zelený kontrolní build (workflow „radio-terasa CI").
- [ ] **Vydat první verzi** — vytvořit a pushnout tag:
  ```
  git tag radio-terasa-v1.0.0
  git push origin radio-terasa-v1.0.0
  ```
  V Actions zkontrolovat workflow „radio-terasa release" a že vznikly dva releases: `radio-terasa-v1.0.0` a `radio-terasa-latest`, oba s přílohou `firmware.bin`.
- [ ] **Bootstrap zařízení (jednorázově)** — nahrát nový firmware do rádia ručně:
  `pio run -e seeed_xiao_esp32s3_ota -t upload` (nebo sériově). Tento build má verzi `dev`, takže se sám neaktualizuje — slouží jen k prvnímu nasazení OTA klienta. Alternativně stáhnout `firmware.bin` z release a nahrát přes espota.py — pak už zařízení jede na verzi z release.
- [ ] **Ověřit samo-aktualizaci** — vydat další tag (např. `radio-terasa-v1.0.1`) a na sériovém monitoru (`pio device monitor`) sledovat: detekce nové verze → stažení → instalace → restart → v logu nová verze. Na displeji se průběh zobrazuje v poli interpret.

Po dokončení všech kroků tento soubor smazat.
