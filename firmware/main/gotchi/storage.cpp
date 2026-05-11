/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "storage.h"
#include "gotchi.h"
#include <ArduinoJson.hpp>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <esp_vfs_fat.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_spiffs.h>

static const char* TAG = "gotchi_storage";
static bool _sdAvailable = false;
static bool _storageInitialized = false;
static wl_handle_t _wlHandle = WL_INVALID_HANDLE;
static bool _fsMounted = false;
static bool _useFlash = false;

static const char* MOUNT_POINT = "/sdcard";

namespace gotchi {

static int mkdir_recursive(const char* path) {
    char tmp[256];
    char* p = nullptr;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    
    if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}

bool initStorage() {
    if (_storageInitialized) return _sdAvailable;
    
    ESP_LOGI(TAG, "Initializing storage...");
    
    // SPI bus is already initialized by LCD driver
    // Just log that we're using the shared bus
    ESP_LOGI(TAG, "Using SPI bus (shared with LCD)");
    
    // Add small delay to allow SD card to stabilize after power-on
    vTaskDelay(pdMS_TO_TICKS(50));
    
    esp_err_t ret;
    
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = 200000;

    sdspi_device_config_t slot_config = {
        .host_id = SPI3_HOST,
        .gpio_cs = GPIO_NUM_4,
        .gpio_cd = GPIO_NUM_NC,
        .gpio_wp = GPIO_NUM_NC,
        .gpio_int = GPIO_NUM_NC,
    };

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 4096
    };

    ESP_LOGI(TAG, "Mounting SD card on SPI3...");
    
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, NULL);
    
    if (ret == ESP_OK) {
        _sdAvailable = true;
        _fsMounted = true;
        _useFlash = false;
        ESP_LOGI(TAG, "SD card mounted successfully!");
    } else {
        ESP_LOGW(TAG, "SD card mount failed: %s (this may be expected if LCD uses SPI)", esp_err_to_name(ret));
        
        ESP_LOGI(TAG, "Trying internal flash FATFS partition...");
        
        ret = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, "storage", &mount_config, &_wlHandle);
        
        if (ret == ESP_OK) {
            _sdAvailable = true;
            _fsMounted = true;
            _useFlash = true;
            ESP_LOGI(TAG, "Using internal flash FATFS partition");
        } else {
            ESP_LOGE(TAG, "Flash partition mount failed: %s", esp_err_to_name(ret));
            _sdAvailable = false;
        }
    }

    _storageInitialized = true;
    
    if (_sdAvailable) {
        ESP_LOGI(TAG, "Storage initialization complete (type: %s)", _useFlash ? "flash" : "SD card");
    } else {
        ESP_LOGW(TAG, "Storage initialization failed - using NVS only");
    }
    
    return _sdAvailable;
}

void deinitStorage() {
    if (_fsMounted && _sdAvailable) {
        if (_useFlash) {
            esp_vfs_fat_spiflash_unmount_rw_wl(MOUNT_POINT, _wlHandle);
        } else {
            esp_vfs_fat_sdcard_unmount(MOUNT_POINT, NULL);
        }
        _fsMounted = false;
    }
    
    _storageInitialized = false;
    _sdAvailable = false;
}

bool hasStorage() {
    return _sdAvailable;
}

