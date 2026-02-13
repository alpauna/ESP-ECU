#include "OtaUtils.h"
#include <SD.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "Logger.h"

static constexpr size_t OTA_BUF_SIZE = 4096;
static constexpr size_t MIN_FIRMWARE_SIZE = 100 * 1024;

bool backupFirmwareToSD(const char* path) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) { Log.error("OTA", "Could not get running partition"); return false; }
    size_t sketchSize = ESP.getSketchSize();
    if (sketchSize < MIN_FIRMWARE_SIZE) { Log.error("OTA", "Sketch size too small: %u", sketchSize); return false; }
    File backup = SD.open(path, FILE_WRITE);
    if (!backup) { Log.error("OTA", "Failed to open %s for writing", path); return false; }
    Log.info("OTA", "Backing up firmware (%u bytes) to %s", sketchSize, path);
    uint8_t buf[OTA_BUF_SIZE];
    for (size_t offset = 0; offset < sketchSize; offset += OTA_BUF_SIZE) {
        size_t toRead = sketchSize - offset;
        if (toRead > OTA_BUF_SIZE) toRead = OTA_BUF_SIZE;
        esp_err_t err = esp_partition_read(running, offset, buf, toRead);
        if (err != ESP_OK) { Log.error("OTA", "Flash read failed"); backup.close(); SD.remove(path); return false; }
        if (backup.write(buf, toRead) != toRead) { Log.error("OTA", "SD write failed"); backup.close(); SD.remove(path); return false; }
    }
    backup.close();
    Log.info("OTA", "Firmware backup complete (%u bytes)", sketchSize);
    return true;
}

bool revertFirmwareFromSD(const char* path) {
    File backup = SD.open(path, FILE_READ);
    if (!backup) { Log.error("OTA", "Failed to open %s", path); return false; }
    size_t fileSize = backup.size();
    if (fileSize < MIN_FIRMWARE_SIZE) { Log.error("OTA", "Backup too small: %u", fileSize); backup.close(); return false; }
    Log.info("OTA", "Reverting firmware from %s (%u bytes)", path, fileSize);
    if (!Update.begin(fileSize)) { Log.error("OTA", "Update.begin failed"); backup.close(); return false; }
    uint8_t buf[OTA_BUF_SIZE];
    size_t remaining = fileSize;
    while (remaining > 0) {
        size_t toRead = remaining > OTA_BUF_SIZE ? OTA_BUF_SIZE : remaining;
        size_t bytesRead = backup.read(buf, toRead);
        if (bytesRead == 0) { Update.abort(); backup.close(); return false; }
        if (Update.write(buf, bytesRead) != bytesRead) { Update.abort(); backup.close(); return false; }
        remaining -= bytesRead;
    }
    backup.close();
    if (Update.end(true)) { Log.info("OTA", "Firmware revert successful"); return true; }
    Log.error("OTA", "Firmware revert failed");
    return false;
}

bool applyFirmwareFromSD(const char* path) {
    backupFirmwareToSD();
    bool ok = revertFirmwareFromSD(path);
    if (ok) { SD.remove(path); Log.info("OTA", "Removed %s after successful apply", path); }
    return ok;
}

bool firmwareBackupExists(const char* path) { return SD.exists(path); }

size_t firmwareBackupSize(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}
