# LiPo 電池資訊

適用於本專案的原版 **LILYGO T-Display-S3（ST7789 1.9 吋）**。系統資訊頁會顯示 LiPo／外部供電的推定狀態與量測電壓，不需額外接線；RTC 使用的 GPIO43／GPIO44 不受影響。

## 原廠資料確認

- [LILYGO 原理圖](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/schematic/T_Display_S3.pdf)：查閱的圖面日期為 2024-04-29。`BAT_ADC` 接 GPIO4，R2／R4 均為 100 kΩ，電壓分壓比為 1/2。量測點 `BAT` 位於供電切換及 D3 之後，與電池接頭的 `VBAT` 是不同節點；USB／5V 供電會遮蔽電池電壓。
- [ESP32-S3 Datasheet，Table 2-8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)：GPIO4 對應 ADC1 channel 3。韌體採 [ESP-IDF 5.5 ADC oneshot](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/adc_oneshot.html) 與 [curve-fitting 校正](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/adc_calibration.html)，ADC1 可與 Wi-Fi 同時使用。
- [LILYGO 接腳說明](https://github.com/Xinyuan-LilyGO/T-Display-S3#6-pinout) 明示插入 USB-C 後不能讀取電池電壓；[原廠電壓範例](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/examples/GetBatteryVoltage/GetBatteryVoltage.ino) 使用校正電壓乘以 2，並以 4.3 V 判別高電壓讀值。本專案只將此作為供電來源推定，USB／5V 時不宣稱電池未接。
- 原理圖 U6 標示 **TP4065**，與較早範例註解的 TP4056 不同。[TP4065 Datasheet，Pin Description / Charging status indicator](https://www.toppwr.com/uploadfile/file/20230304/6403023707f41.pdf) 說明 CHRG 會反映充電／未接電池，但板上此腳只接充電 LED，沒有接到 ESP32 GPIO；原板也沒有可讀取剩餘容量的電量計。

因此本功能顯示供電狀態及供電端電壓，不提供剩餘百分比、剩餘時數、充電中或已充飽判定。這些狀態無法由目前可讀的 GPIO4 唯一決定。

## 畫面顯示

| 文字範例 | 意義 |
| --- | --- |
| `BAT:Checking...` | 開機或供電切換後，等待三次連續一致的分類 |
| `BAT:Likely  Supply 3.85V` | 在正常單節 LiPo／USB 5V 的供電方式下，推定目前由電池供電 |
| `BAT:Unknown (USB/5V) 4.75V` | 推定外部供電；無法判斷是否同時接著電池 |
| `BAT:Unknown  Supply 4.31V` | 電壓位於不確定區間，保留量測值而不判定接入狀態 |
| `BAT:Unknown / Read unavailable` | ADC 初始化、校正、取樣失敗，讀值超出合理範圍，或取樣期間不穩定 |

`Supply` 是 GPIO4 經分壓還原的供電端電壓，包含電路壓降及負載影響，**不是直接量到的電芯端電壓**。不使用 USB 枚舉狀態判斷供電，因為充電器可能只有電源、沒有 USB 資料連線。非標準外部低電壓供電也可能落入電池區間，因此 `Likely` 表示推定。

每秒讀取 9 筆校正樣本，採中位數並乘以 2；取樣跨度大於 150 mV 時捨棄。電源分類使用 2.5–4.2 V 的電池相容區間、4.4–5.5 V 的外部供電區間，中間保留不確定帶；需要連續三次分類一致。上述是顯示策略，並非充電控制或電池保護閾值。校正不可用時不以未校正 ADC 值代替電壓。

`/api/status` 增加 `battery_state`、`supply_mv` 及 `battery_present_estimate`。無有效電壓時 `supply_mv` 為 `null`；只有推定電池供電時 `battery_present_estimate` 為 `true`，其他情況均為 `null`，不會把未知表示成未接電池。

## 驗證

主機測試涵蓋開機穩定取樣、USB 插拔時清除舊推定、4.2／4.4 V 邊界、不確定帶、交替雜訊、讀取失敗與恢復；畫面檢查涵蓋三種風格、五種狀態、整幀與分段繪圖一致性。預覽在 `.artifacts/host/battery.png`。

實板仍需確認：USB 單獨供電與 USB＋LiPo 都顯示未知電池狀態；拔除 USB 後若由 LiPo 持續供電，約三秒內顯示 `BAT:Likely`；與電表量得的供電端電壓比較。尚未燒錄或實測電壓誤差。