bool loadConfig(GotchiConfig& config) {
    if (!_sdAvailable) return false;
    
    char path[128];
    snprintf(path, sizeof(path), "%s/config.json", MOUNT_POINT);
    
    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGI(TAG, "Config file not found, using defaults");
        return false;
    }
    
    // Read file content
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* jsonBuffer = (char*)malloc(fileSize + 1);
    if (!jsonBuffer) {
        fclose(f);
        return false;
    }
    
    size_t bytesRead = fread(jsonBuffer, 1, fileSize, f);
    jsonBuffer[bytesRead] = '\0';
    fclose(f);
    
    // Parse JSON
    ArduinoJson::JsonDocument doc;
    ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, jsonBuffer);
    free(jsonBuffer);
    
    if (error) {
        ESP_LOGW(TAG, "Config JSON parse failed: %s", error.c_str());
        return false;
    }
    
    // Load values with defaults
    if (doc["defaultMode"].is<const char*>()) {
        strncpy(config.defaultMode, doc["defaultMode"].as<const char*>(), 15);
        config.defaultMode[15] = '\0';
    }
    if (doc["neonBrightness"].is<int>()) {
        config.neonBrightness = doc["neonBrightness"].as<int>();
    }
    if (doc["headSpeed"].is<int>()) {
        config.headSpeed = doc["headSpeed"].as<int>();
    }
    if (doc["autoRotateModes"].is<bool>()) {
        config.autoRotateModes = doc["autoRotateModes"].as<bool>();
    }
    if (doc["audioEnabled"].is<bool>()) {
        config.audioEnabled = doc["audioEnabled"].as<bool>();
    }
    if (doc["wigleApiKey"].is<const char*>()) {
        strncpy(config.wigleApiKey, doc["wigleApiKey"].as<const char*>(), 127);
        config.wigleApiKey[127] = '\0';
    }
    if (doc["wigleUsername"].is<const char*>()) {
        strncpy(config.wigleUsername, doc["wigleUsername"].as<const char*>(), 63);
        config.wigleUsername[63] = '\0';
    }
    if (doc["wpasecKey"].is<const char*>()) {
        strncpy(config.wpasecKey, doc["wpasecKey"].as<const char*>(), 127);
        config.wpasecKey[127] = '\0';
    }
    if (doc["autoWigleUpload"].is<bool>()) {
        config.autoWigleUpload = doc["autoWigleUpload"].as<bool>();
    }
    if (doc["autoWpasecUpload"].is<bool>()) {
        config.autoWpasecUpload = doc["autoWpasecUpload"].as<bool>();
    }
    if (doc["logRotationDays"].is<int>()) {
        config.logRotationDays = doc["logRotationDays"].as<int>();
    }
    if (doc["maxNetworks"].is<int>()) {
        config.maxNetworks = doc["maxNetworks"].as<int>();
    }
    if (doc["huntDisclaimerShown"].is<bool>()) {
        config.huntDisclaimerShown = doc["huntDisclaimerShown"].as<bool>();
    }
    
    ESP_LOGI(TAG, "Config loaded from JSON");
    return true;
}

bool saveConfig(const GotchiConfig& config) {
    if (!_sdAvailable) return false;
    
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s", MOUNT_POINT);
    mkdir_recursive(dir_path);
    
    char path[128];
    snprintf(path, sizeof(path), "%s/config.json", MOUNT_POINT);
    
    ArduinoJson::JsonDocument doc;
    doc["defaultMode"] = config.defaultMode;
    doc["neonBrightness"] = config.neonBrightness;
    doc["headSpeed"] = config.headSpeed;
    doc["autoRotateModes"] = config.autoRotateModes;
    doc["audioEnabled"] = config.audioEnabled;
    doc["wigleApiKey"] = config.wigleApiKey;
    doc["wigleUsername"] = config.wigleUsername;
    doc["wpasecKey"] = config.wpasecKey;
    doc["autoWigleUpload"] = config.autoWigleUpload;
    doc["autoWpasecUpload"] = config.autoWpasecUpload;
    doc["logRotationDays"] = config.logRotationDays;
    doc["maxNetworks"] = config.maxNetworks;
    doc["huntDisclaimerShown"] = config.huntDisclaimerShown;
    
    // Serialize to string first, then write to file
    std::string jsonStr;
    ArduinoJson::serializeJson(doc, jsonStr);
    
    FILE* f = fopen(path, "w");
    if (!f) return false;
    
    fprintf(f, "%s", jsonStr.c_str());
    fclose(f);
    
    ESP_LOGI(TAG, "Config saved to JSON");
    return true;
}

bool saveNetworks(const std::vector<NetworkInfo>& networks) {
    if (!_sdAvailable) return false;
    
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s/networks", MOUNT_POINT);
    mkdir_recursive(dir_path);
    
    char path[128];
    snprintf(path, sizeof(path), "%s/networks.json", MOUNT_POINT);
    
    FILE* f = fopen(path, "w");
    if (!f) return false;
    
    fprintf(f, "[\n");
    for (size_t i = 0; i < networks.size(); i++) {
        fprintf(f, "  {\"ssid\": \"%s\", \"bssid\": \"%02x:%02x:%02x:%02x:%02x:%02x\", \"rssi\": %d, \"channel\": %d}",
            networks[i].ssid,
            networks[i].bssid[0], networks[i].bssid[1], networks[i].bssid[2],
            networks[i].bssid[3], networks[i].bssid[4], networks[i].bssid[5],
            (int)networks[i].rssi, (int)networks[i].channel);
        if (i < networks.size() - 1) fprintf(f, ",");
        fprintf(f, "\n");
    }
    fprintf(f, "]\n");
    
    fclose(f);
    return true;
}

