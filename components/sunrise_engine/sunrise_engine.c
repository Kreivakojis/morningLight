#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_event.h"
#include "sunrise_engine.h"
#include "config_manager.h"
#include "time_manager.h"
#include "led_controller.h"

// External curve functions
extern float sunrise_curve_apply(sunrise_curve_t curve, float t);

static const char *TAG = "sunrise";

// Forward declare app events
extern esp_event_base_t APP_EVENTS;
enum {
    APP_EVENT_ALARM_TRIGGERED = 7,
    APP_EVENT_ALARM_COMPLETE,
    APP_EVENT_ALARM_CANCELLED,
};

typedef struct {
    uint8_t alarm_id;
    uint32_t progress_percent;
} app_event_alarm_t;

// State
static struct {
    bool initialized;
    sunrise_state_t state;
    sunrise_curve_t curve;

    // Scheduled alarm info
    int8_t scheduled_alarm_id;
    int minutes_until_alarm;

    // Active sunrise info
    uint32_t start_time_ms;
    uint32_t duration_ms;
    uint16_t color_temp;
    uint8_t max_brightness;
    float current_progress;

    // Tasks and timers
    TaskHandle_t task;
    TimerHandle_t schedule_timer;
} state = {0};

static void set_state(sunrise_state_t new_state)
{
    if (state.state != new_state) {
        ESP_LOGI(TAG, "State: %d -> %d", state.state, new_state);
        state.state = new_state;
    }
}

static void update_leds(float progress)
{
    // Apply curve
    float curved_progress = sunrise_curve_apply(state.curve, progress);

    // Calculate brightness
    uint8_t brightness = (uint8_t)(curved_progress * state.max_brightness);

    ESP_LOGD(TAG, "Sunrise: %.0f%% -> %d%%", progress * 100.0f, brightness);

    // Set color temperature and brightness
    led_controller_set_color_temp(state.color_temp, brightness);

    state.current_progress = progress;
}

static void sunrise_task(void *arg)
{
    ESP_LOGI(TAG, "Sunrise started: %lums, %dK, %d%%",
             state.duration_ms, state.color_temp, state.max_brightness);

    state.start_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    set_state(SUNRISE_STATE_ACTIVE);

    // Post alarm triggered event
    app_event_alarm_t event = {
        .alarm_id = state.scheduled_alarm_id >= 0 ? state.scheduled_alarm_id : 0,
        .progress_percent = 0,
    };
    esp_event_post(APP_EVENTS, APP_EVENT_ALARM_TRIGGERED, &event, sizeof(event), 0);

    while (state.state == SUNRISE_STATE_ACTIVE) {
        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - state.start_time_ms;
        float progress = (float)elapsed / state.duration_ms;

        if (progress >= 1.0f) {
            progress = 1.0f;
            update_leds(progress);
            set_state(SUNRISE_STATE_COMPLETE);

            event.progress_percent = 100;
            esp_event_post(APP_EVENTS, APP_EVENT_ALARM_COMPLETE, &event, sizeof(event), 0);
            break;
        }

        update_leds(progress);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_ML_SUNRISE_UPDATE_INTERVAL_MS));
    }

    if (state.state == SUNRISE_STATE_CANCELLED) {
        led_controller_off();
        esp_event_post(APP_EVENTS, APP_EVENT_ALARM_CANCELLED, &event, sizeof(event), 0);
    }

    ESP_LOGI(TAG, "Sunrise task ended");
    state.task = NULL;
    vTaskDelete(NULL);
}

static void check_schedule(void)
{
    if (!time_manager_is_synced()) {
        state.scheduled_alarm_id = -1;
        state.minutes_until_alarm = -1;
        return;
    }

    int day = time_manager_get_day_of_week();
    int hour = time_manager_get_hour();
    int minute = time_manager_get_minute();

    if (day < 0 || hour < 0 || minute < 0) {
        return;
    }

    uint8_t alarm_id;
    alarm_config_t *next = config_manager_get_next_alarm(day, hour, minute, &alarm_id);

    if (next == NULL) {
        state.scheduled_alarm_id = -1;
        state.minutes_until_alarm = -1;
        set_state(SUNRISE_STATE_IDLE);
        return;
    }

    state.scheduled_alarm_id = alarm_id;
    state.minutes_until_alarm = time_manager_minutes_until(next->hour, next->minute, -1);

    ESP_LOGD(TAG, "Next alarm: #%d at %02d:%02d, in %d min",
             alarm_id, next->hour, next->minute, state.minutes_until_alarm);

    // Check if it's time to start (within 1 minute window)
    if (state.minutes_until_alarm <= 0 && state.state != SUNRISE_STATE_ACTIVE) {
        ESP_LOGI(TAG, "Starting sunrise #%d: %dmin, %dK, %d%%",
                 alarm_id, next->duration_min, next->color_temp, next->brightness);

        state.duration_ms = next->duration_min * 60 * 1000;
        state.color_temp = next->color_temp;
        state.max_brightness = next->brightness;

        // Limit to global max brightness
        device_config_t *config = config_manager_get();
        if (state.max_brightness > config->brightness_max) {
            state.max_brightness = config->brightness_max;
        }

        // Start task
        if (state.task == NULL) {
            xTaskCreate(sunrise_task, "sunrise", 4096, NULL, 5, &state.task);
        }
    } else if (state.state == SUNRISE_STATE_IDLE || state.state == SUNRISE_STATE_COMPLETE) {
        set_state(SUNRISE_STATE_SCHEDULED);
    }
}

