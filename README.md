# 薪水小偷計算器

一個放在桌上的 ESP32-S3 薪資時鐘，依照月薪與工作排程，即時顯示今天已賺到的金額。上班時小金幣會掉落、碰撞並堆疊；午休換成輕晃的漢堡，下班後則是睡覺的小貓。

介面採繁體中文，使用 **320 × 170 橫向螢幕，USB 接口朝左**。專案以 PlatformIO、ESP-IDF、FreeRTOS 與 C++ 開發。

![畫面預覽](docs/screens.png)

[開機動畫](docs/boot.gif) · [硬幣動畫](docs/coin_physics.gif) · [堆疊近照](docs/stack_settled.png) · [午休動畫](docs/lunch.gif) · [下班動畫](docs/rest.gif)

## 功能

- 即時計算今日收入、剩餘金額與剩餘工時。
- 每次收入增加，金額右上方會跳出當次增額（例如 `+0.22`），向上彈跳、回彈後淡出。
- 上班、午休、午休結束與下班交界會彈出放大文字動畫，約 3 秒後回到原本頁面；完成對時後離線也會觸發。
- 依台灣政府行事曆計算每月工作日與工時，排除國定假日、例假日及午休，計入補班日。
- 放假時顯示「放假啦~」放大動畫，並播放陽光、遮陽傘與躺椅的休假場景。
- 顯示日期、時間、工作狀態與本月薪資統計。
- 四個顯示頁面，透過板載按鈕切換。
- 開機播放約五秒的金幣動畫並顯示目前韌體版本，同時在背景連接 Wi-Fi。
- 三種可選螢幕風格：經典原版、琥珀終端、復古掌機。
- 可設定每日螢幕開關時間，預設 08:00 開啟、19:00 關閉；關閉時按任一板載按鈕可亮起五分鐘。
- 使用手機網頁設定 Wi-Fi、月薪、上下班時間與時區。
- 自動偵測 GPIO18／GPIO17 上的 DS3231；網路校時寫入 RTC，下次開機可離線讀取，並顯示同步動畫。
- 系統資訊顯示 GPIO4 量測的供電電壓與推定電池供電狀態；USB／5V 下電池接入狀態顯示未知，詳見 [LiPo 電池資訊](docs/battery.md)。
- 設定儲存在 NVS，重新開機及 OTA 更新後保留。
- 每次開機自動檢查 GitHub Release，有新版時透過 HTTPS 更新。

## 硬體與開發環境

| 項目 | 規格 |
| --- | --- |
| 開發板 | LILYGO T-Display-S3，1.9 吋 ST7789 LCD |
| 晶片 | ESP32-S3R8，240 MHz |
| Flash | 16 MB QIO，80 MHz |
| PSRAM | 8 MB Octal，80 MHz |
| 開發工具 | PlatformIO Core 6.2.0 |
| 平台 | `espressif32@6.12.0`，ESP-IDF 5.5.0 |

此專案使用原版 T-Display-S3 的螢幕與接腳配置；AMOLED、Pro 與原始 ESP32 T-Display 需要另外調整驅動。

### 選配 DS3231 硬體時鐘

| DS3231 模組 | T-Display-S3 |
| --- | --- |
| SDA | GPIO18 |
| SCL | GPIO17 |
| VCC | 3.3V |
| GND | GND |

使用具備 SDA／SCL 上拉電阻的模組，並安裝模組適用的備援電池，斷電後才能持續走時。INT/SQW 與 32K 不需接線。

韌體每次開機在 I²C 位址 `0x68` 自動偵測，不需額外設定。未接 RTC 時維持原本網路校時與 Wi-Fi 設定流程；接線變更後請重新開機。

- 已存有有效時間：開機直接讀取 RTC 並開始計時，主頁下方顯示 `HWCLOCK MODE`。無 Wi-Fi 時持續顯示薪資時鐘並在背景重連，不會因連線逾時跳回設定頁；仍可長按 5 秒手動設定。
- 首次使用、電池失效或時間無效：等待成功的 SNTP 網路校時，之後寫入 DS3231 並讀回驗證。只有連上 Wi-Fi、尚未取得網路時間時，不會覆寫 RTC。
- 每次 SNTP 校時成功都會更新已偵測到的 RTC；寫入失敗最多再重試兩次，間隔 30 秒，系統時鐘繼續運作。
- 讀取顯示 `RTC READ`／`HWCLOCK READY`；寫入顯示 `RTC SYNC`／`RTC SAVED`，含約 3.2 秒的彈出與跳點動畫。失敗時顯示失敗提示。
- RTC 一律存 UTC，畫面依設定時區顯示。支援 2000–2099 年；RTC 的停振旗標或不合法日期會被拒絕，避免用失效時間計薪。寄存器定義依 [DS3231 原廠資料表](https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf)。

