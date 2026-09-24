import calendar
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from update_taiwan_calendar import MIN_YEAR, calendar_years, make_header, parse_year


def year_fixture(year=2028):
    return {"year": year, "months": [dict(month=month, holidays=[
        dict(date=f"{year}{month:02d}{day:02d}", isHoliday=calendar.weekday(year, month, day) >= 5)
        for day in range(1, calendar.monthrange(year, month)[1] + 1)]) for month in range(1, 13)]}


class TaiwanCalendarTests(unittest.TestCase):
    def test_holiday_and_makeup_override_weekday(self):
        data = year_fixture()
        data["months"][0]["holidays"][2]["isHoliday"] = True  # Monday Jan 3
        data["months"][0]["holidays"][0]["isHoliday"] = False  # Saturday Jan 1
        days = parse_year(data, 2028)
        self.assertEqual(days[0][2], "0")
        self.assertEqual(days[0][0], "1")
        self.assertEqual(len(days[1]), 29)

    def test_incomplete_data_rejected(self):
        for missing in ("month", "day"):
            data = year_fixture()
            if missing == "month":
                data["months"].pop()
            else:
                data["months"][1]["holidays"].pop()
            with self.assertRaises(ValueError):
                parse_year(data, 2028)

    def test_duplicate_or_mismatched_dates_rejected(self):
        for date in ("20280101", "20290102", "20280132", "2028-01-02"):
            data = year_fixture()
            data["months"][0]["holidays"][1]["date"] = date
            with self.assertRaises(ValueError):
                parse_year(data, 2028)

    def test_flags_must_be_boolean(self):
        for value in ("false", 0, 2, None):
            data = year_fixture()
            data["months"][0]["holidays"][0]["isHoliday"] = value
            with self.assertRaises(ValueError):
                parse_year(data, 2028)

    def test_older_years_never_return(self):
        index = {"availableCalendars": [{"year": year} for year in (MIN_YEAR-2, MIN_YEAR-1, MIN_YEAR, MIN_YEAR+1, MIN_YEAR)]}
        self.assertEqual(calendar_years(index), [MIN_YEAR, MIN_YEAR+1])
        with self.assertRaises(ValueError):
            calendar_years({"availableCalendars": [{"year": MIN_YEAR-1}]})
        with self.assertRaises(ValueError):
            make_header({"years": [{"year": MIN_YEAR-1, "workdays": ["0"*31]*12}]})

    def test_snapshot_and_header_match(self):
        data = json.loads((ROOT / "data/taiwan_calendar.json").read_text(encoding="utf-8"))
        self.assertEqual(make_header(data), (ROOT / "components/app_core/taiwan_calendar_data.h").read_text(encoding="utf-8"))
        bad = copy.deepcopy(data)
        bad["years"][0]["workdays"][0] = "1"
        with self.assertRaises(ValueError):
            make_header(bad)


if __name__ == "__main__":
    unittest.main()
