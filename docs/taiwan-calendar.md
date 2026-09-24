# 台灣政府行事曆

工作日完全採用 [allen0099/taiwan-calendar](https://allen0099.github.io/taiwan-calendar/)
轉換的 [行政院人事行政總處資料](https://data.gov.tw/dataset/14718)。以每日 `isHoliday`
為準：`false` 是上班，`true` 是放假。不能只看星期幾或 `isWeekend`，否則會漏算週六補班。

每日工時仍依使用者設定的上下班、午休時間；每月工時等於工作日數乘以每日工時。
月薪除以整月工作秒數得到每秒速率。國定假日和例假日的今日收入及剩餘工時為零。
舊版 NVS 設定結構維持相容，既有 Wi-Fi、月薪與排程繼續使用；`work_days` 僅保留
作為舊版資料欄位，不參與新版薪資計算。

目前收錄 2026–2027 年，共 24 個完整月份；不保留 2026 年以前的行事曆資料。資料內建於 flash，對時後不需要網路，
不會因關閉手機熱點或網站連線失敗而變更計算結果。尚未收錄的年度明確顯示
「行事曆待更新」，時間仍會走，薪資暫停計算，不自行猜測未公布的假日。

## 更新資料

```sh
python tools/update_taiwan_calendar.py --refresh
python tools/update_taiwan_calendar.py --check
python -m unittest discover -s tests -p test_taiwan_calendar.py
```

`--refresh` 下載索引，只抓取 2026 年起的年度 JSON，驗證每個月的每一天完整、沒有重複日期，且
`isHoliday` 為布林值，再生成快照和 C++ bitmask。`--check` 完全離線，驗證兩者一致。
更新後執行主機測試、建置兩種韌體，再循專案既有發布／燒錄流程更新裝置。
裝置不會自行下載行事曆；上游新增年度或修訂假日後，需重新生成資料並更新韌體。

`data/taiwan_calendar.json` 記錄各年度來源時間及原始回應 SHA-256；授權聲明見
[TaiwanCalendar-NOTICE.md](../licenses/TaiwanCalendar-NOTICE.md)。

## 驗證

主機測試逐日比對內建資料的工作／放假狀態、工作日序號、月工時與金額，並涵蓋
2026 年中秋節、教師節、自訂每日工時及未收錄年度。解析器以合成資料驗證週六補班與閏年二月，並確認更新時不會重新收錄舊年度。
另驗證跨入假日、假日完成對時、連續假日，每天只播一次「放假啦~」，三種風格的
整幀／分段畫面一致，以及假日場景不顯示原硬幣。

預覽由真實 C++ renderer 產生在 `.artifacts/host/holiday_start.gif`、`holiday.gif`
及 `transitions.png`；實機流暢度仍需燒錄後確認。
