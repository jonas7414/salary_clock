# Wi-Fi 連線優化研究

2026-09-23；基準為目前 v1.1.0、PlatformIO espressif32 6.12.0、ESP-IDF 5.5.0。使用情境：可連上家用分享器 SW，但連線慢或偶爾斷線。本次僅研究及新增本文，未修改韌體、升級套件或操作分享器。

建議先做「分階段診斷、快速首次重連、開機失敗後繼續恢復」，再根據紀錄調整 AP 選擇與校時。改善重連策略能縮短中斷，不能據此保證消除無線干擾或分享器造成的斷線。

## 現有證據

| 項目 | 本機程式／有效設定 | 判讀 |
| --- | --- | --- |
| 重試 | `components/app_core/include/wifi_policy.h:7,14,31`，固定五秒 | 短暫斷線後會先多等五秒，才開始下一次連線 |
| 開機期限 | 同檔 `:23`，首次取得 IP 前有二十秒 AP 連線總預算 | 已儲存正確設定時，分享器較慢開機仍可能讓裝置進入 Setup |
| Setup | `wifi_manager.cpp:53`、policy `:10` | 進入後停止自動重試，分享器恢復也不會自行退出 |
| DHCP | policy `:17`，關聯成功後獨立等六十秒 | 舊版「已關聯仍重複 connect／二十秒直接切 Setup」已修正 |
| 掃描 | `wifi_manager.cpp:156` 的 STA 結構零初始化 | FAST_SCAN、無 BSSID 綁定、channel=0；未針對同名多 AP 做完整候選比較 |
| 省電／RSSI | `wifi_manager.cpp:163,116` | 已關閉 modem sleep；RSSI 只在已關聯時每秒查一次，不需要重做 |
| 校時 | `time_manager.cpp:16–19`；有效 sdkconfig 的 `LWIP_SNTP_STARTUP_DELAY=y`、`MAXIMUM_STARTUP_DELAY=5000` | 取得 IP 後才啟動 SNTP，首次請求另有 0–5 秒隨機等待 |
| 事件處理 | Wi-Fi 回呼更新 EventGroup，task 每 250 ms 讀快照 | 快速發生的成功／斷線轉換可能未被 policy 觀察到 |

