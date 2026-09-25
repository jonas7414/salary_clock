# 台灣政府行事曆

工作日完全採用 [allen0099/taiwan-calendar](https://allen0099.github.io/taiwan-calendar/)
轉換的 [行政院人事行政總處資料](https://data.gov.tw/dataset/14718)。以每日 `isHoliday`
為準：`false` 是上班，`true` 是放假。不能只看星期幾或 `isWeekend`，否則會漏算週六補班。

每日工時仍依使用者設定的上下班、午休時間；每月工時等於工作日數乘以每日工時。
月薪除以整月工作秒數得到每秒速率。國定假日和例假日的今日收入及剩餘工時為零。
舊版 NVS 設定結構維持相容，既有 Wi-Fi、月薪與排程繼續使用；`work_days` 僅保留
作為舊版資料欄位，不參與新版薪資計算。

韌體內建 2026–2027 年，共 24 個完整月份，並自動下載今年、明年的完整年度資料。已驗證的 NVS 快取優先於內建資料，離線時兩者皆可使用，
不會因關閉手機熱點或網站連線失敗而清空資料。快取和內建皆未收錄的年度明確顯示
「行事曆待更新」，時間仍會走，薪資暫停計算，不自行猜測未公布的假日。

## 裝置自動更新

- 開機一分鐘後開始背景檢查；必須已連線且 SNTP 校時成功，才決定今年／明年並驗證 HTTPS 憑證。
- 直接取得上游 `/<年>/all.json`，每天最多嘗試一次（以 UTC 日期為準）。嘗試日期先保存至 NVS，斷電、重啟或深度睡眠也不會反覆下載；失敗則下一個 UTC 日期再試。網路時間修正到另一日期時可重新檢查。
- 每次只使用 1 KB 下載緩衝區，以串流解析 JSON；每年度上限 160 KB、請求期限 60 秒、socket 逾時 5 秒。HTTPS/TLS 本身另需暫存記憶體，OTA 與行事曆下載透過共用鎖輪流執行。
- 驗證年份、十二個月、每天完整且不重複、日期與閏年合法、`isHoliday` 是布林值。拒絕截斷、錯誤 JSON、過度巢狀內容及非 200 回應；不跟隨重新導向。只有完整通過的一年才更新該年，另一個年度失敗不影響已保存資料。
- 使用獨立 `calendar/cache` NVS blob；兩年度資料、格式、檢查日期與 checksum 共 **116 bytes**，不含 NVS 管理開銷。只在資料變更時保存年度內容，保留設定與既有分割區。快取損壞時忽略並使用內建資料。
- NVS 提交與設定／OTA／睡眠互斥；下載不阻塞顯示與薪資 task。成功後直接套用，無須重開機；長按睡眠可中斷下載。
- 設定頁 `/api/status` 可查 `calendar_cached_years`、`calendar_last_attempt_utc_day`、`calendar_updating`、`calendar_last_check_success`。成功旗標表示本次開機最近一次檢查兩年皆成功；明年尚未公布時仍可使用今年的有效資料。

## 更新韌體內建的備用資料

```sh
python tools/update_taiwan_calendar.py --refresh
python tools/update_taiwan_calendar.py --check
python -m unittest discover -s tests -p test_taiwan_calendar.py
```

`--refresh` 下載索引，只抓取 2026 年起的年度 JSON，驗證每個月的每一天完整、沒有重複日期，且
`isHoliday` 為布林值，再生成快照和 C++ bitmask。`--check` 完全離線，驗證兩者一致。
更新後執行主機測試、建置兩種韌體，再循專案既有發布／燒錄流程更新裝置。
此工具只用於維護韌體內建的備用資料；裝置的日常行事曆更新不需要重新發布韌體。

`data/taiwan_calendar.json` 記錄各年度來源時間及原始回應 SHA-256；授權聲明見
[TaiwanCalendar-NOTICE.md](../licenses/TaiwanCalendar-NOTICE.md)。

## 驗證

`tests/test_calendar_download.cpp` 驗證串流解析、逐日完整性、未知欄位、Unicode 跳脫、重複日期、錯誤型別、截斷／傳輸失敗、長度及深度上限、閏年與世紀年、快取 checksum、跨年替換、重開機每日限制，以及多執行緒讀取與更新。可額外傳入 2026／2027 原始年度 JSON，逐月比對內建參考資料。本機已通過 2,398 項檢查（含實際來源資料），測試已整合至發布工作流程。

主機測試逐日比對內建資料的工作／放假狀態、工作日序號、月工時與金額，並涵蓋
2026 年中秋節、教師節、自訂每日工時及未收錄年度。解析器以合成資料驗證週六補班與閏年二月，並確認更新時不會重新收錄舊年度。
另驗證跨入假日、假日完成對時、連續假日，每天只播一次「放假啦~」，三種風格的
整幀／分段畫面一致，以及假日場景不顯示原硬幣。

預覽由真實 C++ renderer 產生在 `.artifacts/host/holiday_start.gif`、`holiday.gif`
及 `transitions.png`；實機流暢度仍需燒錄後確認。
