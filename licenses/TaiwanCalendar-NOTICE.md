# Taiwan government calendar data

資料來源：行政院人事行政總處「中華民國政府行政機關辦公日曆表」。

- 原始資料集：https://data.gov.tw/dataset/14718
- JSON 轉換來源：https://allen0099.github.io/taiwan-calendar/
- 授權：政府資料開放授權條款第 1 版（Open Government Data License 1.0）
- 授權條款：https://data.gov.tw/license

`data/taiwan_calendar.json` 保留各年度來源的更新時間與 SHA-256，將逐日
`isHoliday` 轉換為工作日字串（1 為上班、0 為放假），供韌體離線使用。
`components/app_core/taiwan_calendar_data.h` 是此快照的 bitmask 格式。
本專案未使用來源專案的程式碼。