int loadNetworks(std::vector<NetworkInfo>& networks) {
    if (!_sdAvailable) return 0;
    
    char path[128];
    snprintf(path, sizeof(path), "%s/networks.json", MOUNT_POINT);
    
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    
    networks.clear();
    NetworkInfo net;
    int matched = fscanf(f, "{\"ssid\": \"%[^\"]\", \"bssid\": \"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\", \"rssi\": %hhd, \"channel\": %hhu}",
        net.ssid, &net.bssid[0], &net.bssid[1], &net.bssid[2], &net.bssid[3], &net.bssid[4], &net.bssid[5],
        &net.rssi, &net.channel);
    
    while (matched == 9) {
        net.isHidden = false;
        net.hasCapture = false;
        net.lastSeen = 0;
        networks.push_back(net);
        matched = fscanf(f, "{\"ssid\": \"%[^\"]\", \"bssid\": \"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\", \"rssi\": %hhd, \"channel\": %hhu}",
            net.ssid, &net.bssid[0], &net.bssid[1], &net.bssid[2], &net.bssid[3], &net.bssid[4], &net.bssid[5],
            &net.rssi, &net.channel);
    }
    
    fclose(f);
    return networks.size();
}

bool saveNetworkToCSV(const NetworkInfo& net, const GPSData& gps) {
    if (!_sdAvailable) return false;
    
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s/wardriving", MOUNT_POINT);
    mkdir_recursive(dir_path);
    
    char path[128];
    snprintf(path, sizeof(path), "%s/wardrive.csv", MOUNT_POINT);
    
    FILE* f = fopen(path, "a");
    if (!f) return false;
    
    fprintf(f, "%s,%02x%02x%02x%02x%02x%02x,%d,%d,%.6f,%.6f,%lu\n",
        net.ssid,
        net.bssid[0], net.bssid[1], net.bssid[2], net.bssid[3], net.bssid[4], net.bssid[5],
        (int)net.rssi, (int)net.channel,
        gps.latitude, gps.longitude, (unsigned long)gps.timestamp);
    
    fclose(f);
    return true;
}

bool saveHandshake(const uint8_t* data, size_t len, const char* ssid, const uint8_t* bssid) {
    if (!_sdAvailable) return false;
    
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s/handshakes", MOUNT_POINT);
    mkdir_recursive(dir_path);
    
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/handshakes/%s_%02x%02x%02x%02x%02x%02x.hccapx",
        MOUNT_POINT, ssid, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    
    FILE* f = fopen(filename, "wb");
    if (!f) return false;
    
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

int getStoredHandshakeCount() {
    if (!_sdAvailable) return 0;
    
    char path[128];
    snprintf(path, sizeof(path), "%s/handshakes", MOUNT_POINT);
    
    int count = 0;
    DIR* dir = opendir(path);
    if (!dir) return 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".hccapx")) count++;
    }
    closedir(dir);
    
    return count;
}

// Returns list of handshake files (caller must free each string)
// IMPORTANT: Call freeHandshakeFiles() to release memory after use
std::vector<char*> getHandshakeFiles() {
    std::vector<char*> files;
    if (!_sdAvailable) return files;
    
    char path[128];
    snprintf(path, sizeof(path), "%s/handshakes", MOUNT_POINT);
    
    DIR* dir = opendir(path);
    if (!dir) return files;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".hccapx")) {
            char* name = strdup(entry->d_name);
            files.push_back(name);
        }
    }
    closedir(dir);
    
    return files;
}

// Free memory allocated by getHandshakeFiles()
void freeHandshakeFiles(std::vector<char*>& files) {
    for (auto& f : files) {
        free(f);
    }
    files.clear();
}

bool deleteHandshake(const char* filename) {
    if (!_sdAvailable) return false;
    
    char path[128];
    snprintf(path, sizeof(path), "%s/handshakes/%s", MOUNT_POINT, filename);
    
    return f_unlink(path) == 0;
}