先前附件的 v1.0.1 日誌出現兩次 `auth → init`、reason 2，以及一次 reason 205，當時還沒進入 DHCP。更早的一份則已關聯、等待 IP；兩者不是同一階段。如今能連上，仍需新的斷線紀錄才能判定目前原因。reason 2 是認證逾時或 AP 回報相同理由，不能單憑它認定密碼錯誤。[官方原因碼](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/wifi.html#wi-fi-reason-code)

## 建議順序與具體方案

### 1. 先把慢的階段量出來

保留每次嘗試的編號及單調時鐘時間：開始 connect、STA_CONNECTED、GOT_IP、SNTP synchronized、DISCONNECTED。紀錄 BSSID、channel、驗證模式、斷線 reason／RSSI、連續失敗次數及下次重試時間；不記錄密碼。

目前日誌有部分事件，但沒有完整嘗試與耗時統計。IDF 5.5 本機 `esp_wifi_types_generic.h:1143–1161` 已提供關聯與斷線事件所需欄位，無須在未連線時查 AP 資訊。執行中的診斷優先輸出 USB 與資訊頁；現有 HTTP 僅在 Setup 開啟，不為診斷另外開放 LAN 入口。

將「開始連線到關聯」「關聯到 IP」「IP 到校時」分開統計，才知道要改 Wi-Fi、DHCP，還是 DNS／NTP。螢幕目前必須完成首次校時才計薪，等待畫面不等於 Wi-Fi 還沒連上。

### 2. 首次快速重連，失敗後逐步放慢

提案參數為 1、2、4、8、15 秒，十五秒封頂，加少量隨機偏移；實際數值需 A/B 測試。穩定取得 IP 三十秒後重置失敗計數，避免短暫 GOT_IP 讓抖動連線一直高速重試。

第一次的一秒相較現在五秒，可以少四秒「人為等待」，不代表整段認證、DHCP 只需一秒。後續退避會減少長停機時的掃描，但分享器恢復後最壞可能多等到退避上限。

僅在確定斷線後安排重試；正在認證、等待 DHCP、手動設定或設定頁掃描時不得重複 connect。主動 disconnect 與網路故障需區分。[官方重連說明](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/wifi.html#wi-fi-reconnect)

### 3. 已有設定時，開機失敗仍能恢復

建議把二十秒視為顯示「仍在重試」的時點，而非永久停止自動恢復。已有設定就持續有限速的 STA 重連，保留長按進 Setup；沒有設定或使用者主動進 Setup 時維持現有流程。DHCP 仍保留獨立期限。

若產品要同時自動提供設定 AP 與背景重連，需另外處理 APSTA 通道變化、使用者正在填表、掃描與 connect 互斥，以及恢復連線後關閉設定服務；不是單純移除 `if (setup)` 就能完成。這是產品行為調整，列為獨立變更。

### 4. 依實測選擇掃描方式

先從事件紀錄確認 SW 是否有多個 BSSID。若有 Mesh／延伸器，或連續失敗，可比較 `WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL`；單一 AP 不一定會更快。先保留 `failure_retry_cnt=0`，必要時再試 1；不要直接加到 3–5。[官方掃描與選擇規則](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/wifi.html#station-basic-configuration)、[重試欄位說明](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/network/esp_wifi.html#_CPPv417wifi_sta_config_t)

本機 5.5.0 header 亦確認上述語義。可研究 RAM 中保存上次成功 channel 作掃描優先提示，失敗即回完整選擇；收益待測。先不強綁 BSSID、不在每次重連寫 NVS。調整 station config 必須在連線嘗試間進行，保留既有 Connecting／DHCP 保護。

### 5. 校時改善另行量測

若慢主要在 GOT_IP 之後，可把 `CONFIG_LWIP_SNTP_MAXIMUM_STARTUP_DELAY` 從 5000 降至 1000，保留隨機等待；它只降低首次 NTP 請求前的等待，不能解決 Wi-Fi 斷線。大量裝置同時上電時需考慮 NTP 請求集中。[官方 SNTP 組態](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/kconfig-reference.html#config-lwip-sntp-maximum-startup-delay)

目前 time task 只在首次 GOT_IP 後初始化一次；若首次校時尚未完成就斷線，重連後會依原有 SNTP 排程等待。可在重得 IP 且尚未校時時，受冷卻時間限制地重啟 SNTP。使用已初始化的 `esp_netif_sntp_start()`，不要重複 init；本機 `esp_netif_sntp.h:89–95` 與實作 `esp_netif_sntp.c:170–179` 確認可重新啟動。保留現有兩個伺服器與已校準的系統時鐘。[官方 SNTP 生命週期](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/network/esp_netif_programming.html#sntp-service)

### 6. 後續加強事件與升級評估

回呼可傳送帶時間戳與連線代次的小型事件，由單一 Wi-Fi task 依序處理。現在若 GOT_IP 與斷線都落在同一次 250 ms 間隔內，policy 可能不知道曾成功，仍套用首次二十秒規則；這是程式可推導的競態，不是已證明的實板根因。實作時需測事件佇列溢位、過期事件與逾時／GOT_IP 同時到達。

目前 5.5.0 可先完成前述策略優化。PlatformIO 6.13.0 配套 5.5.3，且會更新工具鏈；5.5.3 發行記錄包含掃描／斷線事件遺失、deauth reason 解析等修正，值得另做對照，但尚無證據本裝置命中這些 bug。先不為研究觸發下載，也不直接跨到 IDF 6.x。[PlatformIO 6.13.0 發行說明](https://github.com/platformio/platform-espressif32/releases/tag/v6.13.0)、[ESP-IDF 5.5.3 修正](https://github.com/espressif/esp-idf/releases/tag/v5.5.3)

## 暫不列為第一批修改

- 已設 `WIFI_PS_NONE`，再調省電不是新增改善；保留 PMF 相容設定。
- RX static/dynamic/BA 為 6/12/6，符合本機 SDK 條件；沒有接收緩衝不足的證據，先不為此增加 internal RAM。
- 舊 SW 紀錄為 WPA2，WPA3 SAE 模式調整不能視為已知修復。
- 尚無持續 DHCP 故障證據，不直接改固定 IP；固定 IP 也不能修復認證階段斷線。

## 驗證方式

1. 同一位置、分享器與供電，現版／優化版各二十次啟動，分階段記錄成功率、耗時中位數及 p95。
2. 分享器已恢復可連線後，量測裝置再取得 IP 的時間；另測路由器晚二十五、六十、一百二十秒啟動，不按鍵也能恢復。
3. 桌面位置與靠近分享器各觀察至少一個工作日，記錄每小時斷線數、原因、RSSI、BSSID，分辨環境與策略因素。
4. 測只斷 Internet、保留區域 Wi-Fi，確認不誤判為 Wi-Fi 斷線；已校時薪資仍繼續。
5. 測 Setup 掃描／儲存、連續錯誤驗證、同名多 AP、事件快速交錯；確保不重複 connect、不破壞 DHCP 與畫面更新。

目前沒有新韌體的完整斷線 log，也沒有在目標板執行上述對照；本文是可實作、可驗證的優化建議，並非實測改善幅度。
