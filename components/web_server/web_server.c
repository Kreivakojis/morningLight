#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "cJSON.h"

#include "web_server.h"
#include "wifi_manager.h"
#include "config_manager.h"
#include "time_manager.h"
#include "led_controller.h"
#include "sunrise_engine.h"
#include "dns_server.h"

static const char *TAG = "web_server";

// Embedded files
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");

// Forward declare app events
extern esp_event_base_t APP_EVENTS;
enum {
    APP_EVENT_LED_TEST_START = 14,
    APP_EVENT_LED_TEST_STOP,
};

typedef struct {
    uint8_t r, g, b, brightness;
} app_event_led_test_t;

static httpd_handle_t server = NULL;

// Helper to send JSON response
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

// Helper to parse JSON from request
static cJSON *parse_json_body(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 4096) {
        return NULL;
    }

    char *buf = malloc(content_len + 1);
    if (buf == NULL) {
        return NULL;
    }

    int received = httpd_req_recv(req, buf, content_len);
    if (received != content_len) {
        free(buf);
        return NULL;
    }
    buf[content_len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    return json;
}

// GET / - Serve index.html
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

// GET /style.css
static esp_err_t style_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, style_css_start, style_css_end - style_css_start);
    return ESP_OK;
}

// GET /app.js
static esp_err_t app_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, app_js_start, app_js_end - app_js_start);
    return ESP_OK;
}

// GET /api/status
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();

    // WiFi status
    cJSON_AddStringToObject(json, "wifi_state",
        wifi_manager_is_connected() ? "connected" : "disconnected");

    char ap_ssid[33];
    wifi_manager_get_ap_ssid(ap_ssid);
    cJSON_AddStringToObject(json, "ap_ssid", ap_ssid);

    uint32_t ip = wifi_manager_get_ip();
    if (ip) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR,
                 (int)(ip & 0xFF), (int)((ip >> 8) & 0xFF),
                 (int)((ip >> 16) & 0xFF), (int)((ip >> 24) & 0xFF));
        cJSON_AddStringToObject(json, "ip", ip_str);
    }

    // Time
    cJSON_AddBoolToObject(json, "time_synced", time_manager_is_synced());
    if (time_manager_is_synced()) {
        char time_str[32];
        time_manager_get_time_str(time_str, sizeof(time_str));
        cJSON_AddStringToObject(json, "time", time_str);
    }

    // Sunrise engine
    cJSON_AddStringToObject(json, "sunrise_state",
        sunrise_engine_get_state_str());
    cJSON_AddNumberToObject(json, "brightness", led_controller_get_brightness());

    // Config status
    device_config_t *config = config_manager_get();
    cJSON_AddBoolToObject(json, "setup_complete", config ? config->setup_complete : false);

    return send_json_response(req, json);
}