bool exportWardriveCSV(const std::vector<NetworkInfo>& networks, const char* filename) {
    if (!_sdAvailable) return false;
    
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s/wardriving", MOUNT_POINT);
    mkdir_recursive(dir_path);
    
    char path[128];
    snprintf(path, sizeof(path), "%s/wardriving/%s", MOUNT_POINT, filename);
    
    FILE* f = fopen(path, "w");
    if (!f) return false;
    
    fprintf(f, "SSID,BSSID,RSSI,Channel,Latitude,Longitude,Timestamp\n");
    
    for (const auto& net : networks) {
        fprintf(f, "%s,%02x:%02x:%02x:%02x:%02x:%02x,%d,%d,0,0,0\n",
            net.ssid,
            net.bssid[0], net.bssid[1], net.bssid[2], net.bssid[3], net.bssid[4], net.bssid[5],
            (int)net.rssi, (int)net.channel);
    }
    
    fclose(f);
    return true;
}

bool appendToWardriveCSV(const NetworkInfo& net, const GPSData& gps) {
    return saveNetworkToCSV(net, gps);
}

void logToFile(const char* message) {
    if (!_sdAvailable) return;
    
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s/logs", MOUNT_POINT);
    mkdir_recursive(dir_path);
    
    char path[128];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char filename[32];
    strftime(filename, sizeof(filename), "gotchi_%Y%m%d.log", tm_info);
    
    snprintf(path, sizeof(path), "%s/logs/%s", MOUNT_POINT, filename);
    
    FILE* f = fopen(path, "a");
    if (!f) return;
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(f, "[%s] %s\n", timestamp, message);
    
    fclose(f);
}

void rotateLogs() {
    if (!_sdAvailable) return;
    
    char logs_dir[128];
    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", MOUNT_POINT);
    
    DIR* dir = opendir(logs_dir);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "gotchi_") && strstr(entry->d_name, ".log")) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", logs_dir, entry->d_name);
            
            struct stat st;
            if (stat(path, &st) == 0) {
                time_t age = time(NULL) - st.st_mtime;
                if (age > (7 * 24 * 60 * 60)) {
                    f_unlink(path);
                }
            }
        }
    }
    closedir(dir);
}

// Stub: WiFi handshake upload to pwnagotchi-compatible server
// TODO: Implement HTTP upload to pwnagotchi web interface
bool uploadToWpasec(const char* handshakeFile, const char* apiKey, char* result, size_t resultLen) {
    if (!_sdAvailable) {
        if (result) snprintf(result, resultLen, "Storage not available");
        return false;
    }
    
    if (result) snprintf(result, resultLen, "Upload not implemented");
    return false;
}

// Stub: Returns count of cracked passwords
// TODO: Implement hash cracking status tracking
int getCrackedCount() {
    return 0;
}

// Stub: Returns list of cracked passwords
// TODO: Implement hash cracking result storage
std::vector<char*> getCrackedPasswords() {
    std::vector<char*> passwords;
    return passwords;
}

// Stub: WiFi wardrive upload to WiGLE.net
// TODO: Implement WiGLE API upload
bool uploadToWigle(const char* csvFile, const char* apiKey, const char* username, char* result, size_t resultLen) {
    if (!_sdAvailable) {
        if (result) snprintf(result, resultLen, "Storage not available");
        return false;
    }
    
    if (result) snprintf(result, resultLen, "Upload not implemented");
    return false;
}

bool ensureDirectory(const char* path) {
    if (!_sdAvailable) return false;
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", MOUNT_POINT, path);
    
    return mkdir_recursive(full_path) == 0;
}

bool fileExists(const char* path) {
    if (!_sdAvailable) return false;
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", MOUNT_POINT, path);
    
    struct stat st;
    return stat(full_path, &st) == 0;
}

int64_t getFileSize(const char* path) {
    if (!_sdAvailable) return -1;
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", MOUNT_POINT, path);
    
    struct stat st;
    if (stat(full_path, &st) != 0) return -1;
    
    return st.st_size;
}

int64_t getStorageFreeSpace() {
    if (!_sdAvailable) return 0;
    
    FATFS* fs = nullptr;
    DWORD freeClusters;
    FRESULT res = f_getfree("0:", &freeClusters, &fs);
    
    if (res != FR_OK) return 0;
    
    return (int64_t)freeClusters * fs->csize * 512;
}

}