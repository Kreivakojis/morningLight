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
#include "animation_engine.h"
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

    // Dark mode status
    bool dm_active = false;
    if (time_manager_is_synced()) {
        int d = time_manager_get_day_of_week();
        int h = time_manager_get_hour();
        int m = time_manager_get_minute();
        if (d >= 0) dm_active = config_manager_is_dark_mode_blocking(d, h, m);
    }
    cJSON_AddBoolToObject(json, "dark_mode_active", dm_active);

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

    // LED configuration
    cJSON_AddNumberToObject(json, "led_type", config->led_type);
    cJSON_AddStringToObject(json, "led_type_name",
        config->led_type == CONFIG_LED_TYPE_WS2811 ? "WS2811" : "PWM");
    cJSON_AddNumberToObject(json, "led_count", config->led_count);

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
    bool restart_required = false;

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

    // LED type change requires restart
    cJSON *led_type = cJSON_GetObjectItem(json, "led_type");
    if (cJSON_IsNumber(led_type)) {
        uint8_t new_type = (uint8_t)led_type->valuedouble;
        ESP_LOGI(TAG, "LED type: current=%d, new=%d", config->led_type, new_type);
        if (new_type != config->led_type) {
            config->led_type = new_type;
            restart_required = true;
        }
    }

    // LED count change requires restart for WS2811
    cJSON *led_count = cJSON_GetObjectItem(json, "led_count");
    if (cJSON_IsNumber(led_count)) {
        uint16_t new_count = (uint16_t)led_count->valuedouble;
        ESP_LOGI(TAG, "LED count: current=%d, new=%d", config->led_count, new_count);
        if (new_count >= 1 && new_count <= 300) {
            if (new_count != config->led_count && config->led_type == CONFIG_LED_TYPE_WS2811) {
                restart_required = true;
            }
            config->led_count = new_count;
        }
    }

    ESP_LOGI(TAG, "Saving config with led_type=%d, led_count=%d", config->led_type, config->led_count);

    config_manager_save();
    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddBoolToObject(resp, "restart_required", restart_required);
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
        cJSON_AddNumberToObject(a, "animation_preset", alarm->animation_preset);
        cJSON_AddNumberToObject(a, "cooldown_min", alarm->cooldown_min);
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

    cJSON *anim_preset = cJSON_GetObjectItem(json, "animation_preset");
    if (cJSON_IsNumber(anim_preset)) {
        int8_t val = (int8_t)anim_preset->valuedouble;
        if (val >= -1 && val < ANIMATION_MAX_PRESETS) {
            alarm->animation_preset = val;
        }
    }

    cJSON *cooldown = cJSON_GetObjectItem(json, "cooldown_min");
    if (cJSON_IsNumber(cooldown)) {
        uint8_t val = (uint8_t)cooldown->valuedouble;
        if (val <= 60) alarm->cooldown_min = val;
    }

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

    // Stop any running animation before LED test
    if (animation_engine_is_running()) {
        animation_engine_stop();
    }

    cJSON *off = cJSON_GetObjectItem(json, "off");
    cJSON *color_temp = cJSON_GetObjectItem(json, "color_temp");
    cJSON *brightness = cJSON_GetObjectItem(json, "brightness");

    // Check dark mode (skip for "off" commands)
    if (!cJSON_IsTrue(off) && time_manager_is_synced()) {
        int d = time_manager_get_day_of_week();
        int h = time_manager_get_hour();
        int m = time_manager_get_minute();
        if (d >= 0 && config_manager_is_dark_mode_blocking(d, h, m)) {
            cJSON_Delete(json);
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", false);
            cJSON_AddStringToObject(resp, "error", "Dark mode active");
            return send_json_response(req, resp);
        }
    }

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

// GET /api/animation/presets
static esp_err_t api_animation_presets_get_handler(httpd_req_t *req)
{
    device_config_t *config = config_manager_get();
    cJSON *json = cJSON_CreateObject();
    cJSON *presets = cJSON_AddArrayToObject(json, "presets");

    for (int i = 0; i < ANIMATION_MAX_PRESETS; i++) {
        animation_preset_t *p = &config->animation_presets[i];
        cJSON *preset = cJSON_CreateObject();
        cJSON_AddNumberToObject(preset, "id", i);
        cJSON_AddStringToObject(preset, "name", p->name);
        cJSON_AddNumberToObject(preset, "wavelength", p->wavelength);
        cJSON_AddNumberToObject(preset, "amplitude", p->amplitude);
        cJSON_AddNumberToObject(preset, "speed", p->speed);
        cJSON_AddNumberToObject(preset, "base_brightness", p->base_brightness);
        cJSON_AddNumberToObject(preset, "variation", p->variation);
        cJSON_AddNumberToObject(preset, "color_temp", p->color_temp);
        cJSON_AddItemToArray(presets, preset);
    }

    cJSON_AddNumberToObject(json, "active", animation_engine_get_active_preset());
    cJSON_AddBoolToObject(json, "running", animation_engine_is_running());

    return send_json_response(req, json);
}