static void schedule_timer_callback(TimerHandle_t timer)
{
    if (state.state != SUNRISE_STATE_ACTIVE) {
        check_schedule();
    }
}

esp_err_t sunrise_engine_init(void)
{
    ESP_LOGI(TAG, "Initializing sunrise engine");

    if (state.initialized) {
        return ESP_OK;
    }

    state.state = SUNRISE_STATE_IDLE;
    state.curve = SUNRISE_CURVE_LOGARITHMIC;  // Most natural
    state.scheduled_alarm_id = -1;
    state.minutes_until_alarm = -1;

    // Create schedule check timer (every 30 seconds)
    state.schedule_timer = xTimerCreate("sunrise_sched", pdMS_TO_TICKS(30000),
                                        pdTRUE, NULL, schedule_timer_callback);
    if (state.schedule_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer");
        return ESP_ERR_NO_MEM;
    }

    xTimerStart(state.schedule_timer, 0);

    state.initialized = true;
    ESP_LOGI(TAG, "Sunrise engine initialized");

    return ESP_OK;
}

void sunrise_engine_update_schedule(void)
{
    ESP_LOGI(TAG, "Updating schedule");
    check_schedule();
}

sunrise_state_t sunrise_engine_get_state(void)
{
    return state.state;
}

const char *sunrise_engine_get_state_str(void)
{
    switch (state.state) {
        case SUNRISE_STATE_IDLE:      return "idle";
        case SUNRISE_STATE_SCHEDULED: return "scheduled";
        case SUNRISE_STATE_ACTIVE:    return "active";
        case SUNRISE_STATE_COMPLETE:  return "complete";
        case SUNRISE_STATE_CANCELLED: return "cancelled";
        default:                      return "unknown";
    }
}

int sunrise_engine_get_progress(void)
{
    if (state.state != SUNRISE_STATE_ACTIVE) {
        return -1;
    }
    return (int)(state.current_progress * 100);
}

int sunrise_engine_get_active_alarm(void)
{
    return state.scheduled_alarm_id;
}

esp_err_t sunrise_engine_start_manual(uint8_t duration_min, uint16_t color_temp, uint8_t brightness)
{
    if (state.state == SUNRISE_STATE_ACTIVE) {
        ESP_LOGW(TAG, "Sunrise already active");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting manual sunrise: %d min, %dK, %d%%",
             duration_min, color_temp, brightness);

    state.scheduled_alarm_id = -1;
    state.duration_ms = duration_min * 60 * 1000;
    state.color_temp = color_temp;
    state.max_brightness = brightness;

    // Limit to global max brightness
    device_config_t *config = config_manager_get();
    if (state.max_brightness > config->brightness_max) {
        state.max_brightness = config->brightness_max;
    }

    if (state.task == NULL) {
        xTaskCreate(sunrise_task, "sunrise", 4096, NULL, 5, &state.task);
    }

    return ESP_OK;
}

void sunrise_engine_cancel(void)
{
    if (state.state == SUNRISE_STATE_ACTIVE) {
        ESP_LOGI(TAG, "Cancelling sunrise");
        set_state(SUNRISE_STATE_CANCELLED);
    }
}

void sunrise_engine_stop(void)
{
    sunrise_engine_cancel();
    led_controller_off();

    if (state.state == SUNRISE_STATE_COMPLETE) {
        set_state(SUNRISE_STATE_IDLE);
    }
}

void sunrise_engine_set_curve(sunrise_curve_t curve)
{
    state.curve = curve;
    ESP_LOGI(TAG, "Curve set to: %d", curve);
}

void sunrise_engine_deinit(void)
{
    if (!state.initialized) return;

    sunrise_engine_cancel();

    if (state.schedule_timer) {
        xTimerStop(state.schedule_timer, 0);
        xTimerDelete(state.schedule_timer, 0);
        state.schedule_timer = NULL;
    }

    // Wait for task to exit
    if (state.task) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    state.initialized = false;
}
