#include "display.h"
#include "display_schedule.h"
#include "ui_renderer.h"
#include "ui_animation.h"
#include "panel_init_sequence.h"
#include "app_config.h"
#include "app_system.h"
#include "driver/gpio.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace {
constexpr int STRIP_ROWS=10, STRIPS=SCREEN_HEIGHT/STRIP_ROWS;
constexpr int FRAME_MS=40;
constexpr size_t STRIP_BYTES=SCREEN_WIDTH*STRIP_ROWS*sizeof(uint16_t);
constexpr size_t FRAME_BYTES=SCREEN_WIDTH*SCREEN_HEIGHT*sizeof(uint16_t);
esp_lcd_panel_handle_t panel;
SemaphoreHandle_t transfer_done;
uint16_t *front, *back, *strip;
uint32_t strip_hashes[STRIPS]{};
bool first_frame=true;
bool transferred(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t *,void *) {
    BaseType_t wake=pdFALSE; xSemaphoreGiveFromISR(transfer_done,&wake); return wake==pdTRUE;
}
esp_err_t panel_init() {
    gpio_config_t output{}; output.pin_bit_mask=(1ULL<<15)|(1ULL<<38)|(1ULL<<9);
    output.mode=GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&output));
    gpio_set_level(GPIO_NUM_15,1); gpio_set_level(GPIO_NUM_9,1); gpio_set_level(GPIO_NUM_38,0);
    vTaskDelay(pdMS_TO_TICKS(20));
    transfer_done=xSemaphoreCreateBinary(); if (!transfer_done) return ESP_ERR_NO_MEM;
    esp_lcd_i80_bus_config_t bus{};
    bus.dc_gpio_num=7; bus.wr_gpio_num=8; bus.clk_src=LCD_CLK_SRC_DEFAULT;
    const int data[]={39,40,41,42,45,46,47,48};
    std::copy(data,data+8,bus.data_gpio_nums); bus.bus_width=8; bus.max_transfer_bytes=STRIP_BYTES;
    bus.dma_burst_size=64;
    esp_lcd_i80_bus_handle_t handle;
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus,&handle));
    esp_lcd_panel_io_i80_config_t io{};
    io.cs_gpio_num=6; io.pclk_hz=10000000; io.trans_queue_depth=1;
    io.on_color_trans_done=transferred;
    io.dc_levels.dc_idle_level=0; io.dc_levels.dc_cmd_level=0; io.dc_levels.dc_dummy_level=0; io.dc_levels.dc_data_level=1;
    io.lcd_cmd_bits=8; io.lcd_param_bits=8; io.flags.swap_color_bytes=1;
    esp_lcd_panel_io_handle_t io_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(handle,&io,&io_handle));
    esp_lcd_panel_dev_config_t dev{}; dev.reset_gpio_num=5; dev.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB; dev.bits_per_pixel=16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle,&dev,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel,true));
    // Landscape: ST7789 RAM is 240x320, with a 35-pixel offset on the 170-pixel axis.
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel,false,true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel,0,35));
    for (const auto &cmd:PANEL_COMMANDS)
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle,cmd.command,cmd.data,cmd.count));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
    strip=static_cast<uint16_t *>(heap_caps_aligned_alloc(64,STRIP_BYTES,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
    if (!strip) return ESP_ERR_NO_MEM;
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM)>FRAME_BYTES*2) {
        front=static_cast<uint16_t *>(heap_caps_malloc(FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        back=static_cast<uint16_t *>(heap_caps_malloc(FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if (!front || !back) { heap_caps_free(front); heap_caps_free(back); front=back=nullptr; }
    }
    ESP_LOGI("display","320x170 landscape / %s",back?"PSRAM double buffer":"internal strip buffer");
    return ESP_OK;
}
uint32_t hash(const uint16_t *pixels) {
    uint32_t h=2166136261U;
    for (size_t i=0;i<STRIP_BYTES/2;++i) { h^=pixels[i]; h*=16777619U; }
    return h;
}
esp_err_t flush(const UiModel &model) {
    if (back) ui_render(back,0,SCREEN_HEIGHT,model);
    for (int band=0;band<STRIPS;++band) {
        const int y=band*STRIP_ROWS;
        if (back) {
            const auto *source=back+y*SCREEN_WIDTH;
            if (!first_frame && std::memcmp(source,front+y*SCREEN_WIDTH,STRIP_BYTES)==0) continue;
            std::memcpy(strip,source,STRIP_BYTES);
        } else ui_render(strip,y,STRIP_ROWS,model);
        const uint32_t current=hash(strip);
        // Periodically refresh each strip as well, so a hash collision cannot persist.
        if (!back && !first_frame && current==strip_hashes[band] && (model.uptime%5)!=0) continue;
        const auto err=esp_lcd_panel_draw_bitmap(panel,0,y,SCREEN_WIDTH,y+STRIP_ROWS,strip);
        if (err!=ESP_OK) return err;
        // The DMA source is NEVER modified until the ISR reports transfer completion.
        if (xSemaphoreTake(transfer_done,pdMS_TO_TICKS(500))!=pdTRUE) return ESP_ERR_TIMEOUT;
        strip_hashes[band]=current;
    }
    if (back) std::swap(front,back);
    first_frame=false;
    return ESP_OK;
}
esp_err_t update_screen(const UiModel &model,bool visible,bool &panel_on) {
    if (!visible) {
        if (!panel_on) return ESP_OK;
        gpio_set_level(GPIO_NUM_38,0);
        const auto err=esp_lcd_panel_disp_on_off(panel,false);
        if (err==ESP_OK) panel_on=false;
        return err;
    }
    // Refresh every strip before lighting the backlight, in both buffer modes.
    if (!panel_on) first_frame=true;
    auto err=flush(model);
    if (err==ESP_OK && !panel_on) err=esp_lcd_panel_disp_on_off(panel,true);
    if (err==ESP_OK) { panel_on=true; gpio_set_level(GPIO_NUM_38,1); }
    return err;
}
void task(void *) {
    if (panel_init()!=ESP_OK) {
        xEventGroupSetBits(system_events(),SYSTEM_ERROR_BIT); ESP_LOGE("display","Display initialization failed");
        vTaskDelete(nullptr); return;
    }
    UiAnimation animation(esp_random());
    UiModel model{}; model.config=app_config_snapshot(); model.physics=&animation.physics();
    model.theme=app_config_theme();
    const auto schedule=app_config_display_schedule();
    DisplaySchedulePolicy display_policy;
    bool panel_on=true;
    uint32_t last_button_presses=0;
    std::snprintf(model.idf,sizeof(model.idf),"%s",esp_get_idf_version());
    std::snprintf(model.firmware,sizeof(model.firmware),"%s",APP_FIRMWARE_VERSION);
    TickType_t wake=xTaskGetTickCount(); int64_t last_us=esp_timer_get_time();
    BootAnimation boot(last_us);
    uint32_t dropped=0;
    while (true) {
        const int64_t frame_start=esp_timer_get_time();
        const float dt=std::clamp(float(frame_start-last_us)/1000000.f,0.f,.05f); last_us=frame_start;
        const auto device=device_snapshot(); model.salary=salary_snapshot(); model.system=system_state();
        const auto bits=xEventGroupGetBits(system_events()); model.synced=bits&TIME_SYNCED_BIT; model.connected=bits&WIFI_CONNECTED_BIT;
        model.associated=bits&WIFI_ASSOCIATED_BIT;
        model.sntp_synced=bits&SNTP_SYNCED_BIT;
        model.rtc=device.rtc;
        model.battery=device.battery;
        model.rtc_progress=std::clamp(float(frame_start-model.rtc.activity_started_us)/3200000.f,0.f,
            model.rtc.result==RtcResult::Pending?.8f:1.f);
        model.held_ms=device.button_held_ms; model.sntp_wait_expired=device.sntp_wait_expired;
        model.boot_progress=boot.update(frame_start,model.system,model.held_ms);
        std::snprintf(model.ap_ssid,sizeof(model.ap_ssid),"%s",device.network.ap_ssid);
        std::snprintf(model.ssid,sizeof(model.ssid),"%s",device.network.ssid);
        std::snprintf(model.ip,sizeof(model.ip),"%s",device.network.ip); model.rssi=device.network.rssi;
        model.uptime=uint32_t(frame_start/1000000); model.partial=!back;
        model.animation_ms=uint32_t(frame_start/1000);
        model.free_heap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        model.free_psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        model.frame_us=device.frame_us; model.dropped_frames=dropped;
        uint8_t page;
        while (xQueueReceive(page_events(),&page,0)==pdTRUE) model.page=(model.page+1)%4;
        uint16_t minute=0;
        if (model.synced) {
            time_t now=time(nullptr); tm local{}; localtime_r(&now,&local);
            minute=local.tm_hour*60+local.tm_min;
            std::strftime(model.date,sizeof(model.date),"%Y/%m/%d",&local);
            std::strftime(model.clock,sizeof(model.clock),"%H:%M:%S",&local);
        }
        // Let the first visible salary frame announce holidays after the splash.
        if (model.boot_progress>=1.f) animation.update(model.salary,frame_start,dt);
        model.pulse=animation.pulse();
        model.gain_money=animation.gain_money();
        model.gain_progress=animation.gain_progress();
        model.transition_state=animation.transition_state();
        model.transition_progress=animation.transition_progress();
        // A long press that skips the intro also gets the normal night-time wake,
        // since its original press edge occurred while the intro forced the LCD on.
        const bool pressed=device.button_presses!=last_button_presses || boot.button_wake();
        last_button_presses=device.button_presses;
        const bool force_on=model.boot_progress<1.f ||
            (bits&(SETUP_MODE_BIT|SYSTEM_ERROR_BIT)) || !(bits&CONFIG_READY_BIT);
        const bool visible=display_policy.update(schedule,minute,model.synced,force_on,pressed,model.animation_ms);
        const auto err=update_screen(model,visible,panel_on);
        if (err!=ESP_OK) {
            ESP_LOGE("display","LCD transfer failed: %s",esp_err_to_name(err));
            xEventGroupSetBits(system_events(),SYSTEM_ERROR_BIT);
            // Stop touching the DMA buffer after timeout. Other tasks and the portal stay alive.
            vTaskDelete(nullptr); return;
        }
        const uint32_t elapsed=uint32_t(esp_timer_get_time()-frame_start);
        if (elapsed>FRAME_MS*1000U) { ++dropped; wake=xTaskGetTickCount(); }
        display_publish(elapsed,dropped,!back);
        system_heartbeat(CriticalTask::Display);
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(FRAME_MS));
    }
}
}
esp_err_t display_start() {
    return xTaskCreatePinnedToCore(task,"display",TASK_STACK_DISPLAY,nullptr,TASK_PRIORITY_DISPLAY,nullptr,1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