// POST /api/animation/presets
static esp_err_t api_animation_presets_post_handler(httpd_req_t *req)
{
    cJSON *json = parse_json_body(req);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *id_item = cJSON_GetObjectItem(json, "id");
    if (!cJSON_IsNumber(id_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing preset id");
        return ESP_FAIL;
    }

    int id = (int)id_item->valuedouble;
    if (id < 0 || id >= ANIMATION_MAX_PRESETS) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid preset id");
        return ESP_FAIL;
    }

    animation_preset_t *preset = animation_engine_get_preset(id);
    if (!preset) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get preset");
        return ESP_FAIL;
    }

    // Update preset fields
    cJSON *name = cJSON_GetObjectItem(json, "name");
    if (cJSON_IsString(name)) {
        strncpy(preset->name, name->valuestring, sizeof(preset->name) - 1);
        preset->name[sizeof(preset->name) - 1] = '\0';
    }

    cJSON *wavelength = cJSON_GetObjectItem(json, "wavelength");
    if (cJSON_IsNumber(wavelength)) {
        preset->wavelength = (float)wavelength->valuedouble;
        if (preset->wavelength < 2.0f) preset->wavelength = 2.0f;
        if (preset->wavelength > 300.0f) preset->wavelength = 300.0f;
    }

    cJSON *amplitude = cJSON_GetObjectItem(json, "amplitude");
    if (cJSON_IsNumber(amplitude)) {
        preset->amplitude = (uint8_t)amplitude->valuedouble;
        if (preset->amplitude > 100) preset->amplitude = 100;
    }

    cJSON *speed = cJSON_GetObjectItem(json, "speed");
    if (cJSON_IsNumber(speed)) {
        preset->speed = (float)speed->valuedouble;
        if (preset->speed < 0.0f) preset->speed = 0.0f;
        if (preset->speed > 5.0f) preset->speed = 5.0f;
    }

    cJSON *base = cJSON_GetObjectItem(json, "base_brightness");
    if (cJSON_IsNumber(base)) {
        preset->base_brightness = (uint8_t)base->valuedouble;
        if (preset->base_brightness > 100) preset->base_brightness = 100;
    }

    cJSON *variation = cJSON_GetObjectItem(json, "variation");
    if (cJSON_IsNumber(variation)) {
        preset->variation = (uint8_t)variation->valuedouble;
        if (preset->variation > 100) preset->variation = 100;
    }

    cJSON *color_temp = cJSON_GetObjectItem(json, "color_temp");
    if (cJSON_IsNumber(color_temp)) {
        preset->color_temp = (uint16_t)color_temp->valuedouble;
        if (preset->color_temp < 2000) preset->color_temp = 2000;
        if (preset->color_temp > 6500) preset->color_temp = 6500;
    }

    config_manager_save();
    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddNumberToObject(resp, "id", id);
    return send_json_response(req, resp);
}

// POST /api/animation/start
static esp_err_t api_animation_start_handler(httpd_req_t *req)
{
    cJSON *json = parse_json_body(req);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *preset_id = cJSON_GetObjectItem(json, "preset_id");
    if (!cJSON_IsNumber(preset_id)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing preset_id");
        return ESP_FAIL;
    }

    int id = (int)preset_id->valuedouble;
    cJSON_Delete(json);

    // Check dark mode before starting animation
    if (time_manager_is_synced()) {
        int d = time_manager_get_day_of_week();
        int h = time_manager_get_hour();
        int m = time_manager_get_minute();
        if (d >= 0 && config_manager_is_dark_mode_blocking(d, h, m)) {
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", false);
            cJSON_AddStringToObject(resp, "error", "Dark mode active");
            return send_json_response(req, resp);
        }
    }

    esp_err_t ret = animation_engine_start(id);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", ret == ESP_OK);
    if (ret != ESP_OK) {
        cJSON_AddStringToObject(resp, "error", esp_err_to_name(ret));
    }
    return send_json_response(req, resp);
}

// POST /api/animation/stop
static esp_err_t api_animation_stop_handler(httpd_req_t *req)
{
    animation_engine_stop();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    return send_json_response(req, resp);
}

