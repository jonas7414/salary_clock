# 崩潰紀錄、回報與 USB 讀取

韌體啟用 ESP-IDF Flash core dump。發生進入 panic handler 的致命錯誤（例如未處理的 CPU 例外、`abort()`、失敗的 `ESP_ERROR_CHECK`、設為 panic 的 watchdog）時，將崩潰原因、暫存器與 task 堆疊保存到 Flash。一般 `ESP_LOGE` 不會觸發保存，也不是連續記錄所有 log。

## 保存方式

- 專用 `coredump` 分區：`0xfc0000`，256 KiB，ELF 格式加 CRC32。
- 永遠只保存最近一次崩潰；即使沒網路，下次崩潰也會覆蓋前一次，不排隊、不增加檔案。固定 256 KiB 分區不會隨錯誤次數成長。開機、查看或 USB 匯出不清除紀錄；確認回報且 Loki 接受後才清除。
- 使用 2048 bytes 的內部 RAM 專用崩潰堆疊；兩種 PSRAM 組態都啟用，不保存全部 heap 或 PSRAM。
- Flash 完成寫入後，斷電及重開機仍保留。突然斷電、brownout、Flash 故障、太早發生的啟動故障或在寫入途中再次失敗，無法保證留下有效紀錄；CRC 失敗會明確回報。
- 紀錄包含記憶體內容，可能帶有設定資料；提供給他人除錯前請確認分享範圍。

機制依據：[ESP-IDF 5.5 Core Dump 文件](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/core_dump.html)。

## 開機後詢問回報

有有效崩潰紀錄、已配置 Loki 憑證，且開機 90 秒內 Wi-Fi 取得 IP、時間有效時，會在其他操作允許的情況下顯示「檢測到錯誤，是否回報？」。沒網路、校時未完成、沒有有效紀錄或正在設定模式時不彈窗，也不嘗試上傳。視窗出現後斷線會關閉，保留紀錄至下次開機。

![錯誤回報彈窗，預設選擇稍後](images/manual/crash-report.png)

- 上方按鈕：切換「回報／稍後」，預設「稍後」。
- 下方按鈕：確認；結果畫面則返回原頁。
- 選「稍後」：保留單筆紀錄，這次開機不再詢問。
- 選「回報」：只嘗試一次 HTTPS POST，TLS 憑證需通過驗證，不追蹤重新導向。與 OTA、日曆共用網路資源鎖，且用維護鎖保護 Flash 清除。
- Loki 回傳完整 HTTP 204：清除整個專用崩潰分區，顯示「錯誤已回報」。
- 斷線、逾時、401、429、5xx 等失敗：不刪紀錄，不在背景無限重試；下一次開機有網路時再詢問。若清除 Flash 失敗則另行提示。

Wi-Fi 取得 IP 不保證外網可達；實際連線失敗同樣保留紀錄。成功回應後、Flash 清除前若突然斷電，下一次回報可能重複；`device` 與 `report_id` 可協助辨識同一筆。此流程不保證雲端恰好一次寫入，但本機始終最多一筆。

## Loki 欄位與本機建置憑證

目的地為 `https://logs-prod-030.grafana.net/loki/api/v1/push`。每次送一個 stream、一行 JSON；`device` 同時是 Loki label 與 JSON 欄位，以 Wi-Fi STA MAC 產生穩定值，例如 `salary-clock-A1B2C3D4E5F`。

Labels 為 `job="salary_clock"`、`source="device"`、`device="…"`。JSON 內容包含 `event="panic"`、`report_id`、崩潰原因、task、PC、exception、最多 16 個 backtrace 位址、損壞標記、core dump 大小與崩潰映像的 ELF SHA256。`reporter_firmware` 是目前負責回報的版本，OTA 回滾後不應誤認為崩潰版本。Loki 時間戳記是回報時刻，不捏造崩潰發生的時間。

回報為錯誤摘要，**不上傳完整 core dump、Wi-Fi 密碼或 Token**。完整 core dump 如有需要，請在同意回報清除之前先 USB 匯出。

本機建置使用 `.tools/secrets/loki_credentials.h`，由元件 CMake 私有 include 引入；該目錄已被 `.gitignore` 排除。未提供此檔案的建置會停用雲端回報，USB 診斷仍可使用。格式：

```cpp
#pragma once
#define SALARY_CLOCK_LOKI_USER "<Logs instance user ID>"
#define SALARY_CLOCK_LOKI_TOKEN "<write-only access token>"
```

正式 Token 只保存在本機忽略檔與編譯後的韌體中，不放入原始碼、文件、版本庫或測試輸出。此工作區的韌體含使用者提供的寫入憑證；一般 CI 建置沒有這個檔案，不能直接當成啟用雲端回報的同等產物。

公開 GitHub Release 明確設定 `SALARY_CLOCK_PUBLIC_BUILD=1`，即使本機存在憑證檔也不編入 Token。公開版不顯示雲端回報彈窗，仍保留 USB 診斷；私人版須自行配置上述檔案並在未設定此環境變數時建置。`tools/release_tools.py --public` 會拒絕含 Grafana Token 的映像；含 Token 的私人韌體與 ELF 應只留在本機，不能作為公開附件。

Grafana Explore 查詢範例：

```logql
{job="salary_clock", source="device", device="salary-clock-A1B2C3D4E5F"} | json
```

