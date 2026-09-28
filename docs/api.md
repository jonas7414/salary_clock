# API 契約

以 [setup_portal.cpp](../components/setup_portal/setup_portal.cpp)、[config_json.cpp](../components/app_config/config_json.cpp)、[app_types.cpp](../components/app_core/app_types.cpp) 為實作依據。

## AP 設定 HTTP

設定 AP 名稱為 `SalaryThief-XXXX`，網址 `http://192.168.4.1`，開放式 AP，最多四個連線。這是區域裝置設定介面，沒有帳號登入或遠端雲端 API。寫入需要設定模式與自訂 header；此 header 不是身分驗證憑證。未啟用 CORS/preflight。

| Method | 路徑 | 回應／用途 |
| --- | --- | --- |
| GET | `/` | 內嵌設定 HTML |
| GET | `/api/scan` | `{ "total": n, "networks": [{"ssid":"…","rssi":-53,"security":"WPA2"}] }`；最多回傳 20 筆，total 可大於陣列長度 |
| GET | `/api/config` | 目前有效設定，回傳 has_password，不回傳 wifi_password |
| GET | `/api/status` | 裝置、連線、時間、日曆、電池與今日收入狀態 |
| POST | `/api/config` | 驗證並保存設定；200 `{"saved":true,"reboot_required":true}` |
| POST | `/api/reboot` | 排入重啟；202 `{"accepted":true}` |
| POST | `/api/reset` | 排入清除設定；202 `{"accepted":true}`；會遺失已存設定 |

所有 POST 都要求 `X-SalaryThief-Request: setup`。`/api/config` 另外要求 `Content-Type: application/json`、1–2048 bytes body。body 為單層 JSON object；禁止巢狀物件／陣列、重複 key、內嵌 NUL、串接 JSON。布林值不能取代整數設定。

`/api/reboot` 與 `/api/reset` 目前不解析 body，前端送 `{}`；202 代表排入 queue，不表示操作已完成。沒有 `/api/ota`、`/api/salary` 或獨立修改到職日的路由。

## 設定欄位

POST 是完整核心設定提交，不是一般 PATCH。下表「必填」欄位每次都需要；選填欄位省略則保留目前值。

| 欄位 | 型別／範圍 | POST |
| --- | --- | --- |
| `config_version` | 整數，必須為 1 | 必填 |
| `wifi_ssid` | 非空字串，最多 32 bytes | 必填 |
| `wifi_password` | 空字串、8–63 bytes，或 64 位十六進位 PSK | 同 SSID 可省略以保留密碼；換 SSID 時必須提供 |
| `monthly_salary` | 整數 1–1,000,000,000 | 必填 |
| `work_days` | 整數 1–127，預設 31；舊設定相容欄位，實際工作日依政府行事曆 | 必填 |
| `timezone` | Asia/Taipei、Asia/Tokyo、Asia/Hong_Kong、Asia/Singapore、UTC | 必填 |
| `ntp_server` | 整數 0（pool.ntp.org，預設）／1（time.cloudflare.com），選定者優先、另一個為備援 | 選填，重啟後套用 |
| `work_start`, `lunch_start`, `lunch_end`, `work_end` | `HH:MM`，00:00–23:59，依列出順序嚴格遞增 | 必填 |
| `display_theme` | 整數 0／1／2 | 選填 |
| `display_on`, `display_off` | `HH:MM`；預設 08:00／19:00，相同表示全天開啟，支援跨夜 | 選填 |
| `page_order` | 0–6 各一次的七字元字串，預設 `0126453` | 選填 |
| `job_start_date` | 真實日期 `YYYY-MM-DD`，1900–2199，預設 `2026-08-01` | 選填 |
| `anniversary_name` | 最多 24 字，限定支援的英數、中日文與標點；不支援 emoji | 選填 |
| `anniversary_date` | `YYYY-MM-DD`，1900–2199；名稱非空時日期必須有效 | 選填 |
| `anniversary_annual` | 整數 0／1，預設 1 | 選填 |
| `anniversary_name_2` … `anniversary_name_5` | 第 2–5 個紀念日名稱，同第一個的字數與字元限制 | 選填 |
| `anniversary_date_2` … `anniversary_date_5` | 第 2–5 個日期，同第一個的日期限制 | 選填 |
| `anniversary_annual_2` … `anniversary_annual_5` | 各自的每年重複設定，整數 0／1 | 選填 |
| `has_password` | GET 的布林值，只表示是否有密碼 | 不用提交 |

紀念日關閉時名称與日期必須同時清空；每年重複的 2/29 在平年以 2/28 計算。日期與字元驗證見 `display_preferences.h`。未知 key 目前不會造成整體拒絕，不代表有儲存它。

