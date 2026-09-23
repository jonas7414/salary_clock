#include "button.h"
#include "button_logic.h"
#include "app_system.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"
namespace {
void task(void *) {
    ButtonLogic buttons[2]; const gpio_num_t pins[]={GPIO_NUM_0,GPIO_NUM_14};
    TickType_t wake=xTaskGetTickCount();
    while (true) {
        const uint32_t now=uint32_t(esp_timer_get_time()/1000);
        uint32_t held=0;
        for (int i=0;i<2;++i) {
            const auto action=buttons[i].update(gpio_get_level(pins[i])==0,now);
            const auto h=buttons[i].held_ms(now); if (h>held) held=h;
            if (action==ButtonAction::Page) { const uint8_t page=1; xQueueSend(page_events(),&page,0); }
            else if (action==ButtonAction::Setup || action==ButtonAction::Reset) {
                ESP_LOGI("button","%s requested",action==ButtonAction::Setup ? "Setup" : "Factory reset");
                if (system_request(action==ButtonAction::Setup ? SystemCommand::Setup : SystemCommand::FactoryReset)!=ESP_OK)
                    ESP_LOGW("button","Command queue full");
            }
        }
        button_publish(held);
        system_heartbeat(CriticalTask::Button);
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(10));
    }
}
}
esp_err_t button_start() {
    gpio_config_t c{}; c.pin_bit_mask=(1ULL<<0)|(1ULL<<14); c.mode=GPIO_MODE_INPUT; c.pull_up_en=GPIO_PULLUP_ENABLE;
    esp_err_t err=gpio_config(&c); if (err!=ESP_OK) return err;
    return xTaskCreate(task,"button",TASK_STACK_BUTTON,nullptr,TASK_PRIORITY_BUTTON,nullptr)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