首次尚未設定 Wi-Fi／薪資的裝置仍先進入設定頁。系統資訊的 `SNTP` 狀態只表示本次開機是否完成網路校時；`/api/status` 另提供 `rtc_present` 與 `rtc_valid`。

## 編譯與燒錄

安裝 Python 3.11 或 3.12，將開發板透過 USB 接上電腦後執行：

```sh
python -m pip install platformio==6.2.0
pio run
pio run -t upload
pio device monitor -b 115200
```

也可以使用 VS Code 的 PlatformIO 擴充套件進行 Build、Upload 與 Monitor。

若電腦有多個序列埠，可指定裝置，例如：

```sh
pio run -t upload --upload-port COM5
pio device monitor -p COM5 -b 115200
```

無法進入下載模式時，按住 BOOT、按一下 RST，再依序放開 RST 與 BOOT。上傳完成後按 RST 重新啟動。

預設建置環境為 `tdisplay_s3`，韌體輸出位於 `.pio/build/tdisplay_s3/firmware.bin`。需要停用 PSRAM 時，使用另一個環境：

```sh
pio run -e tdisplay_s3_no_psram
pio run -e tdisplay_s3_no_psram -t upload
```

工具鏈與 ESP-IDF 套件儲存在專案的 `.tools/platformio`，首次建置會下載所需套件。

## 第一次使用

1. 開機先播放金幣動畫與版本號；尚未設定的裝置會在動畫結束後顯示設定模式及 `SalaryThief-XXXX` Wi-Fi 名稱。
2. 用手機連上該 Wi-Fi，若提示沒有網際網路，選擇保持連線。
3. 在瀏覽器開啟 **http://192.168.4.1**。
4. 掃描或輸入家中／辦公室的 Wi-Fi 名稱與密碼。
5. 設定月薪、上下班時間、午休、時區、螢幕風格與螢幕開關時間，按「儲存並重新開機」；工作日完全依台灣政府行事曆。
6. 裝置連上 Wi-Fi 並完成網路校時後，即開始顯示薪資進度。

預設排程為月薪 NT$40,000、依台灣政府行事曆上班、09:00–18:00，午休 12:00–13:00，時區為 Asia/Taipei。

Wi-Fi 使用 2.4 GHz 網路，支援一般密碼或開放網路。設定 AP 沒有密碼，完成設定並重開機後會關閉。

可選時區：Asia/Taipei、Asia/Tokyo、Asia/Hong_Kong、Asia/Singapore、UTC。排程以同一天的當地日期與時間計算，工作日仍使用台灣政府行事曆；不包含跨午夜班別或加班。

## 按鈕與畫面

每次開機先播放約五秒的金幣動畫，右上角顯示目前韌體版本，配色依所選螢幕風格。動畫期間 Wi-Fi、RTC 與網路校時在背景執行；連線與校時完成後直接進入薪資畫面，只有尚未連上 Wi-Fi 且沒有有效時間時才顯示連線提示。已連線但尚未校時則顯示等待校時，RTC 已提供有效時間時仍可離線顯示薪資。

開機動畫期間螢幕保持亮起，結束後恢復每日開關排程；連線失敗仍沿用原有重試與設定模式流程。長按按鈕會立即讓出畫面顯示操作倒數，系統錯誤也會直接顯示。版本號來自 `version.txt` 的編譯版本，更新韌體時自動跟著變更。

兩顆板載按鈕的操作相同：

| 操作 | 功能 |
| --- | --- |
| 短按 | 切換下一頁 |
| 按住 5 秒後放開 | 進入設定模式 |
| 持續按住至 10 秒 | 清除設定並重新開機 |

GPIO0 同時是 BOOT 按鈕，一般開機時請勿按住。

設定頁的「05 / 螢幕開關時間」可調整每天亮屏時段，預設 **08:00 開啟、19:00 關閉**，依設定時區執行，假日也適用。支援跨午夜的亮屏時段；開、關時間相同代表全天開啟。

