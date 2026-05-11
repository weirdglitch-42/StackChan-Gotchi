/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "web_manager.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include <string.h>
#include <stdio.h>
#include "gotchi.h"
#include "storage.h"
#include "rogue_manager.h"

static const char* TAG = "gotchi_web";

namespace gotchi {

WebManager* WebManager::_instance = nullptr;

WebManager::WebManager() : _server(nullptr), _htmlSource(HtmlSource::Internal) {
    _htmlFilePath[0] = '\0';
    _instance = this;
}

WebManager::~WebManager() {
    stop();
    _instance = nullptr;
}

void WebManager::start() {
    if (_server != nullptr) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 4096;
    
    httpd_uri_t rootUri = {"/", HTTP_GET, rootHandler, nullptr};
    httpd_uri_t apiConfigUri = {"/api/config", HTTP_GET, apiConfigHandler, nullptr};
    httpd_uri_t apiConfigPostUri = {"/api/config", HTTP_POST, apiConfigHandler, nullptr};
    httpd_uri_t apiStatsUri = {"/api/stats", HTTP_GET, apiStatsHandler, nullptr};
    httpd_uri_t apiRogueUri = {"/api/rogue", HTTP_POST, apiRogueHandler, nullptr};
    httpd_uri_t apiRogueNetworksUri = {"/api/rogue/networks", HTTP_GET, apiRogueNetworksHandler, nullptr};
    httpd_uri_t apiRogueSetUri = {"/api/rogue/set", HTTP_POST, apiRogueSetTargetHandler, nullptr};
    httpd_uri_t apiFilesUri = {"/api/files", HTTP_GET, apiFilesHandler, nullptr};
    httpd_uri_t apiWigleUri = {"/api/wigle", HTTP_POST, apiWigleHandler, nullptr};
    httpd_uri_t apiPwnUri = {"/api/pwnagotchi", HTTP_POST, apiPwnagotchiHandler, nullptr};

    if (httpd_start(&_server, &config) == ESP_OK) {
        httpd_register_uri_handler(_server, &rootUri);
        httpd_register_uri_handler(_server, &apiConfigUri);
        httpd_register_uri_handler(_server, &apiConfigPostUri);
        httpd_register_uri_handler(_server, &apiStatsUri);
        httpd_register_uri_handler(_server, &apiRogueUri);
        httpd_register_uri_handler(_server, &apiRogueNetworksUri);
        httpd_register_uri_handler(_server, &apiRogueSetUri);
        httpd_register_uri_handler(_server, &apiFilesUri);
        httpd_register_uri_handler(_server, &apiWigleUri);
        httpd_register_uri_handler(_server, &apiPwnUri);
        ESP_LOGI(TAG, "HTTP server started");
    } else {
        ESP_LOGW(TAG, "HTTP server failed to start");
    }
}

void WebManager::stop() {
    if (_server != nullptr) {
        httpd_stop(_server);
        _server = nullptr;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

void WebManager::setHtmlSource(HtmlSource source, const char* path) {
    _htmlSource = source;
    if (path != nullptr) {
        strncpy(_htmlFilePath, path, sizeof(_htmlFilePath) - 1);
    }
}

const char* WebManager::generateHtml() {
    if (_htmlSource == HtmlSource::File && _htmlFilePath[0] != '\0') {
        return loadHtmlFromFile(_htmlFilePath);
    }
    
    static const char html[] = R"HTML(<!DOCTYPE html><html>
<head><title>StackChan-Gotchi</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial;margin:0;background:#1a1a2e;color:#eee;font-size:14px}
h1{color:#00ff88;text-align:center;margin:10px 0;font-size:20px}
.nav{display:flex;background:#0f3460;padding:5px}
.nav button{flex:1;padding:10px;background:#16213e;color:#888;border:none;font-weight:bold}
.nav button.active{background:#00ff88;color:#000}
.page{display:none}.page.active{display:block}
.card{background:#16213e;padding:12px;margin:10px;border-radius:8px}
select{padding:10px;margin:5px 0;width:100%;background:#0f3460;color:#fff;border:1px solid #00ff88;border-radius:4px}
button{background:#00ff88;color:#000;padding:10px;border:none;border-radius:5px;cursor:pointer;width:100%;font-weight:bold;margin:5px 0}
button:hover{background:#00cc6a}
button.danger{background:#ff6b6b;color:#fff}
button.secondary{background:#4a4a8a;color:#fff}
p{color:#aaa;margin:5px 0}.value{color:#00ff88;font-weight:bold}
table{width:100%;border-collapse:collapse;margin:10px 0}
td,th{padding:8px;text-align:left;border-bottom:1px solid #333}
th{color:#00ff88}
#message{text-align:center;padding:10px;border-radius:4px;display:none;margin:10px}
.success{background:#00ff8833;color:#00ff88}
.error{background:#ff000033;color:#ff4444}
</style></head>
<body>
<h1>StackChan-Gotchi</h1>
<div class="nav">
  <button id="btnConfig" class="active" onclick="showPage('config')">Config</button>
  <button id="btnStats" onclick="showPage('stats')">Stats</button>
  <button id="btnNetworks" onclick="showPage('networks')">Networks</button>
  <button id="btnFiles" onclick="showPage('files')">Files</button>
</div>
<div id="message"></div>

<div id="pageConfig" class="page active">
  <div class='card'>
    <p>Current: <span class='value' id='currentMode'>Loading...</span></p>
    <p>Level: <span class='value' id='level'>-</span> | XP: <span class='value' id='xp'>-</span> | Nets: <span class='value' id='networks'>-</span></p>
  </div>
  <div class='card'>
    <p><strong>Mode Selection</strong></p>
    <select id='mode'>
      <option value='IDLE'>IDLE - Resting</option>
      <option value='SCOUT'>SCOUT - WiFi Scan</option>
      <option value='HUNT'>HUNT - Promiscuous</option>
      <option value='WARDIVE'>WARDIVE - Wardriving</option>
      <option value='SPECTRUM'>SPECTRUM - Spectrum</option>
      <option value='BLE_SCAN'>BLE_SCAN - Bluetooth</option>
      <option value='ROGUE'>ROGUE - Beacon Spam</option>
      <option value='STATS'>STATS - Statistics</option>
    </select>
    <button onclick='saveConfig()'>Apply Mode</button>
  </div>
  <div class='card'>
    <p><strong>Quick Actions</strong></p>
    <button class='danger' onclick='stopRogue()'>Stop Rogue Mode</button>
    <button class='secondary' onclick='restartAP()'>Restart AP</button>
  </div>
  <div class='card'>
    <p><strong>Rogue Target Network</strong></p>
    <select id='rogueNetwork'><option value=''>-- Select Network --</option></select>
    <button onclick='loadRogueNetworks()'>Load Networks</button>
    <button onclick='setRogueTarget()'>Set Target</button>
    <p><small>Select from discovered networks to target in ROGUE mode</small></p>
  </div>
</div>

<div id='pageStats' class='page'>
  <div class='card'>
    <p><strong>Player Stats</strong></p>
    <table>
      <tr><td>Level</td><td class='value' id='statsLevel'>-</td></tr>
      <tr><td>XP</td><td class='value' id='statsXP'>-</td></tr>
      <tr><td>Networks</td><td class='value' id='statsNetworks'>-</td></tr>
      <tr><td>Handshakes</td><td class='value' id='statsHS'>-</td></tr>
      <tr><td>Prestige</td><td class='value' id='statsPrestige'>-</td></tr>
      <tr><td>Uptime</td><td class='value' id='statsUptime'>-</td></tr>
      <tr><td>Achievements</td><td class='value' id='statsAch'>-</td></tr>
    </table>
  </div>
  <div class='card'>
    <p><strong>Session Stats</strong></p>
    <p>Session XP: <span class='value' id='sessionXP'>-</span></p>
    <p>Session Time: <span class='value' id='sessionTime'>-</span></p>
  </div>
</div>

<div id='pageNetworks' class='page'>
  <div class='card'>
    <p><strong>Discovered Networks</strong></p>
    <div id='networkList'>Loading...</div>
  </div>
</div>

<div id='pageFiles' class='page'>
  <div class='card'>
    <p><strong>Storage</strong></p>
    <p>Free: <span class='value' id='storageFree'>-</span></p>
  </div>
</div>

<script>
var currentPage='config';
function showPage(p){
  currentPage=p;
  document.querySelectorAll('.page').forEach(function(x){x.classList.remove('active')});
  document.querySelectorAll('.nav button').forEach(function(x){x.classList.remove('active')});
  document.getElementById('page'+p.charAt(0).toUpperCase()+p.slice(1)).classList.add('active');
  document.getElementById('btn'+p.charAt(0).toUpperCase()+p.slice(1)).classList.add('active');
}
function showMessage(msg,isError){
  var m=document.getElementById('message');m.textContent=msg;m.className=isError?'error':'success';m.style.display='block';
  setTimeout(function(){m.style.display='none'},3000);
}
function saveConfig(){
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({mode:document.getElementById('mode').value})})
  .then(function(r){return r.json()}).then(function(d){showMessage('Mode: '+d.mode,false);updateStats();})
  .catch(function(e){showMessage('Error: '+e,true)});
}
function stopRogue(){fetch('/api/rogue',{method:'POST'}).then(function(){showMessage('Rogue stopped',false)}).catch(function(){});}
function loadRogueNetworks(){fetch('/api/rogue/networks').then(function(r){return r.json()}).then(function(d){
  var sel=document.getElementById('rogueNetwork');sel.innerHTML='<option value="">-- Select Network --</option>';
  d.networks.forEach(function(n){var opt=document.createElement('option');
    opt.value=n.ssid+'|'+n.bssid+'|'+n.channel;opt.textContent=n.ssid+' (CH'+n.channel+', '+n.rssi+'dBm)';
    sel.appendChild(opt);});
  showMessage('Loaded '+d.networks.length+' networks',false);
}).catch(function(e){showMessage('Failed to load networks',true);});}
function setRogueTarget(){var sel=document.getElementById('rogueNetwork');var v=sel.value;
  if(!v){showMessage('Select a network first',true);return;}
  var parts=v.split('|');var params='ssid='+encodeURIComponent(parts[0])+'&bssid='+parts[1]+'&channel='+parts[2];
  fetch('/api/rogue/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params})
  .then(function(r){return r.json()}).then(function(d){showMessage('Target: '+d.ssid+' CH'+d.channel,false);}).catch(function(e){showMessage('Failed to set target',true);});}
function restartAP(){location.reload();}
function updateStats(){
  fetch('/api/stats').then(function(r){return r.json()}).then(function(d){
    document.getElementById('currentMode').textContent=d.mode;
    document.getElementById('level').textContent=d.level;
    document.getElementById('xp').textContent=d.xp;
    document.getElementById('networks').textContent=d.networks;
    var ms=document.getElementById('mode').options;
    for(var i=0;i<ms.length;i++){if(ms[i].value===d.mode){ms[i].selected=true;break;}}
    document.getElementById('statsLevel').textContent=d.level;
    document.getElementById('statsXP').textContent=d.xp;
    document.getElementById('statsNetworks').textContent=d.networks;
    document.getElementById('statsHS').textContent=d.handshakes;
    document.getElementById('statsPrestige').textContent=d.prestige;
    document.getElementById('statsUptime').textContent=d.uptime;
    document.getElementById('statsAch').textContent=d.achievements+'/17';
    document.getElementById('sessionXP').textContent='+'+d.sessionXP;
    document.getElementById('sessionTime').textContent=d.sessionTime;
    var nl=document.getElementById('networkList');
    if(d.networksList && d.networksList.length>0){
      nl.innerHTML='<table><tr><th>SSID</th><th>Ch</th><th>dBm</th></tr>'+
      d.networksList.map(function(n){return '<tr><td>'+n.ssid+'</td><td>'+n.ch+'</td><td>'+n.rssi+'</td></tr>'}).join('')+'</table>';
    }else{nl.innerHTML='<p>No networks</p>';}
  }).catch(function(){});
  fetch('/api/files').then(function(r){return r.json()}).then(function(f){
    var freeKB=Math.floor(f.freeSpace/1024);
    document.getElementById('storageFree').textContent=freeKB+' KB';
  }).catch(function(){});
}
setInterval(updateStats,3000);
updateStats();
</script></body></html>)HTML";
    return html;
}

const char* WebManager::loadHtmlFromFile(const char* path) {
    // Future: Load HTML from SD card file
    ESP_LOGW(TAG, "HTML file loading not implemented, using internal");
    return nullptr;
}

esp_err_t WebManager::rootHandler(httpd_req_t* req) {
    WebManager* wm = getInstance();
    if (!wm) return ESP_FAIL;
    
    const char* html = wm->generateHtml();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

esp_err_t WebManager::apiConfigHandler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        Stats s = getStats();
        char json[256];
        snprintf(json, sizeof(json), 
            "{\"mode\":\"%s\",\"level\":%d,\"xp\":%d,\"apActive\":%s}",
            getModeName(getCurrentMode()),
            (int)s.level,
            (int)s.xp,
            isConfigMode() ? "true" : "false");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    // Parse mode from JSON and set it
    char modeStr[32] = {0};
    if (sscanf(content, "%*[^:]%*c%31s", modeStr) == 1) {
        // Remove trailing }
        char* p = strchr(modeStr, '}');
        if (p) *p = '\0';
        
        // Remove quotes
        if (modeStr[0] == '"') memmove(modeStr, modeStr+1, strlen(modeStr));
        int len = strlen(modeStr);
        if (len > 0 && modeStr[len-1] == '"') modeStr[len-1] = '\0';
        
        Mode m = Mode::IDLE;
        if (strcmp(modeStr, "SCOUT") == 0) m = Mode::SCOUT;
        else if (strcmp(modeStr, "HUNT") == 0) m = Mode::HUNT;
        else if (strcmp(modeStr, "WARDIVE") == 0) m = Mode::WARDIVE;
        else if (strcmp(modeStr, "SPECTRUM") == 0) m = Mode::SPECTRUM;
        else if (strcmp(modeStr, "BLE_SCAN") == 0) m = Mode::BLE_SCAN;
        else if (strcmp(modeStr, "ROGUE") == 0) m = Mode::ROGUE;
        else if (strcmp(modeStr, "STATS") == 0) m = Mode::STATS;
        else if (strcmp(modeStr, "CONFIG") == 0) m = Mode::CONFIG;
        
        setMode(m);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"mode\":\"CONFIG\"}", 30);
    return ESP_OK;
}

esp_err_t WebManager::apiStatsHandler(httpd_req_t* req) {
    Stats s = getStats();
    auto networks = getNetworks();
    
    char json[1024];
    int pos = snprintf(json, sizeof(json),
        "{\"mode\":\"%s\",\"level\":%d,\"xp\":%d,\"networks\":%u,\"handshakes\":%u,"
        "\"prestige\":%u,\"achievements\":%u,\"uptime\":\"%us\",\"sessionXP\":%u,\"sessionTime\":\"%us\",\"networksList\":[",
        getModeName(getCurrentMode()),
        (int)s.level, (int)s.xp,
        (unsigned)s.networksFound, (unsigned)s.handshakesCaptured,
        (unsigned)s.prestige, (unsigned)s.achievementCount,
        (unsigned)s.uptimeSeconds,
        (unsigned)(s.xp - s.sessionXPGain),
        (unsigned)s.sessionTimeSeconds);
    
    for (size_t i = 0; i < networks.size() && i < 10; i++) {
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"ssid\":\"%.32s\",\"ch\":%d,\"rssi\":%d}%s",
            networks[i].ssid, networks[i].channel, (int)networks[i].rssi,
            (i < networks.size() - 1 && i < 9) ? "," : "");
    }
    
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebManager::apiRogueHandler(httpd_req_t* req) {
    if (req->method == HTTP_POST) {
        stopRogue();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"stopped\"}", 18);
    }
    return ESP_OK;
}

esp_err_t WebManager::apiRogueNetworksHandler(httpd_req_t* req) {
    auto networks = getNetworks();
    char json[2048];
    int offset = 0;
    
    offset += snprintf(json + offset, sizeof(json) - offset, "{\"networks\":[");
    
    for (size_t i = 0; i < networks.size() && i < 20; i++) {
        const auto& net = networks[i];
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
            net.bssid[0], net.bssid[1], net.bssid[2],
            net.bssid[3], net.bssid[4], net.bssid[5]);
        
        offset += snprintf(json + offset, sizeof(json) - offset,
            "%s{\"ssid\":\"%s\",\"bssid\":\"%s\",\"channel\":%d,\"rssi\":%d}",
            (i > 0) ? "," : "",
            net.ssid[0] ? net.ssid : "(hidden)",
            bssid, net.channel, net.rssi);
    }
    
    offset += snprintf(json + offset, sizeof(json) - offset, "],\"count\":%zu}", networks.size());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebManager::apiRogueSetTargetHandler(httpd_req_t* req) {
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\":\"no_data\"}", 17);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    char ssid[33] = {0};
    char bssidStr[18] = {0};
    uint8_t channel = 6;
    
    char* p = content;
    while (*p) {
        if (strncmp(p, "ssid=", 5) == 0) {
            strncpy(ssid, p + 5, sizeof(ssid) - 1);
        } else if (strncmp(p, "bssid=", 6) == 0) {
            strncpy(bssidStr, p + 6, sizeof(bssidStr) - 1);
        } else if (strncmp(p, "channel=", 8) == 0) {
            channel = atoi(p + 8);
        }
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    
    uint8_t bssid[6] = {0};
    if (strlen(bssidStr) == 17) {
        sscanf(bssidStr, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
            &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
    }
    
    RogueManager& rogue = getRogueManager();
    rogue.setTargetNetwork(ssid, bssid, channel);
    rogue.saveToNVS();
    
    char json[128];
    snprintf(json, sizeof(json), "{\"status\":\"ok\",\"ssid\":\"%s\",\"channel\":%d}", ssid, channel);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebManager::apiFilesHandler(httpd_req_t* req) {
    char json[256];
    snprintf(json, sizeof(json), "{\"freeSpace\":%lld,\"files\":[]}", 
        (long long)getStorageFreeSpace());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebManager::apiWigleHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"not_implemented\"}", 26);
    return ESP_OK;
}

esp_err_t WebManager::apiPwnagotchiHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"not_implemented\"}", 26);
    return ESP_OK;
}

WebManager& getWebManager() {
    static WebManager wm;
    return wm;
}

}