// GET /api/darkmode
static esp_err_t api_darkmode_get_handler(httpd_req_t *req)
{
    device_config_t *config = config_manager_get();
    cJSON *json = cJSON_CreateObject();
    cJSON *schedules = cJSON_AddArrayToObject(json, "schedules");

    for (int i = 0; i < DARK_MODE_MAX_SCHEDULES; i++) {
        dark_mode_schedule_t *s = &config->dark_schedules[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", i);
        cJSON_AddBoolToObject(item, "enabled", s->enabled);
        cJSON_AddNumberToObject(item, "start_hour", s->start_hour);
        cJSON_AddNumberToObject(item, "start_minute", s->start_minute);
        cJSON_AddNumberToObject(item, "end_hour", s->end_hour);
        cJSON_AddNumberToObject(item, "end_minute", s->end_minute);
        cJSON_AddNumberToObject(item, "days_mask", s->days_mask);
        cJSON_AddBoolToObject(item, "allow_override", s->allow_override);
        cJSON_AddItemToArray(schedules, item);
    }

    return send_json_response(req, json);
}

// POST /api/darkmode
static esp_err_t api_darkmode_post_handler(httpd_req_t *req)
{
    cJSON *json = parse_json_body(req);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    device_config_t *config = config_manager_get();

    cJSON *id_item = cJSON_GetObjectItem(json, "id");
    if (!cJSON_IsNumber(id_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id");
        return ESP_FAIL;
    }

    int id = (int)id_item->valuedouble;
    if (id < 0 || id >= DARK_MODE_MAX_SCHEDULES) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid id (0-2)");
        return ESP_FAIL;
    }

    dark_mode_schedule_t *s = &config->dark_schedules[id];

    cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled)) s->enabled = cJSON_IsTrue(enabled);

    cJSON *start_hour = cJSON_GetObjectItem(json, "start_hour");
    if (cJSON_IsNumber(start_hour)) {
        uint8_t v = (uint8_t)start_hour->valuedouble;
        if (v <= 23) s->start_hour = v;
    }

    cJSON *start_minute = cJSON_GetObjectItem(json, "start_minute");
    if (cJSON_IsNumber(start_minute)) {
        uint8_t v = (uint8_t)start_minute->valuedouble;
        if (v <= 59) s->start_minute = v;
    }

    cJSON *end_hour = cJSON_GetObjectItem(json, "end_hour");
    if (cJSON_IsNumber(end_hour)) {
        uint8_t v = (uint8_t)end_hour->valuedouble;
        if (v <= 23) s->end_hour = v;
    }

    cJSON *end_minute = cJSON_GetObjectItem(json, "end_minute");
    if (cJSON_IsNumber(end_minute)) {
        uint8_t v = (uint8_t)end_minute->valuedouble;
        if (v <= 59) s->end_minute = v;
    }

    cJSON *days_mask = cJSON_GetObjectItem(json, "days_mask");
    if (cJSON_IsNumber(days_mask)) s->days_mask = (uint8_t)days_mask->valuedouble;

    cJSON *allow_override = cJSON_GetObjectItem(json, "allow_override");
    if (cJSON_IsBool(allow_override)) s->allow_override = cJSON_IsTrue(allow_override);

    config_manager_save();
    cJSON_Delete(json);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddNumberToObject(resp, "id", id);
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
    config.max_open_sockets = 7;  // Reduced to avoid socket exhaustion

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

    // Animation endpoints
    httpd_uri_t api_anim_presets_get = { .uri = "/api/animation/presets", .method = HTTP_GET, .handler = api_animation_presets_get_handler };
    httpd_uri_t api_anim_presets_post = { .uri = "/api/animation/presets", .method = HTTP_POST, .handler = api_animation_presets_post_handler };
    httpd_uri_t api_anim_start = { .uri = "/api/animation/start", .method = HTTP_POST, .handler = api_animation_start_handler };
    httpd_uri_t api_anim_stop = { .uri = "/api/animation/stop", .method = HTTP_POST, .handler = api_animation_stop_handler };

    // Dark mode endpoints
    httpd_uri_t api_darkmode_get = { .uri = "/api/darkmode", .method = HTTP_GET, .handler = api_darkmode_get_handler };
    httpd_uri_t api_darkmode_post = { .uri = "/api/darkmode", .method = HTTP_POST, .handler = api_darkmode_post_handler };

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
    httpd_register_uri_handler(server, &api_anim_presets_get);
    httpd_register_uri_handler(server, &api_anim_presets_post);
    httpd_register_uri_handler(server, &api_anim_start);
    httpd_register_uri_handler(server, &api_anim_stop);
    httpd_register_uri_handler(server, &api_darkmode_get);
    httpd_register_uri_handler(server, &api_darkmode_post);
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