// GET /api/wifi/scan
static esp_err_t api_wifi_scan_handler(httpd_req_t *req)
{
    // Start scan if not complete
    if (!wifi_manager_scan_complete()) {
        wifi_manager_start_scan();
        // Wait for scan (max 5 seconds)
        for (int i = 0; i < 50 && !wifi_manager_scan_complete(); i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    cJSON *json = cJSON_CreateObject();
    cJSON *networks = cJSON_AddArrayToObject(json, "networks");

    wifi_scan_result_t results[20];
    uint16_t count = wifi_manager_get_scan_results(results, 20);

    for (uint16_t i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", results[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", results[i].rssi);
        cJSON_AddBoolToObject(net, "secure", results[i].authmode != 0);
        cJSON_AddItemToArray(networks, net);
    }

    return send_json_response(req, json);
}

// POST /api/wifi/connect
static esp_err_t api_wifi_connect_handler(httpd_req_t *req)
{
    cJSON *json = parse_json_body(req);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON *password_item = cJSON_GetObjectItem(json, "password");

    if (!cJSON_IsString(ssid_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = cJSON_IsString(password_item) ? password_item->valuestring : NULL;

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    // Save credentials
    device_config_t *config = config_manager_get();
    strncpy(config->wifi.ssid, ssid, sizeof(config->wifi.ssid) - 1);
    if (password) {
        strncpy(config->wifi.password, password, sizeof(config->wifi.password) - 1);
    } else {
        config->wifi.password[0] = '\0';
    }
    config->setup_complete = true;
    config_manager_save();

    // Connect
    esp_err_t ret = wifi_manager_connect(ssid, password);

    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", ret == ESP_OK);
    return send_json_response(req, resp);
}

// GET /api/config
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    device_config_t *config = config_manager_get();
    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "timezone", config->timezone);
    cJSON_AddNumberToObject(json, "brightness_max", config->brightness_max);
    cJSON_AddNumberToObject(json, "pwm_frequency", config->pwm_frequency);
    cJSON_AddBoolToObject(json, "setup_complete", config->setup_complete);

    // WiFi (don't expose password)
    cJSON *wifi = cJSON_AddObjectToObject(json, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", config->wifi.ssid);

    return send_json_response(req, json);
}

// POST /api/config
static esp_err_t api_config_post_handler(httpd_req_t *req)
{
    cJSON *json = parse_json_body(req);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    device_config_t *config = config_manager_get();

    cJSON *tz = cJSON_GetObjectItem(json, "timezone");
    if (cJSON_IsString(tz)) {
        strncpy(config->timezone, tz->valuestring, sizeof(config->timezone) - 1);
        time_manager_set_timezone(config->timezone);
    }

    cJSON *brightness = cJSON_GetObjectItem(json, "brightness_max");
    if (cJSON_IsNumber(brightness)) {
        config->brightness_max = (uint8_t)brightness->valuedouble;
    }

    cJSON *freq = cJSON_GetObjectItem(json, "pwm_frequency");
    if (cJSON_IsNumber(freq)) {
        uint32_t new_freq = (uint32_t)freq->valuedouble;
        if (new_freq >= 100 && new_freq <= 40000) {
            config->pwm_frequency = new_freq;
            led_controller_set_frequency(new_freq);
        }
    }

    config_manager_save();
    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    return send_json_response(req, resp);
}

// GET /api/alarms
static esp_err_t api_alarms_get_handler(httpd_req_t *req)
{
    device_config_t *config = config_manager_get();
    cJSON *json = cJSON_CreateObject();
    cJSON *alarms = cJSON_AddArrayToObject(json, "alarms");

    for (int i = 0; i < 8; i++) {
        alarm_config_t *alarm = &config->alarms[i];
        if (!alarm->enabled && alarm->hour == 0 && alarm->minute == 0) {
            continue;
        }

        cJSON *a = cJSON_CreateObject();
        cJSON_AddNumberToObject(a, "id", i);
        cJSON_AddBoolToObject(a, "enabled", alarm->enabled);
        cJSON_AddNumberToObject(a, "hour", alarm->hour);
        cJSON_AddNumberToObject(a, "minute", alarm->minute);
        cJSON_AddNumberToObject(a, "duration_min", alarm->duration_min);
        cJSON_AddNumberToObject(a, "days_mask", alarm->days_mask);
        cJSON_AddNumberToObject(a, "color_temp", alarm->color_temp);
        cJSON_AddNumberToObject(a, "brightness", alarm->brightness);
        cJSON_AddItemToArray(alarms, a);
    }

    return send_json_response(req, json);
}

// POST /api/alarms
static esp_err_t api_alarms_post_handler(httpd_req_t *req)
{
    cJSON *json = parse_json_body(req);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    device_config_t *config = config_manager_get();

    cJSON *id_item = cJSON_GetObjectItem(json, "id");
    int id = cJSON_IsNumber(id_item) ? (int)id_item->valuedouble : -1;

    // Find slot if no ID specified
    if (id < 0 || id >= 8) {
        for (int i = 0; i < 8; i++) {
            if (!config->alarms[i].enabled) {
                id = i;
                break;
            }
        }
    }

    if (id < 0 || id >= 8) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No alarm slots available");
        return ESP_FAIL;
    }

    alarm_config_t *alarm = &config->alarms[id];

    cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled)) alarm->enabled = cJSON_IsTrue(enabled);

    cJSON *hour = cJSON_GetObjectItem(json, "hour");
    if (cJSON_IsNumber(hour)) alarm->hour = (uint8_t)hour->valuedouble;

    cJSON *minute = cJSON_GetObjectItem(json, "minute");
    if (cJSON_IsNumber(minute)) alarm->minute = (uint8_t)minute->valuedouble;

    cJSON *duration = cJSON_GetObjectItem(json, "duration_min");
    if (cJSON_IsNumber(duration)) alarm->duration_min = (uint8_t)duration->valuedouble;

    cJSON *days = cJSON_GetObjectItem(json, "days_mask");
    if (cJSON_IsNumber(days)) alarm->days_mask = (uint8_t)days->valuedouble;

    cJSON *color_temp = cJSON_GetObjectItem(json, "color_temp");
    if (cJSON_IsNumber(color_temp)) alarm->color_temp = (uint16_t)color_temp->valuedouble;

    cJSON *brightness = cJSON_GetObjectItem(json, "brightness");
    if (cJSON_IsNumber(brightness)) alarm->brightness = (uint8_t)brightness->valuedouble;

    config_manager_save();
    sunrise_engine_update_schedule();

    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddNumberToObject(resp, "id", id);
    return send_json_response(req, resp);
}

