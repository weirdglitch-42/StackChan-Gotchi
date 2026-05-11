/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <esp_http_server.h>

namespace gotchi {

class WebManager {
public:
    WebManager();
    ~WebManager();

    void start();
    void stop();
    bool isRunning() const { return _server != nullptr; }

    // HTML loading modes
    enum class HtmlSource {
        Internal,   // Use const char* (current)
        File        // Load from SD card file (future)
    };
    void setHtmlSource(HtmlSource source, const char* path = nullptr);

private:
    // HTTP handlers
    static esp_err_t rootHandler(httpd_req_t* req);
    static esp_err_t apiConfigHandler(httpd_req_t* req);
    static esp_err_t apiStatsHandler(httpd_req_t* req);
    static esp_err_t apiRogueHandler(httpd_req_t* req);
    static esp_err_t apiRogueNetworksHandler(httpd_req_t* req);
    static esp_err_t apiRogueSetTargetHandler(httpd_req_t* req);
    static esp_err_t apiFilesHandler(httpd_req_t* req);
    static esp_err_t apiWigleHandler(httpd_req_t* req);
    static esp_err_t apiPwnagotchiHandler(httpd_req_t* req);

    // HTML generation
    const char* generateHtml();
    const char* loadHtmlFromFile(const char* path);

    httpd_handle_t _server;
    HtmlSource _htmlSource;
    char _htmlFilePath[128];

    static WebManager* _instance;
    static WebManager* getInstance() { return _instance; }
};

// Global access
WebManager& getWebManager();

}