API 格式依據：[Loki HTTP API](https://grafana.com/docs/loki/latest/reference/loki-http-api/)；HTTPS 使用 [ESP-IDF HTTP Client](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/protocols/esp_http_client.html)。

## 舊裝置第一次啟用

原本尚未使用的 `storage` 分區縮小為 `0x7a0000`，尾端劃為崩潰區。NVS、otadata 與兩個 OTA 槽位的位置及大小不變；不更動設定 blob。

**必須透過 USB 更新分區表與韌體。一般 OTA 只更新 application，無法替舊裝置新增分區。** 若尚未更新分區表，Monitor 會顯示 `Crash storage unavailable`，韌體仍可運作。專案目前未掛載 storage；若自行修改過韌體並在該區保存檔案，先備份再更新。

先建置並保存該次 `firmware.elf`，再於需要燒錄時執行標準 PlatformIO Upload（包含分區表）。不用 `erase-flash`。燒錄後若仍運行舊版本，請依 OTA 文件檢查啟動槽位，不要直接清空 NVS。

```powershell
pio run -e tdisplay_s3
pio run -e tdisplay_s3 -t upload
```

無 PSRAM 版本改用 `tdisplay_s3_no_psram`。新分區表仍保留舊版 A/B 回滾需要的位置；回滾到尚未啟用 core dump 的舊韌體時，舊韌體不會保存新崩潰。

## Monitor 查看摘要

```powershell
pio device monitor -b 115200
```

開啟 Monitor 後按裝置 RESET。每次開機會輸出 `[crash]`／`crash:` 模組紀錄：是否存在有效 core dump、大小、panic 原因、task 名稱、PC、backtrace 位址與崩潰韌體的 ELF SHA256。資料無效則提示錯誤，不自動清除。若韌體在到達開機摘要之前持續崩潰，可以直接用以下 USB 工具讀取 Flash。

## USB 匯出

先關閉 Monitor，將 `COM5` 換成裝置連接埠。使用已有 pyserial 與 esptool 相依套件的 Python；本工作區可使用 `.tools/venv/Scripts/python.exe`。

```powershell
.tools/venv/Scripts/python.exe tools/read_coredump.py --port COM5 --output .artifacts/crash-001.bin
```

工具只呼叫 `read_flash`：先讀裝置實際分區表並驗證 MD5、範圍與重疊，再依 core dump subtype 讀取資料，不假定裝置已更新。輸出包括：

- `crash-001.bin`：去除 Flash 尾端空白、通過 CRC32 驗證的 core dump。
- `crash-001.bin.partition.bin`：原始分區備份；即使 core dump 已損壞，仍保留供調查。

同名檔案存在時拒絕覆蓋。沒有有效紀錄時回傳非零退出碼，不產生假成功的 core dump。匯出會重置裝置並進入下載模式，完成後按 RESET 恢復運作；無法自動進入時可使用硬體 BOOT／RESET 流程。未提供清除命令，避免誤刪現場；下一次崩潰會覆蓋舊紀錄。

## 解析成函式與原始碼行號

需要 **崩潰當次建置的 `firmware.elf`**，不只是相同版本號。PSRAM 與無 PSRAM 版本不能混用。建置產物通常位於 `.pio/build/<environment>/firmware.elf`，下次編譯前先備份；將 ELF 的 SHA 與開機摘要對照。

在可用的 ESP-IDF Python 環境安裝官方 `esp-coredump`，並把 ESP32-S3 GDB 加入 PATH。離線解析原始二進位 core dump：

```powershell
python -m pip install esp-coredump
esp-coredump --chip esp32s3 info_corefile -t raw -c .artifacts/crash-001.bin .pio/build/tdisplay_s3/firmware.elf
```

也可以用 ESP-IDF 隨附的 `components/espcoredump/espcoredump.py` 執行同樣參數。GDB 位於 PlatformIO 的 `tool-xtensa-esp-elf-gdb/bin`；需要的工具以當前 ESP-IDF 版本為準。沒有 ELF 或解析工具時，仍可先保存原始檔及 Monitor 摘要。

本工作區已安裝解析工具，可直接指定 GDB，無須更改 PATH：

```powershell
.tools/venv/Scripts/esp-coredump.exe --chip esp32s3 info_corefile -g .tools/platformio/packages/tool-xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb.exe -t raw -c .artifacts/crash-001.bin .pio/build/tdisplay_s3/firmware.elf
```

`.tools/` 不受版本控制，其他電腦仍需自行安裝相依套件。

## 驗證範圍

主機測試檢查匯出工具的分區表、CRC、空紀錄、截斷與損壞資料；`test_crash_report.cpp` 檢查離線、同意、預設稍後、單次嘗試、先成功再刪除，以及 JSON 跳脫與單筆格式。`render_pages` 輸出三主題、六種回報狀態，檢查整幀／strip 一致性。建置與 release guard 檢查兩種組態的 core dump 設定、分區和允許覆蓋舊紀錄。這些不等於實板驗證。

實板驗收需另行授權燒錄：用專用測試韌體觸發一次 `abort()`，確認保存完成，重開機讀取摘要、斷電後再次匯出並用相符 ELF 解析；也需檢查沒有紀錄與第二次崩潰覆蓋情境。正式韌體不加入可由設定頁觸發崩潰的測試入口。
