#include "salary.h"
#include "app_config.h"
#include "app_system.h"
#include "esp_log.h"
#include "freertos/task.h"
namespace {
void task(void *) {
    TickType_t wake=xTaskGetTickCount();
    int last_date=0;
    while (true) {
        const bool synced=xEventGroupGetBits(system_events()) & TIME_SYNCED_BIT;
        const time_t now=time(nullptr); tm local{};
        if (synced) localtime_r(&now,&local);
        const auto status=calculate_salary(app_config_snapshot(),local,now,synced);
        salary_publish(status);
        if (status.date_key && status.date_key != last_date) {
            last_date=status.date_key;
            ESP_LOGI("salary","Work days: %d; daily salary: %.2f",status.monthly_work_days,status.daily_salary);
        }
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000));
    }
}
}
esp_err_t salary_start() {
    return xTaskCreate(task,"salary",TASK_STACK_SALARY,nullptr,TASK_PRIORITY_SALARY,nullptr)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