最多五個紀念日。第一個沿用無後綴欄位，第 2–5 個使用上述平面欄位，沒有陣列／巢狀 JSON。省略欄位保留目前值，刪除某個項目時同時提交其空名稱與空日期。時鐘頁每五秒輪播非空項目，分別計算倒數／經過天數。

可提交範例（將 Office-WiFi 換成實際 SSID；省略密碼只適用 SSID 未變更）：

```json
{
  "config_version": 1,
  "wifi_ssid": "Office-WiFi",
  "monthly_salary": 40000,
  "work_days": 31,
  "timezone": "Asia/Taipei",
  "work_start": "09:00",
  "lunch_start": "12:00",
  "lunch_end": "13:00",
  "work_end": "18:00",
  "display_theme": 0,
  "display_on": "08:00",
  "display_off": "19:00",
  "page_order": "0126453",
  "job_start_date": "2026-08-01",
  "anniversary_name": "",
  "anniversary_date": "",
  "anniversary_annual": 1
}
```

儲存不會立即換掉 RAM 設定，因此儲存後、重啟前 GET 仍可能讀到舊值。前端流程是先保存，再送 `/api/reboot`；保存成功但重啟請求失敗時應提示重試重啟，不要誤報保存失敗。

## 狀態欄位

`GET /api/status` 的欄位如下，未註明 nullable 的數值／布林仍可能是尚未初始化的預設值。

| 分類 | 欄位 |
| --- | --- |
| 系統／版本 | `system_state`, `firmware_version`, `idf_version`, `config_version`, `uptime_seconds` |
| 網路 | `wifi_connected`, `wifi_associated`, `wifi_connecting`, `ssid`, `ip`, `ap_ssid`, `rssi` |
| 時間 | `time_synced`, `sntp_synced`, `rtc_present`, `rtc_valid`, `sntp_wait_expired` |
| 行事曆 | `calendar_cached_years` 整數陣列、`calendar_last_attempt_utc_day`, `calendar_updating`, `calendar_last_check_success`, `calendar_available`, `work_calendar`（taiwan_government） |
| 薪資 | `earned_money`（今日）、`monthly_work_days`, `monthly_work_hours` |
| 電池 | `battery_state`, `supply_mv`（number/null）, `battery_present_estimate`（true/null，沒有 false） |
| 記憶體／繪圖 | `free_heap`, `free_psram`, `frame_us`, `dropped_frames`, `partial_rendering` |

`job_earned_money`、`job_total_available`、今日剩餘金額及完整 OTA 狀態**尚未由這條 HTTP API 回傳**，不要依名稱猜測端點或欄位。`calendar_available` 不能拿來判定整段到職歷史是否可用。

## 錯誤

主要 handler 錯誤回應為 `{"error":"原因"}`；底層 server／記憶體不足等錯誤不保證同樣 JSON 格式。

| HTTP | 情境 |
| --- | --- |
| 400 | JSON、欄位、日期、工時順序或 Content-Type 錯誤 |
| 403 | 缺自訂 header、值不正確，或不在設定模式 |
| 408 | 接收失敗、逾時或超过接收期限 |
| 413 | 設定 body 為空或大於 2048 bytes |
| 500 | NVS 保存失敗或配置記憶體失敗 |
| 503 | Wi-Fi 掃描不可用／失敗，或系統指令無法排入 |

## 內部 C++ API

| Header | 主要 API 與語意 |
| --- | --- |
| `app_core/include/app_types.h` | `config_defaults`, `config_validate`, `calculate_salary`；純邏輯，SalaryStatus 含到職累積欄位 |
| `app_config/include/app_config.h` | `app_config_init`, snapshot、theme/schedule/preferences getter、save/reset；維護鎖 `app_config_begin_ota/end_ota` |
| `app_config/include/config_json.h` | `config_parse_json`；以目前設定為底，成功後才寫輸出結果 |
| `app_system/include/app_system.h` | snapshot/publish、system_request、event/queue、heartbeat、下載互斥 |
| `display/include/ui_renderer.h` | `UiModel`, `ui_next_page`, `ui_render`；RGB565 buffer，offset/rows 為螢幕座標 |
| `app_core/include/taiwan_calendar.h` | 工作日資料查詢、年度驗證與安裝 |
| `ota_manager/include/ota_manager.h` | `ota_init`, `ota_check_update`, `ota_start_update`, `ota_prompt_next/confirm`, `ota_get_status` |

OTA 請求由 worker 序列處理；檢查不等於安裝，安裝需有效提示與版本確認。Callback 不應長時間阻塞。這些是 firmware C++ API，不是 HTTP 路由。
