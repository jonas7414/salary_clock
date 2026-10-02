#include "crash_report.h"
#include "esp_core_dump.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <algorithm>

void crash_report_print() {
    constexpr const char *TAG="crash";
    const auto *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                  ESP_PARTITION_SUBTYPE_DATA_COREDUMP,nullptr);
    if (!partition) {
        ESP_LOGW(TAG,"Crash storage unavailable: install the new partition table over USB");
        return;
    }
    // IDF 5.5 returns INVALID_SIZE for erased flash, unlike newer API docs.
    uint32_t stored_size=0;
    const auto read=esp_partition_read(partition,0,&stored_size,sizeof(stored_size));
    if (read!=ESP_OK) {
        ESP_LOGW(TAG,"Cannot read crash storage: %s",esp_err_to_name(read));
        return;
    }
    if (stored_size==UINT32_MAX) {
        ESP_LOGI(TAG,"No saved crash; flash capture ready (%lu bytes)",static_cast<unsigned long>(partition->size));
        return;
    }
    if (stored_size<28 || stored_size>partition->size) {
        ESP_LOGW(TAG,"Saved crash has invalid length; left untouched");
        return;
    }
    const auto checked=esp_core_dump_image_check();
    if (checked!=ESP_OK) {
        ESP_LOGW(TAG,"Saved crash unreadable (%s); left untouched",esp_err_to_name(checked));
        return;
    }
    size_t address=0,size=0;
    const auto found=esp_core_dump_image_get(&address,&size);
    if (found!=ESP_OK) {
        ESP_LOGW(TAG,"Cannot locate saved crash: %s",esp_err_to_name(found));
        return;
    }
    ESP_LOGW(TAG,"Saved crash: %u bytes at 0x%08x; export with tools/read_coredump.py --port COMx --output crash.bin",
             unsigned(size),unsigned(address));
    char reason[200]{};
    if (esp_core_dump_get_panic_reason(reason,sizeof(reason))==ESP_OK)
        ESP_LOGW(TAG,"Panic reason: %s",reason);
    esp_core_dump_summary_t summary{};
    const auto result=esp_core_dump_get_summary(&summary);
    if (result!=ESP_OK) {
        ESP_LOGW(TAG,"Summary unavailable: %s; raw dump retained",esp_err_to_name(result));
        return;
    }
    ESP_LOGW(TAG,"Crashed task: %.16s; PC=0x%08x; exception=%u; ELF SHA256=%.*s",
             summary.exc_task,unsigned(summary.exc_pc),unsigned(summary.ex_info.exc_cause),
             int(sizeof(summary.app_elf_sha256)),reinterpret_cast<const char *>(summary.app_elf_sha256));
    for (unsigned i=0;i<std::min<uint32_t>(summary.exc_bt_info.depth,16);++i)
        ESP_LOGW(TAG,"Backtrace[%u]: 0x%08x",i,unsigned(summary.exc_bt_info.bt[i]));
    if (summary.exc_bt_info.corrupted) ESP_LOGW(TAG,"Backtrace is incomplete/corrupt; inspect the raw dump");
}
