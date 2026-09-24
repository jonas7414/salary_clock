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
        system_heartbeat(CriticalTask::Salary);
        if (status.date_key && status.date_key != last_date) {
            last_date=status.date_key;
            if (status.work_state==WORK_STATE_NO_CALENDAR)
                ESP_LOGW("salary","Government calendar unavailable for %04d-%02d; update firmware",local.tm_year+1900,local.tm_mon+1);
            else ESP_LOGI("salary","Taiwan calendar: %d work days, %.2f hours; daily salary: %.2f",
                status.monthly_work_days,double(status.monthly_work_seconds)/3600,status.daily_salary);
        }
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000));
    }
}
}
esp_err_t salary_start() {
    return xTaskCreate(task,"salary",TASK_STACK_SALARY,nullptr,TASK_PRIORITY_SALARY,nullptr)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
