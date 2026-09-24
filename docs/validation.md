# 驗證與實板驗收

## 軟體證據

本文件中的測試與實板項目分開記錄，不能由「有程式碼」推定實板已通過。

主機共 **1,548,636 項 C++ 檢查通過**（核心 461,061、堆疊 1,087,162、NVS 72、JSON 58、OTA 283），另有 6 組 Python 行事曆測試與 3 組 release guard 測試。可攜的結果見 [host-results.txt](host-results.txt) 與 [build-results.json](build-results.json)，後者包含韌體 SHA256。OTA 實板測試矩陣見 [ota.md](ota.md)。

v1.4.0 使用 Core 6.2.0、Espressif32 6.12.0、ESP-IDF 5.5.0；`core_dir = .tools/platformio` 沿用現有套件。本機雙環境建置記錄為 `.artifacts/build-v1.4.0.log`。實板完整更新、斷電及回滾仍需依下列程序驗收；CI 發布狀態見 [GitHub Actions](https://github.com/jonas7414/salary_clock/actions/workflows/release.yml)。

1.2.0 的裝置紀錄已確認 SNTP、GitHub TLS 驗證與 1.2.1 版本偵測，下載到至少 10% 後遇到兩次 errno 11，最後保留舊版。v1.2.2 新增短讀／EAGAIN 恢復、不重複資料、重試上限、總期限、取消與重新下載測試；尚未以此修正版在實板重現相同網路停頓。

| 項目 | 證據來源 |
| --- | --- |
| 薪資、日曆、設定、Wi-Fi policy、按鍵、物理、動畫、renderer | `python tools/test_host.py` 的 `.artifacts/host/results.txt` |
| NVS 首次開機、腐敗、版本不符、commit failure、reset、錯誤時保留、OTA 寫入互斥 | 真正 `app_config.cpp` + `tests/test_nvs.cpp` 的 fake NVS adapter |
| OTA SemVer／Release JSON／SHA／TLS host／健康期／串流故障注入 | `tests/test_ota.cpp`，直接編譯 production policy 與 transfer code |
| 發布版本／分區／產物表檢查 | `python -m unittest discover -s tests -p test_release_tools.py -v` |
| API 設定解析、型別、範圍、時間、重複鍵、巢狀 JSON、密碼保留／清除 | 真正 `config_json.cpp` + ESP-IDF 的 cJSON + `tests/test_json.cpp` |
| 11 個狀態橫向畫面 | `.artifacts/host/screens.png`；實際 C++ renderer |
| 硬幣 16 秒動畫與落定堆疊 | `.artifacts/host/coin_physics.gif`、`stack_settled.png`；實際 C++ physics / renderer |
| 吃飯與下班休息動畫 | `.artifacts/host/lunch.gif`、`rest.gif`；實際 C++ renderer，完整循環 120／100 幀 |
| 16 枚硬幣接觸、支撐、休眠、喚醒及容量壓力 | `tests/test_coin_stack.cpp`；同一套物理引擎，八組隨機種子與額外指定案例 |
| 三種風格、11 個畫面狀態 | 33 個畫面完整／分段逐像素一致、buffer guard、掌機模式最多四色；[themes.png](themes.png) |
| 風格儲存與舊版相容 | NVS 獨立 key、原始設定 blob 不變、缺少／損毀風格回到原版、API 非法值拒絕 |
| 政府行事曆 | 2026–2027 全部 730 天、每月工作日／工時、未收錄年度、補班與閏年生成案例 |
| 加錢、時段與假日動畫 | 三種風格、整幀／strip 一致性、文字進退場、假日首次進入與跨日重播規則 |
| 手機設定頁 | JavaScript 語法與 DOM ID 參照檢查；本版未重做瀏覽器互動測試 |
| ESP-IDF 預設 build | `pio run -e tdisplay_s3`：SUCCESS；`.artifacts/build-v1.4.0.log` |
| 無 PSRAM build | `pio run -e tdisplay_s3_no_psram`：SUCCESS；`.artifacts/build-v1.4.0.log` |

| 組態 | 靜態 RAM | 程式 Flash 用量 | firmware.bin |
| --- | ---: | ---: | ---: |
| PSRAM 預設 | 38,988 bytes | 1,283,959 bytes | 1,284,368 bytes |
| 無 PSRAM | 37,560 bytes | 1,273,979 bytes | 1,274,384 bytes |

上述 RAM 是 linker 的靜態用量，不含執行時 task、Wi-Fi、HTTP 與 framebuffer 配置。已檢查實際產生的 sdkconfig：兩者均為 ESP32-S3、16 MB QIO、FreeRTOS 1000 Hz；預設為 Octal PSRAM，備援組態 `SPIRAM=false`。兩者 firmware 都小於 4 MiB app partition。

本版三種風格的月工時及休假場景預覽已做目視檢查，皆為 320×170 橫向。2026 年 9 月預設排程確認為 20 個工作日、160 小時。行事曆快照與產生的 C++ 資料一致，僅保留 2026 年起資料。ESP32 HTTP／RF 整合、實際螢幕流暢度及完整 OTA 更新仍待實板驗收。

## 實板程序

下列需要指定的 LILYGO 目標板；本次 v1.4.0 更新尚未在實板驗證。使用者提供的 v1.0.0 連線記錄已納入回歸案例。Host adapter 測試不替代 flash 斷電或 RF 測試。

1. **無 NVS／第一次上電**：重設此產品 namespace 後，應顯示橫向 Setup、MAC 尾碼 AP 與 192.168.4.1。用手機開網頁，確認沒有外部資源要求。
2. **Wi-Fi Scan／hidden SSID**：掃描顯示 RSSI、安全模式；點選填入。手動輸入隱藏 SSID。錯誤密碼重開機後約 20 秒回 Setup。
3. **設定與重開機**：設定一組非預設月薪／排程；重啟後確認仍保留，午休及每日費率使用該組設定，工作日依政府行事曆。輪流選擇三種風格並重啟，確認四頁與時段動畫套用同一風格；舊版升級預設經典，恢復原廠也回到經典。
4. **SNTP 失敗**：阻擋 UDP 123 或上游 Internet，畫面等待且薪資不計算。恢復後應同步進入正確狀態。
5. **斷線重連**：同步後關閉 AP，金額仍依時間增加；重新開 AP 後背景 reconnect，不重設金額、不自動開設定入口。
6. **跨界排程**：將排程設為接近當下的短時段，觀察上班、午休開始、午休結束、下班；午休改播漢堡輕晃、下午回到原有硬幣堆疊、下班改播小貓睡覺。午休與下班不生成新幣，金額固定時動畫仍持續。
7. **實板重開機一致性**：記錄設定、日期時間及金額；重開後 SNTP 校時，同時刻公式結果須一致，不從零累加。
8. **按鍵**：短按四頁循環；五秒時仍未 reboot，放開進設定；十秒 reset。兩顆按鈕都測試。
9. **PSRAM／partial**：資訊頁確認 DOUBLE；燒錄 no_psram 組態後確認 PARTIAL。兩者畫面與數值相同。
10. **動畫與時間**：累積至 16 顆硬幣，記錄每幀耗時與 missed frame；觀察方向、科技字體、RGB 顏色、圖像偏移、堆疊碰撞、sleep、頂層替換、flicker／tearing。午休／下班應切換情境動畫，不能露出硬幣或遮住金額、時間與進度條；換頁回來及在該時段重開機也應顯示正確動畫。目標 20–30 FPS，預設排程 25 FPS。
11. **耐久與 heap**：至少運行一個工作日，包含大量換頁與多次設定掃描，檢查 heap 不持續下降、watchdog 不重啟。
12. **NVS 電氣驗證**：只有在可復原的測試板上做寫入時斷電；重開應得到完整舊設定或完整新設定，損毀時進 Setup。

## v1.1.0 字體、堆疊與時段動畫回歸

數字使用等寬字格，避免金額和秒數跳動；940 個字形的 4-bit 點陣已與原字型逐像素核對。字形保留負 bearing、頂端及右側超出 advance 的筆畫；renderer 的整幀與 10-row strip 仍逐像素一致。月統計過長費率會縮字，不丟失末位數字。

硬幣直徑為 18–24 px，固定 16 枚上限。案例包含多層落定、硬幣不穿透、嚴格左右與底部邊界、失去支撐掉落、撞擊喚醒、滿額保留底層、長 dt 截限、非法 dt、20/30 FPS 一致性。午休與下班停止生成，右側分別顯示漢堡輕晃與小貓睡覺動畫；下午回到原堆疊。

吃飯 120 幀、休息 100 幀完整循環均檢查整幀／10-row strip 一致性、buffer guard、動畫只改動右側區域，以及隱藏硬幣不洩漏。固定薪資與時鐘時仍確認畫面隨毫秒時間改變。預覽見 [lunch.gif](lunch.gif) 與 [rest.gif](rest.gif)，尚待實板確認播放流暢度。

16 秒預覽加快生成間隔為 0.72 秒，仍使用 25 FPS 真實物理更新；產品生成間隔仍至少 2.5 秒。預覽最後呈現 16 顆落定狀態，實板效能需依上述程序測量。

## v1.0.1 Wi-Fi 回歸

`wifi_tests()` 共 74 項斷言，包含使用者記錄的時間順序：第 12 秒 association 成功、第 16 秒仍在等待 DHCP、第 21 秒尚未取得 IP。修正後這兩個時間點都不重複 connect，也不切換 Setup；在第 70 秒取得 IP 可繼續執行。另一案例在第 72 秒（association 後完整 60 秒）仍無 IP，才回設定模式。

其他案例涵蓋進行中的掃描／認證不被定時重啟、初次連不到基地台的 20 秒期限、斷線後五秒重試、曾取得 IP 後的連線或 DHCP 逾時只要求一次中斷、失去 IP 後重新等待 DHCP，以及手動設定模式禁止重試。

實板更新後應依序看到 `Station associated; waiting for DHCP`、`Station obtained IP`、`SNTP synchronized`。若仍出現 `DHCP timeout`，需另查路由器分配 IP 的狀況；目前的舊記錄無法判定 DHCP 沒有完成的外部原因。

## 需求對應

附件 1–7、13：`app_config` / `app_core` / `platformio.ini`；8–12：`wifi_manager` / `setup_portal`；14–19、44–48：薪資核心、salary task、四頁 renderer；20–43、59、62、66：coin physics、ui_animation、renderer；49–51、57：native LCD owner、雙 framebuffer 與 fallback strip；52：button task / ButtonLogic；53–56、60–61：Wi-Fi policy、SNTP、EventGroup、集中 task 設定；63：host tests 與上述板端矩陣；64–65、67：README、完整專案與 build artifacts。
