#include "button.h"
#include "button_logic.h"
#include "app_system.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <initializer_list>
namespace {
bool suppress_power_press=false;
void task(void *) {
    ButtonLogic button;
    ButtonLogic previous(true);
    TickType_t wake=xTaskGetTickCount();
    uint32_t presses=0;
    while (true) {
        const uint32_t now=uint32_t(esp_timer_get_time()/1000);
        uint32_t held=0; bool power_held=false;
        if (suppress_power_press) {
            if (gpio_get_level(GPIO_NUM_14)!=0) suppress_power_press=false;
        } else if (!(xEventGroupGetBits(system_events())&SLEEP_REQUESTED_BIT)) {
            const bool was_pressed=button.pressed();
            const auto action=button.update(gpio_get_level(GPIO_NUM_14)==0,now);
            if (!was_pressed && button.pressed()) ++presses;
            held=button.held_ms(now); power_held=button.pressed();
            if (action==ButtonAction::Sleep && !(xEventGroupGetBits(system_events())&OTA_ACTIVE_BIT))
                xEventGroupSetBits(system_events(),SLEEP_REQUESTED_BIT);
            if (action==ButtonAction::Page) { const uint8_t page=1; xQueueSend(page_events(),&page,0); }
            else if (action==ButtonAction::Setup) {
                ESP_LOGI("button","GPIO14 double click: setup requested");
                if (system_request(SystemCommand::Setup)!=ESP_OK)
                    ESP_LOGW("button","Command queue full");
            }
        }
        if (!(xEventGroupGetBits(system_events())&SLEEP_REQUESTED_BIT)) {
            const bool was_pressed=previous.pressed();
            const auto action=previous.update(gpio_get_level(GPIO_NUM_0)==0,now);
            if (!was_pressed && previous.pressed()) ++presses;
            if (action==ButtonAction::Page) { const uint8_t page=255; xQueueSend(page_events(),&page,0); }
        }
        button_publish(held,presses,power_held);
        system_heartbeat(CriticalTask::Button);
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(10));
    }
}
}
[[noreturn]] void button_enter_deep_sleep() {
    // Called before display startup or by the display task after its last transfer.
    for (auto pin : {GPIO_NUM_38,GPIO_NUM_15}) {
        ESP_ERROR_CHECK(gpio_set_direction(pin,GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(pin,0));
        ESP_ERROR_CHECK(gpio_hold_en(pin));
    }
    gpio_deep_sleep_hold_en();
    ESP_ERROR_CHECK(rtc_gpio_init(GPIO_NUM_14));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(GPIO_NUM_14,RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(GPIO_NUM_14));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(GPIO_NUM_14));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(GPIO_NUM_14,0));
    esp_deep_sleep_start();
}
void button_check_wakeup() {
    ESP_ERROR_CHECK(rtc_gpio_deinit(GPIO_NUM_14));
    gpio_config_t c{}; c.pin_bit_mask=(1ULL<<0)|(1ULL<<14); c.mode=GPIO_MODE_INPUT; c.pull_up_en=GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&c));
    if (esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0) {
        // Keep the panel dark and all services stopped until the hold is confirmed.
        const int64_t started=esp_timer_get_time();
        while (esp_timer_get_time()-started<5000000) {
            if (gpio_get_level(GPIO_NUM_14)!=0) button_enter_deep_sleep();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        suppress_power_press=true;
    }
    for (auto pin : {GPIO_NUM_38,GPIO_NUM_15}) {
        ESP_ERROR_CHECK(gpio_set_direction(pin,GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(pin,0));
        ESP_ERROR_CHECK(gpio_hold_dis(pin));
    }
    gpio_deep_sleep_hold_dis();
}
esp_err_t button_start() {
    gpio_config_t c{}; c.pin_bit_mask=(1ULL<<0)|(1ULL<<14); c.mode=GPIO_MODE_INPUT; c.pull_up_en=GPIO_PULLUP_ENABLE;
    esp_err_t err=gpio_config(&c); if (err!=ESP_OK) return err;
    return xTaskCreate(task,"button",TASK_STACK_BUTTON,nullptr,TASK_PRIORITY_BUTTON,nullptr)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