// POST /api/led/test
static esp_err_t api_led_test_handler(httpd_req_t *req)
{
    cJSON *json = parse_json_body(req);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *off = cJSON_GetObjectItem(json, "off");
    cJSON *color_temp = cJSON_GetObjectItem(json, "color_temp");
    cJSON *brightness = cJSON_GetObjectItem(json, "brightness");

    if (cJSON_IsTrue(off)) {
        led_controller_off();
    } else if (cJSON_IsNumber(color_temp) && cJSON_IsNumber(brightness)) {
        uint16_t temp = (uint16_t)color_temp->valuedouble;
        uint8_t bright = (uint8_t)brightness->valuedouble;
        led_controller_set_color_temp(temp, bright);
    }

    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddNumberToObject(resp, "current_freq", led_controller_get_frequency());
    return send_json_response(req, resp);
}

// POST /api/time/sync
static esp_err_t api_time_sync_handler(httpd_req_t *req)
{
    time_manager_force_sync();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    return send_json_response(req, resp);
}

// 404 handler for captive portal redirect
static esp_err_t http_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Check for captive portal detection URLs
    const char *uri = req->uri;
    if (strstr(uri, "generate_204") || strstr(uri, "hotspot-detect") ||
        strstr(uri, "connecttest") || strstr(uri, "ncsi")) {
        // Redirect to root
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Default: redirect to root
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// OPTIONS handler for CORS
static esp_err_t options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (server != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting web server");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Static files
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t style = { .uri = "/style.css", .method = HTTP_GET, .handler = style_get_handler };
    httpd_uri_t app_js = { .uri = "/app.js", .method = HTTP_GET, .handler = app_js_get_handler };

    // API endpoints
    httpd_uri_t api_status = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler };
    httpd_uri_t api_wifi_scan = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = api_wifi_scan_handler };
    httpd_uri_t api_wifi_connect = { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = api_wifi_connect_handler };
    httpd_uri_t api_config_get = { .uri = "/api/config", .method = HTTP_GET, .handler = api_config_get_handler };
    httpd_uri_t api_config_post = { .uri = "/api/config", .method = HTTP_POST, .handler = api_config_post_handler };
    httpd_uri_t api_alarms_get = { .uri = "/api/alarms", .method = HTTP_GET, .handler = api_alarms_get_handler };
    httpd_uri_t api_alarms_post = { .uri = "/api/alarms", .method = HTTP_POST, .handler = api_alarms_post_handler };
    httpd_uri_t api_led_test = { .uri = "/api/led/test", .method = HTTP_POST, .handler = api_led_test_handler };
    httpd_uri_t api_time_sync = { .uri = "/api/time/sync", .method = HTTP_POST, .handler = api_time_sync_handler };

    // CORS preflight
    httpd_uri_t options = { .uri = "/api/*", .method = HTTP_OPTIONS, .handler = options_handler };

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &style);
    httpd_register_uri_handler(server, &app_js);
    httpd_register_uri_handler(server, &api_status);
    httpd_register_uri_handler(server, &api_wifi_scan);
    httpd_register_uri_handler(server, &api_wifi_connect);
    httpd_register_uri_handler(server, &api_config_get);
    httpd_register_uri_handler(server, &api_config_post);
    httpd_register_uri_handler(server, &api_alarms_get);
    httpd_register_uri_handler(server, &api_alarms_post);
    httpd_register_uri_handler(server, &api_led_test);
    httpd_register_uri_handler(server, &api_time_sync);
    httpd_register_uri_handler(server, &options);

    // 404 handler for captive portal
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_handler);

    // Start DNS server for captive portal
    dns_server_start();

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (server == NULL) {
        return ESP_OK;
    }

    dns_server_stop();
    httpd_stop(server);
    server = NULL;

    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}

bool web_server_is_running(void)
{
    return server != NULL;
}
