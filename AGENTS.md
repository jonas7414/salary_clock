# 給 Codex 與其他 AI 開發工具

本專案是 LILYGO T-Display-S3 薪水時鐘韌體，使用 PlatformIO、ESP-IDF、C++17 與 FreeRTOS；不是 Node.js 網站。AP 設定頁是嵌入韌體的單一 HTML。

## 先讀什麼

1. [架構與資料流](docs/architecture.md)：模組責任、執行緒、頁面 ID、薪資與儲存模型。
2. [裝置 HTTP 與內部 API](docs/api.md)：實際路由、設定契約、錯誤與 C++ 入口。
3. [開發與驗證指南](docs/development.md)：修改位置、建置、測試、字型、預覽與發版。
4. [README](README.md)：使用方式；特殊功能再讀 [OTA](docs/ota.md)、[行事曆](docs/taiwan-calendar.md)、[電池](docs/battery.md)。

文件描述目前原始碼；如與實作不同，以對應原始碼確認行為並同步修正文檔。版本從 `version.txt` 讀取，不從文件猜測。

## 修改慣例

- 使用繁體中文溝通與撰寫使用者文案。按鈕文案固定為「上方按鈕／下方按鈕」；硬體常數保持原接腳定義，不因文案變更而換腳。
- 先查對應模組，優先在既有純 C++ 邏輯層修改，避免把計算、NVS 或網路 I/O 放進繪圖函式。
- 頁面 ID 是穩定識別碼，不是畫面排序索引。新增頁面要同步更新偏好驗證、AP 排序、NVS 遷移與測試。
- 保留 `AppConfig` 的二進位布局、CRC、NVS namespace 與 OTA 回滾相容性。新增持久設定優先採獨立 key；不能直接改舊 blob 大小再當成同一格式讀取。
- 使用 snapshot/publish API 與既有 mutex、queue、event bits；不要引入跨 task 未同步的共享可變資料。
- 無 PSRAM 環境必須可用；`ui_render()` 須同時支援整幀與 strip 渲染。
- 新增螢幕中文後重新生成 `font_data.h`，不可只改字串而漏字庫。三種主題都要檢查；金額與時間不得超出 320×170 範圍。
- 改動行為時執行相應既有測試；不要把舊報告或主機測試描述成實板測試。
- `.pio/`、`.tools/`、`.artifacts/`、`release/` 是本機生成目錄。不要提交憑證、密碼、工具鏈、整個產物目錄或機器特定路徑。
- 目前工作區可能是 ZIP 解壓目錄，沒有 `.git`。執行 Git 操作前確認真正的 repository root、remote 與差異，不把歷史發版 checkout 當成正在編輯的來源。
- 一般修改不代表發版或燒錄。使用者已明確授權的範圍內繼續作業；遇工具明確阻擋時如實回報，不能改路徑繞過。

## 任務完成時

簡述修改內容、驗證結果、預覽／產物位置，以及是否真的燒錄或公開發布。不要只說「編譯成功」就宣稱 OTA、實板或發布成功。
