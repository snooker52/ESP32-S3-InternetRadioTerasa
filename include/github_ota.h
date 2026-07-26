#pragma once
#include <Arduino.h>

// Verzi dosazuje CI z git tagu (-DFIRMWARE_VERSION="v1.2.3"); lokalni build = "dev"
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif
// Fine-grained PAT (Contents: Read) pro privatni repo, dosazuje se z platformio.ini
#ifndef GITHUB_TOKEN
#define GITHUB_TOKEN ""
#endif

// Zkontroluje release radio-terasa-latest na GitHubu; pri nove verzi stahne
// firmware.bin, nahraje pres Update a restartuje. Volat z loop().
void gitHubOtaCheck();

// implementovano v main.cpp
void otaStatus(String zprava);
void otaProgress(String zprava);
bool audioStopSong();
extern volatile bool pozadavekPripojitStream;
