#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lm215.h"
#include "credentials.h"

static LM215Display* display    = nullptr;
static uint32_t      frame_count = 0;

// ---------------------------------------------------------------------------
// Test pattern — vertical bars, 8 columns wide.
// ---------------------------------------------------------------------------
static void loadTestPattern() {
    for (int ph = 0; ph < LCD_PHASES; ph++) {
        for (int row = 0; row < LCD_ROWS; row++) {
            for (int col = 0; col < LCD_ROW_BYTES; col++) {
                const uint32_t offset = (uint32_t)ph * LCD_BUF_BYTES
                                      + (uint32_t)row * LCD_ROW_BYTES
                                      + col;
                display->writeByte(offset, ((col / 4) & 1) ? 0x00 : 0xFF);
            }
        }
    }
    display->commitFrame();
}

// ---------------------------------------------------------------------------
// HTTP handlers — esp_http_server reads raw bytes, no String/null-byte issues.
// ---------------------------------------------------------------------------
static esp_err_t handleFrame(httpd_req_t* req) {
    if ((int)req->content_len != LCD_TOTAL_BYTES) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected %d bytes, got %d\n",
                 LCD_TOTAL_BYTES, (int)req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_OK;
    }

    uint8_t  chunk[256];
    int      remaining = LCD_TOTAL_BYTES;
    uint32_t offset    = 0;

    while (remaining > 0) {
        int n = httpd_req_recv(req, (char*)chunk,
                               min(remaining, (int)sizeof(chunk)));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        for (int i = 0; i < n; i++)
            display->writeByte(offset++, chunk[i]);
        remaining -= n;
    }

    display->commitFrame();
    frame_count++;
    httpd_resp_sendstr(req, "OK\n");
    return ESP_OK;
}

static esp_err_t handleStatus(httpd_req_t* req) {
    char json[128];
    snprintf(json, sizeof(json),
             "{\"ip\":\"%s\",\"uptime_ms\":%lu,\"frames\":%lu}\n",
             WiFi.localIP().toString().c_str(), millis(), frame_count);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

static void startHttpServer() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t srv = nullptr;
    if (httpd_start(&srv, &cfg) != ESP_OK) return;

    httpd_uri_t frame_uri  = { "/frame",  HTTP_POST, handleFrame,  nullptr };
    httpd_uri_t status_uri = { "/status", HTTP_GET,  handleStatus, nullptr };
    httpd_uri_t root_uri   = { "/",       HTTP_GET,  handleStatus, nullptr };
    httpd_register_uri_handler(srv, &frame_uri);
    httpd_register_uri_handler(srv, &status_uri);
    httpd_register_uri_handler(srv, &root_uri);
}

// ---------------------------------------------------------------------------

void setup() {
    display = createDisplay();
    display->begin();
    loadTestPattern();

    // WiFi init must run on the Arduino main task — doing it from a custom
    // FreeRTOS task causes the driver to stall at status 255 (WL_NO_SHIELD).
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("lm215");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
        delay(500);

    MDNS.begin("lm215");
    startHttpServer();

    // Must be last: releases LCD task to max priority on Core 1.
    display->startRefreshLoop();
}

void loop() {
    // LCD task holds Core 1 at max priority — loop() is never scheduled.
    vTaskDelay(portMAX_DELAY);
}