關屏時按任一板載按鈕會立即亮起五分鐘，夜間再次按下會重新計時；短按切頁、長按設定及重設功能維持原樣。若已到每日開屏時間，螢幕會持續亮著。關屏僅關閉 LCD 顯示與背光，時鐘、薪資計算及網路功能繼續運作。尚未取得有效時間、設定模式或系統錯誤時保持亮屏。

開關時間儲存在獨立 NVS 欄位，重新開機後套用；舊版升級預設使用 08:00–19:00，保留既有 Wi-Fi、薪資與風格設定。

各頁上方顯示日期 `YYYY/MM/DD` 與時間 `HH:MM:SS`。

| 頁面 | 內容 |
| --- | --- |
| 今天已偷到 | 今日收入、工作狀態、情境動畫與進度條 |
| 還能偷多少 | 今日剩餘金額與排除午休後的剩餘工時 |
| 本月戰績 | 月薪、工作日數、當月總工時及每日／每小時／每分鐘／每秒費率 |
| 系統資訊 | Wi-Fi、IP、訊號、校時、RTC、電池／供電電壓、版本、記憶體與運作時間 |

上班期間顯示金幣堆疊；午休時金額暫停增加，播放漢堡輕晃動畫；下班後顯示小貓休息。休假日顯示 NT$0.00 與海灘休假動畫；跨入假日或假日開機完成對時時，會播一次「放假啦~」。上班前顯示倒數。

## 螢幕風格

按住板載按鈕 5 秒後放開，連上設定 Wi-Fi 並開啟 `http://192.168.4.1`，在「04 / 螢幕風格」選擇後，按「儲存並重新開機」。

| 風格 | 畫面特色 |
| --- | --- |
| 經典原版（預設） | 深藍灰底、薄荷綠金額、金色進度與原本彩色動畫 |
| 琥珀終端 | 暖橘黑底、終端角框、動畫區掃描線與分格進度條 |
| 復古掌機 | 四階綠色、清晰像素字緣、掌機方框與格狀進度條 |

三種風格套用到全部四頁、設定提示與等待畫面，都保留日期、時間、金幣、完整漢堡輕晃與小貓睡覺動畫。風格會記住；從舊版升級時維持經典原版，原有 Wi-Fi 與薪資設定繼續沿用。

下圖由實際韌體繪圖程式產生，左至右為經典原版、琥珀終端、復古掌機。

![三種螢幕風格](docs/themes.png)

## 薪資計算

```text
每日薪資 = 月薪 ÷ 當月工作日數
每日工時 = 上午工作時間 + 下午工作時間
每月工時 = 政府行事曆當月工作日數 × 每日工時
每秒薪資 = 月薪 ÷ 每月工作秒數
今日收入 = 已工作秒數 × 每秒薪資
```

