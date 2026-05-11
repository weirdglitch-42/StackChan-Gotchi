/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <vector>
#include <cstring>
#include <gotchi/gps.h>
#include <gotchi/gotchi.h>

namespace gotchi {

static const size_t MAX_STORED_NETWORKS = 5000;
static const int MAX_LOG_FILES = 5;

struct GotchiConfig {
    char defaultMode[16];
    int neonBrightness;
    int headSpeed;
    bool autoRotateModes;
    bool audioEnabled;
    char wigleApiKey[128];
    char wigleUsername[64];
    char wpasecKey[128];
    bool autoWigleUpload;
    bool autoWpasecUpload;
    int logRotationDays;
    int maxNetworks;
    bool huntDisclaimerShown;
    
    GotchiConfig() {
        strcpy(defaultMode, "SCOUT");
        neonBrightness = 255;
        headSpeed = 1;
        autoRotateModes = false;
        audioEnabled = true;
        wigleApiKey[0] = '\0';
        wigleUsername[0] = '\0';
        wpasecKey[0] = '\0';
        autoWigleUpload = false;
        autoWpasecUpload = false;
        logRotationDays = 7;
        maxNetworks = MAX_STORED_NETWORKS;
        huntDisclaimerShown = false;
    }
};

bool initStorage();
void deinitStorage();
bool hasStorage();

bool loadConfig(GotchiConfig& config);
bool saveConfig(const GotchiConfig& config);

bool saveNetworks(const std::vector<NetworkInfo>& networks);
int loadNetworks(std::vector<NetworkInfo>& networks);
bool saveNetworkToCSV(const NetworkInfo& net, const GPSData& gps);

bool saveHandshake(const uint8_t* data, size_t len, const char* ssid, const uint8_t* bssid);
int getStoredHandshakeCount();
std::vector<char*> getHandshakeFiles();
void freeHandshakeFiles(std::vector<char*>& files);
bool deleteHandshake(const char* filename);

bool exportWardriveCSV(const std::vector<NetworkInfo>& networks, const char* filename);
bool appendToWardriveCSV(const NetworkInfo& net, const GPSData& gps);

void logToFile(const char* message);
void rotateLogs();

bool uploadToWpasec(const char* handshakeFile, const char* apiKey, char* result, size_t resultLen);
int getCrackedCount();
std::vector<char*> getCrackedPasswords();

bool uploadToWigle(const char* csvFile, const char* apiKey, const char* username, char* result, size_t resultLen);

bool ensureDirectory(const char* path);
bool fileExists(const char* path);
int64_t getFileSize(const char* path);
int64_t getStorageFreeSpace();

}