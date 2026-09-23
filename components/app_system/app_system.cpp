#include "app_system.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
namespace {
EventGroupHandle_t events;
QueueHandle_t commands, pages, salary;
SemaphoreHandle_t mutex;
DeviceStatus device;
TaskHealth health;
portMUX_TYPE health_lock=portMUX_INITIALIZER_UNLOCKED;
}
esp_err_t app_system_init() {
    events = xEventGroupCreate();
    commands = xQueueCreate(8,sizeof(SystemCommand));
    pages = xQueueCreate(8,sizeof(uint8_t));
    salary = xQueueCreate(1,sizeof(SalaryStatus));
    mutex = xSemaphoreCreateMutex();
    if (!events || !commands || !pages || !salary || !mutex) return ESP_ERR_NO_MEM;
    SalaryStatus initial{}; xQueueOverwrite(salary,&initial);
    return ESP_OK;
}
EventGroupHandle_t system_events() { return events; }
QueueHandle_t system_commands() { return commands; }
QueueHandle_t page_events() { return pages; }
esp_err_t system_request(SystemCommand c) {
    if (xEventGroupGetBits(events)&OTA_ACTIVE_BIT) return ESP_ERR_INVALID_STATE;
    return xQueueSend(commands,&c,pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
SystemState system_state() {
    const auto bits = xEventGroupGetBits(events);
    if (bits & SYSTEM_ERROR_BIT) return SYSTEM_ERROR;
    if (bits & SETUP_MODE_BIT) return SYSTEM_SETUP_MODE;
    if (!(bits & CONFIG_READY_BIT)) return SYSTEM_BOOTING;
    if (bits & TIME_SYNCED_BIT) return SYSTEM_RUNNING;
    if (bits & WIFI_CONNECTED_BIT) return SYSTEM_SYNCING_TIME;
    return SYSTEM_CONNECTING_WIFI;
}
const char *system_state_name(SystemState s) {
    static const char *names[] = {"booting","setup","connecting_wifi","syncing_time","running","error"};
    return names[static_cast<unsigned>(s) < 6 ? s : SYSTEM_ERROR];
}
void salary_publish(const SalaryStatus &s) { xQueueOverwrite(salary,&s); }
SalaryStatus salary_snapshot() { SalaryStatus s{}; xQueuePeek(salary,&s,0); return s; }
DeviceStatus device_snapshot() { xSemaphoreTake(mutex,portMAX_DELAY); const auto d = device; xSemaphoreGive(mutex); return d; }
void network_publish(const NetworkStatus &n) { xSemaphoreTake(mutex,portMAX_DELAY); device.network=n; xSemaphoreGive(mutex); }
void button_publish(uint32_t t) { xSemaphoreTake(mutex,portMAX_DELAY); device.button_held_ms=t; xSemaphoreGive(mutex); }
void display_publish(uint32_t t,uint32_t d,bool p) {
    xSemaphoreTake(mutex,portMAX_DELAY); device.frame_us=t; device.dropped_frames=d;
    device.partial_rendering=p; xSemaphoreGive(mutex);
}
void time_wait_publish(bool e) { xSemaphoreTake(mutex,portMAX_DELAY); device.sntp_wait_expired=e; xSemaphoreGive(mutex); }
void system_heartbeat(CriticalTask task) {
    const auto now=esp_timer_get_time();
    portENTER_CRITICAL(&health_lock); health.beat(task,now); portEXIT_CRITICAL(&health_lock);
}
bool system_tasks_healthy(int64_t now,int64_t max_age) {
    portENTER_CRITICAL(&health_lock); const bool ok=health.healthy(now,max_age); portEXIT_CRITICAL(&health_lock);
    return ok;
}