當月工作日依 [Taiwan Calendar](https://allen0099.github.io/taiwan-calendar/) 的逐日資料計算（原始來源：行政院人事行政總處），不再使用舊版每週工作日勾選。週六補班計入工時，平日國定假日不計入；午休不計入工時，下班後收入停在當日薪資上限。

以 2026 年 9 月、09:00–18:00／午休 1 小時為例：20 個工作日、160 小時。月薪 NT$40,000 時，每工作日 NT$2,000、每小時 NT$250。

目前韌體內建 **2026–2027 年**資料，可離線運算；更新工具只收錄 2026 年起的資料。資料隨韌體更新，不會在裝置上自行下載；未收錄年度會顯示「行事曆待更新」並暫停薪資計算。更新方式與資料授權見 [台灣行事曆說明](docs/taiwan-calendar.md)。

收入由目前時間重新計算，重新開機後不會從零累加。未接有效 RTC 時，每次開機都需要網路校時；接上已校時的 DS3231 後可直接離線開機。完成校時後，即使暫時斷線也會繼續計算。

## 自動韌體更新

每次重新開機，裝置連上 Wi-Fi 並完成校時後，會等待 20 秒，向 [GitHub Releases](https://github.com/jonas7414/salary_clock/releases) 檢查一次版本。有新版就下載、驗證並重新啟動；沒有新版或檢查失敗時，繼續使用目前版本。

更新使用 HTTPS、SHA-256 驗證與兩個 4 MiB 的 A/B 韌體分區。新版通過 30 秒健康檢查後才確認生效，啟動失敗時可回滾到前一版。更新不清除 Wi-Fi 與薪資設定。

從沒有 OTA 的舊版升級時，需要先透過 USB 完整燒錄一次，安裝新的 bootloader、分區表與韌體；請勿使用 erase-flash，以保留原設定。

韌體版本集中在 `version.txt`。推送與該版本一致的 `vMAJOR.MINOR.PATCH` tag 後，GitHub Actions 會建置兩種組態，產生韌體與 SHA-256 檔案並發布 Release。

詳細流程、手動 API、分區配置及發布步驟見 [OTA 文件](docs/ota.md)。

## 專案結構

```text
src/                    應用程式初始化
components/
  app_core/             設定模型、薪資計算與共用邏輯
  app_config/           NVS 設定與 JSON 解析
  app_system/           系統事件、佇列與任務狀態
  button/               按鈕操作
  coin_physics/         硬幣物理模擬
  display/              LCD 驅動、畫面與動畫
  ota_manager/          版本檢查、下載、驗證與回滾
  salary/               薪資更新任務
  setup_portal/         網頁設定介面
  time_manager/         網路校時、DS3231 偵測與 UTC 讀寫
  battery_manager/      GPIO4 校正電壓取樣與供電狀態
  wifi_manager/         Wi-Fi 連線與設定模式
tools/                  建置、字型生成與測試工具
tests/                  主機端測試
docs/                   操作文件、預覽與驗證記錄
.github/workflows/      GitHub Actions 發布流程
```

## 開發與測試

完成一次韌體建置後，可使用 C++17 編譯器與 Pillow 執行主機端測試：

```sh
python -m pip install pillow
python tools/test_host.py --cxx clang++
python -m unittest discover -s tests -p test_release_tools.py -v
```

測試涵蓋薪資、日曆、設定、Wi-Fi 策略、RTC 日期轉換與 I²C 故障、動畫、NVS 與 OTA 邏輯，並在 `.artifacts/host` 產生畫面及動畫預覽（RTC：`rtc.png`、`rtc_read.gif`、`rtc_sync.gif`）。實板測試程序與建置記錄見 [驗證文件](docs/validation.md)。

字型子集已包含在原始碼中，平常建置不需要重新產生。修改介面文字或字型時，可執行：

```sh
python tools/generate_font.py /path/to/NotoSansTC.ttf --latin-font /path/to/ChakraPetch-Medium.ttf --display-font /path/to/Orbitron.ttf
```

## 常見問題

| 狀況 | 處理方式 |
| --- | --- |
| 開機後回到設定模式 | 確認 Wi-Fi 名稱、密碼與 2.4 GHz 訊號 |
| Wi-Fi 已連線但仍在等待 | 檢查網際網路、DNS 與 NTP 連線；校時完成後才會計薪 |
| 手機提示沒有網際網路 | 保持連上設定 AP，手動開啟 `http://192.168.4.1` |
| 想更換 Wi-Fi 或工作排程 | 按住按鈕 5 秒後放開，修改設定並重新開機 |
| 想切換螢幕風格 | 進入設定頁的「04 / 螢幕風格」，選擇後儲存並重新開機 |
| 午休或下班後金額不再增加 | 依工作排程暫停計薪，屬正常行為 |
| 每月的每日薪資不同 | 每日薪資依當月實際工作日數計算 |
| LCD 黑屏或方向錯誤 | 確認板型為 1.9 吋 T-Display-S3，並將 USB 接口朝左 |
| 沒有自動更新 | 確認已安裝支援 OTA 的版本、完成校時，且 Release 版本高於本機版本；重新開機再次檢查 |

## 參考與素材授權

- [LILYGO T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3)：硬體與接腳配置。
- [LILYGO ESP-IDF 範例](https://github.com/Xinyuan-LilyGO/LilyGo-Display-IDF/blob/master/main/display_s3.c)：面板初始化參考，見 [MIT 授權](licenses/LILYGO-MIT.txt)。
- [Noto Sans TC](https://github.com/google/fonts/tree/main/ofl/notosanstc)：中文字型，見 [SIL OFL 1.1](licenses/NotoSansTC-OFL.txt)。
- [Orbitron](https://github.com/google/fonts/tree/main/ofl/orbitron)：大型英數字型，見 [SIL OFL 1.1](licenses/Orbitron-OFL.txt)。
- [Chakra Petch](https://github.com/google/fonts/tree/main/ofl/chakrapetch)：小型英數字型，見 [SIL OFL 1.1](licenses/ChakraPetch-OFL.txt)。
