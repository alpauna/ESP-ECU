#pragma once

#include <Arduino.h>

bool backupFirmwareToSD(const char* path = "/firmware.bak");
bool revertFirmwareFromSD(const char* path = "/firmware.bak");
bool applyFirmwareFromSD(const char* path = "/firmware.new");
bool firmwareBackupExists(const char* path = "/firmware.bak");
size_t firmwareBackupSize(const char* path = "/firmware.bak");
