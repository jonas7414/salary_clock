# 開發、測試與發版

## 定位修改位置

| 需求 | 先讀／改哪裡 | 驗證 |
| --- | --- | --- |
| 螢幕文案／布局 | `components/display/ui_renderer.cpp` | 字型生成、三主題預覽、整幀／strip 比對 |
| 新增頁面 | `display_preferences.h`, `ui_renderer.cpp`, `display.cpp`, AP HTML、NVS 遷移 | 頁面排序、舊偏好遷移、導航與繪圖測試 |
| 薪資計算 | `app_core/salary_math.cpp`, `salary/salary.cpp` | `test_core.cpp`, `test_job_salary.cpp` |
| 新增持久設定 | 型別、app_config、config_json、setup_portal 的 GET 與 HTML | `test_nvs.cpp`, `test_json.cpp`, `test_portal.cjs` |
| 按鈕行為 | `app_core/button_logic.cpp`, `button/button.cpp` | `test_button.cpp`，再實板檢查輸入模式／睡眠 |
| 行事曆 | `calendar_manager`, `app_core/taiwan_calendar.cpp` | `test_calendar_download.cpp`, `test_taiwan_calendar.py` |
| OTA | `ota_manager`、`ota_status.h` | `test_ota.cpp`、release guards，實板 A/B 驗證 |

## 建置

從包含 `platformio.ini` 的專案根目錄執行。安裝工具需求參考 `.github/workflows/release.yml`；目前使用 PlatformIO 6.2.0。`pio` 不在 PATH 時可用已安装 PlatformIO 的 Python 執行 `python -m platformio`。

```sh
pio run -e tdisplay_s3 -e tdisplay_s3_no_psram
```

產物在 `.pio/build/<environment>/firmware.bin`。`platformio.ini` 指定本機 core_dir `.tools/platformio`，不要假定工具一定位於全域 `~/.platformio`。生成的 `sdkconfig.<environment>` 可覆蓋 defaults；需要可重現的設定時修改 defaults，並確認兩個環境的最終 config。

`tools/configure_build.py` 檢查版本、Flash 與分區，保持必要的 TLS／rollback 設定，版本異動時使 CMake cache 失效。Windows 的長 linker 命令由 `tools/windows_ldgen.py` 處理。

USB upload 是另一個動作；編譯不代表已燒錄。只有需要上板時再用 `pio run -e <environment> -t upload`；不要將 erase-flash 當作一般升級步驟。

## 主機測試

需 C++17 編譯器、Python 與 Pillow，JSON 測試會使用建置所安裝的 ESP-IDF cJSON。

```sh
python tools/test_host.py --cxx g++
python -m unittest discover -s tests -p test_release_tools.py -v
```

`test_host.py` 執行行事曆資料驗證、Python 行事曆測試及 C++ 薪資／頁面／按鈕／RTC／NVS／JSON／OTA 等測試，並生成 `.artifacts/host` 圖片。可用 `--cxx clang++`，或依工具 help 使用 Zig；Windows MSVC 參數不同，不能直接把 `cl` 傳給此 GCC 參數格式的 runner。

AP 頁面測試需 Node.js、Playwright 與 Chrome，可在本機工具目錄安裝依賴而不改韌體專案：

```sh
npm install --prefix .tools/webtest --no-save playwright
```

PowerShell 執行方式：

```powershell
$env:NODE_PATH = '.tools/webtest/node_modules'
node tests/test_portal.cjs
```

測試預設使用 Chrome channel；`CHROME_PATH` 可指定現有 Chromium 執行檔。測試以攔截 HTTP 的假資料驗證手機／桌面表單、排序、保存與布局，不會連到裝置。

## 字型與預覽

`font_data.h` 是生成的位圖字庫。新增中文畫面文字後須執行：

```sh
python tools/generate_font.py '/path/to/NotoSansTC[wght].ttf' --latin-font /path/to/ChakraPetch-Medium.ttf --display-font '/path/to/Orbitron[wght].ttf'
```

字型來自相應授權的 Noto Sans TC、Chakra Petch、Orbitron；授權在 `licenses/`。本機可能已有 `.artifacts/fonts`，但該目錄不受版本控制，新 checkout 不保證存在。修改名稱等動態文字時，注意廣泛中文字庫只有 12 px，其他尺寸主要依 renderer 字串生成子集。

完整頁面渲染器是 `tools/render_pages.cpp`，直接連結實際 renderer、薪資與日曆邏輯。GCC/Clang 範例（先建立 `.artifacts/host`）：

```sh
g++ -std=c++17 -O2 -pthread -Icomponents/app_core/include -Icomponents/display/include -Icomponents/coin_physics/include tools/render_pages.cpp components/app_core/app_types.cpp components/app_core/salary_math.cpp components/app_core/taiwan_calendar.cpp components/app_core/holiday_countdown.cpp components/coin_physics/coin_physics.cpp components/display/ui_renderer.cpp -o .artifacts/host/render_pages
.artifacts/host/render_pages
python tools/package_page_previews.py
```

最後一步需要 `.artifacts/fonts/NotoSansTC[wght].ttf` 與 Pillow。輸出 `.artifacts/all-pages/index.html`、三種主題總覽、對照總覽、各頁 PNG 與原生 320×170 PNG。HTML 可直接以瀏覽器開啟；預覽使用固定示例日期、月薪與系統資訊，非裝置即時狀態。修改後重新執行，不要交付過期圖片。

## 發版

來源版本由 `version.txt` 唯一定義。先確認實際 remote、branch、最新 tag；沒有 `.git` 的 ZIP 目錄不應直接假定能 push。

1. 更新版本、`docs/releases/vX.Y.Z.md` 與受影響使用說明。
2. 完成兩組態編譯、相關回歸、預覽檢查；記錄未實板驗證的範圍。
3. 以 release tools 驗證映像版本、晶片、TLS／rollback／PSRAM、分區容量並產生 checksum：

```sh
python tools/release_tools.py --tag vX.Y.Z --environment tdisplay_s3 --output release
python tools/release_tools.py --tag vX.Y.Z --environment tdisplay_s3_no_psram --output release
```

4. 在使用者授權的儲存庫提交並推送與版本一致的 tag。GitHub Actions 會建置雙組態，執行完整主機回歸，驗證資產，先建草稿，四檔上傳完成才公開設為 latest。
5. 查驗 workflow 成功、Release 已公開且為預期 tag、四檔齊全、下載內容 SHA 與 descriptor 相符。只有做到這一步才說已發布。

OTA 四個必要資產為 `firmware.bin`、`firmware.sha256`、`firmware-no-psram.bin`、`firmware-no-psram.sha256`。不要覆蓋已公開 tag／映像；修正發布使用新版本。`release/` 本機產物不等於 GitHub Release。

目前已知 OTA 目標設定見 `components/ota_manager/include/ota_config.h`；發版前核實，而不是把文件中的連結當作自動授權。更多流程與實板驗收見 [OTA 文件](ota.md)。
