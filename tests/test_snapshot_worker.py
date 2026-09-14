import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import run_ibkr_quote_job as worker


class SnapshotWorkerTests(unittest.TestCase):
    def save(self, **data):
        with patch.object(worker, "save_bars", return_value=1) as bars, patch.object(worker, "run_psql") as sql:
            self.assertEqual(worker.save_snapshot_quote(None, "TEST", data), 1)
            return bars.call_args.args[2][0], sql.call_args.args[1]

    def test_snapshot_pair_and_reference_date(self):
        bar, sql = self.save(selected=13.525, last=13.525, close=14.585,
                             lastDate="2026-09-11", closeDate="2026-09-10", timeZone="MET")
        self.assertEqual(bar["close"], 13.525)
        self.assertIn('"IBKRSnapshotLast" = 13.525', sql)
        self.assertIn('"IBKRSnapshotClose" = 14.585', sql)
        self.assertIn('"IBKRFinalCloseDate" = \'2026-09-10\'::date', sql)
        self.assertIn('"IBKRCloseSource" IS DISTINCT FROM \'snapshot\'', sql)

    def test_unknown_dates_do_not_overwrite_history(self):
        _, sql = self.save(selected=13.525, last=13.525, close=14.585)
        self.assertNotIn('INSERT INTO "Quotes"', sql)
        self.assertNotIn('"IBKRFinalCloseDate"', sql)
        self.assertIn('"IBKRSnapshotClose" = 14.585', sql)

    def test_missing_last_does_not_create_fake_percentage(self):
        _, sql = self.save(selected=14.585, close=14.585)
        self.assertIn('"IBKRSnapshotLast" = NULL', sql)
        self.assertNotIn('INSERT INTO "Quotes"', sql)

    def test_same_or_later_reference_date_is_rejected(self):
        for reference in ("2026-09-11", "2026-09-12"):
            _, sql = self.save(selected=13.525, last=13.525, close=14.585,
                               lastDate="2026-09-11", closeDate=reference)
            self.assertNotIn('INSERT INTO "Quotes"', sql)

    def test_usd_reference_is_separate_from_eur_quote(self):
        bar, sql = self.save(selected=54.02, close=54.02,
                             changeReference={"currency": "USD", "last": 63.0, "close": 60.0, "conId": 123})
        self.assertEqual(bar["close"], 54.02)
        self.assertIn('"IBKRSnapshotLast" = NULL', sql)
        self.assertIn('"IBKRChangeReference" =', sql)
        self.assertIn('"currency": "USD"', sql)
        self.assertNotIn('INSERT INTO "Quotes"', sql)

    def test_native_last_clears_usd_reference(self):
        _, sql = self.save(selected=55, last=55, close=54,
                           changeReference={"currency": "USD", "last": 63, "close": 60})
        self.assertIn('"IBKRChangeReference" = NULL::jsonb', sql)

    def test_historical_reference_keeps_source_and_dates_without_eur_history_writes(self):
        bar, sql = self.save(selected=54.02, close=54.02,
            changeReference={"currency": "USD", "last": 65.94, "close": 65.18,
                             "source": "historical", "lastDate": "2026-09-11", "closeDate": "2026-09-10"})
        self.assertEqual(bar["close"], 54.02)
        self.assertIn('"source": "historical"', sql)
        self.assertIn('"lastDate": "2026-09-11"', sql)
        self.assertNotIn('INSERT INTO "Quotes"', sql)

    def test_incomplete_reference_is_not_saved(self):
        _, sql = self.save(selected=54.02, close=54.02,
                           changeReference={"currency": "USD", "close": 60})
        self.assertIn('"IBKRChangeReference" = NULL::jsonb', sql)

    def test_fwb_history_uses_actual_date_and_not_snapshot_confirmation(self):
        bar, sql = self.save(selected=45.55,
            nativeHistory=[{"date": "2026-09-11", "close": 45.55}, {"date": "2026-09-10", "close": 44.46}],
            changeReference={"currency": "EUR", "last": 45.55, "close": 44.46, "source": "historical",
                             "exchange": "FWB2", "lastDate": "2026-09-11", "closeDate": "2026-09-10"})
        self.assertEqual(bar["date"], "2026-09-11")
        self.assertIn('"currency": "EUR"', sql)
        self.assertIn('"exchange": "FWB2"', sql)
        self.assertNotIn('"IBKRFinalCloseDate"', sql)
        self.assertIn('"IBKRSnapshotLast" = NULL', sql)

    def test_preferred_exchange_survives_snapshot_routing_fallback(self):
        request = worker.StockRequest(1, "NTO.XFRA", 240609979, "NTO", "EUR", "JP3756600007",
            "GETTEX2", "VSE", "SMART,GETTEX2,FWB2", 5, preferred_quote_exchange="GETTEX2")
        routed = worker.smart_snapshot_request(request)
        args = worker.helper_contract_args(routed)
        self.assertEqual(args[args.index("--preferred-quote-exchange") + 1], "GETTEX2")

    def test_jpy_reference_is_saved_separately_from_eur(self):
        bar, sql = self.save(selected=45.97, close=45.97,
            changeReference={"currency": "JPY", "last": 8000, "close": 7900,
                             "exchange": "TSEJ", "source": "historical"})
        self.assertEqual(bar["close"], 45.97)
        self.assertIn('"currency": "JPY"', sql)
        self.assertNotIn('INSERT INTO "Quotes"', sql)

    def test_reference_without_eur_price_never_creates_zero_quote(self):
        with patch.object(worker, "save_bars") as bars, patch.object(worker, "run_psql") as sql:
            worker.save_snapshot_quote(None, "TEST", {"changeReference": {
                "currency": "JPY", "last": 8000, "close": 7900, "exchange": "TSEJ"}})
            bars.assert_not_called()
            self.assertIn('"currency": "JPY"', sql.call_args.args[1])


if __name__ == "__main__":
    unittest.main()
