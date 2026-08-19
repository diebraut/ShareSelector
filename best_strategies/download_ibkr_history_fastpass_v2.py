#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
download_ibkr_history_fastpass.py

Schneller Erstpass für alle noch fehlenden IBKR-Aktien.

Unterschied zu V2:
- nur EIN 5-Jahres-Request pro Aktie
- KEIN 1-Jahres-Fallback im selben Lauf
- kurzer Timeout
- Fehlschläge werden nur protokolliert
- vorhandene CSV-Dateien im Ordner ibkr_history_2019_2023 werden übersprungen

Damit läuft der Erstpass deutlich schneller. Problemfälle können danach
gezielt in einem zweiten Lauf nachgeladen werden.

Start:
    python download_ibkr_history_fastpass.py --port 7496 --resume

Test:
    python download_ibkr_history_fastpass.py --port 7496 --resume --limit 20
"""

from __future__ import annotations

import argparse
import csv
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import pandas as pd

try:
    from ibapi.client import EClient
    from ibapi.contract import Contract
    from ibapi.wrapper import EWrapper
except ImportError:
    print("Installieren mit: python -m pip install pandas ibapi", file=sys.stderr)
    raise SystemExit(1)

DEFAULT_INPUT = "ibkr_isin_resolved.csv"
OUT_DIR = Path("ibkr_history_2019_2023")
STATUS_FILE = Path("ibkr_history_fastpass_status.csv")
V1_STATUS_FILE = Path("ibkr_history_status.csv")
V2_STATUS_FILE = Path("ibkr_history_status_v2.csv")

HOST = "127.0.0.1"
DEFAULT_PORT = 7496
CLIENT_ID = 40

START_DATE = pd.Timestamp("2019-01-01")
END_DATE = pd.Timestamp("2023-12-31")

REQUEST_TIMEOUT = 3.0
PAUSE_BETWEEN_STOCKS = 0.10
SAVE_EVERY = 25


@dataclass
class HistRequest:
    bars: list[Any]
    done: threading.Event
    errors: list[str]


class IBApp(EWrapper, EClient):
    def __init__(self):
        EClient.__init__(self, self)
        self.connected_event = threading.Event()
        self._lock = threading.Lock()
        self._next_req_id = 20000
        self.requests: dict[int, HistRequest] = {}

    def next_req_id(self):
        with self._lock:
            rid = self._next_req_id
            self._next_req_id += 1
            return rid

    def nextValidId(self, orderId: int):
        self.connected_event.set()

    def managedAccounts(self, accountsList: str):
        self.connected_event.set()

    def historicalData(self, reqId, bar):
        r = self.requests.get(reqId)
        if r is not None:
            r.bars.append(bar)

    def historicalDataEnd(self, reqId, start, end):
        r = self.requests.get(reqId)
        if r is not None:
            r.done.set()

    def error(self, reqId, errorCode, errorString, advancedOrderRejectJson=""):
        msg = f"{errorCode}: {errorString}"
        if reqId in self.requests:
            self.requests[reqId].errors.append(msg)
            if errorCode in {162,165,166,200,321,322,354,366,420,10167}:
                self.requests[reqId].done.set()
        if reqId == -1 and errorCode not in {2104,2106,2107,2108,2158}:
            print(f"IBKR: {msg}", flush=True)


def clean(v: Any) -> str:
    if pd.isna(v):
        return ""
    s = str(v).strip()
    return "" if s.lower() == "nan" else s


def make_contract(row: pd.Series) -> Contract:
    c = Contract()
    c.conId = int(float(row["conId"]))
    c.secType = "STK"
    c.exchange = "SMART"

    cur = clean(row.get("Currency", ""))
    sym = clean(row.get("Symbol", ""))
    pe = clean(row.get("PrimaryExchange", ""))

    if cur:
        c.currency = cur
    if sym:
        c.symbol = sym
    if pe:
        c.primaryExchange = pe
    return c


def bars_to_df(bars):
    rows = [{
        "date": b.date,
        "open": b.open,
        "high": b.high,
        "low": b.low,
        "close": b.close,
        "volume": b.volume,
        "barCount": b.barCount,
        "average": b.average,
    } for b in bars]

    df = pd.DataFrame(rows)
    if df.empty:
        return df

    raw = df["date"].astype(str)
    df["date"] = pd.to_datetime(
        raw.str.extract(r"(\d{8})", expand=False),
        format="%Y%m%d",
        errors="coerce"
    )
    df = df.dropna(subset=["date"])
    df = df[(df["date"] >= START_DATE) & (df["date"] <= END_DATE)]
    return df.sort_values("date").drop_duplicates("date")


def request_5y(app: IBApp, contract: Contract):
    rid = app.next_req_id()
    r = HistRequest([], threading.Event(), [])
    app.requests[rid] = r

    app.reqHistoricalData(
        rid,
        contract,
        "20231231 23:59:59",
        "5 Y",
        "1 day",
        "TRADES",
        0,
        1,
        False,
        [],
    )

    finished = r.done.wait(REQUEST_TIMEOUT)

    try:
        if not finished:
            try:
                app.cancelHistoricalData(rid)
            except Exception:
                pass
            r.errors.append("Timeout")

        return bars_to_df(r.bars), r.errors
    finally:
        app.requests.pop(rid, None)


def filename_for(isin, conid):
    isin = "".join(ch for ch in isin if ch.isalnum() or ch in "-_")
    return f"{isin}_{conid}.csv"


def existing_conids():
    out = set()
    if not OUT_DIR.exists():
        return out

    for f in OUT_DIR.glob("*.csv"):
        if "_" not in f.stem:
            continue
        cid = f.stem.rsplit("_", 1)[-1]
        if cid.isdigit():
            out.add(cid)
    return out


def attempted_conids():
    """Alle bereits von V1/V2/Fastpass geprüften conIds, auch NO_DATA."""
    out = set()
    for path in (V1_STATUS_FILE, V2_STATUS_FILE, STATUS_FILE):
        if not path.exists():
            continue
        try:
            df = pd.read_csv(path, dtype=str)
            if "conId" not in df.columns:
                continue
            vals = (
                df["conId"]
                .dropna()
                .astype(str)
                .str.strip()
                .str.replace(".0", "", regex=False)
            )
            out.update(v for v in vals if v.isdigit())
        except Exception as exc:
            print(f"WARNUNG: Statusdatei {path} konnte nicht gelesen werden: {exc}")
    return out


def save_status(rows):
    pd.DataFrame(rows).to_csv(
        STATUS_FILE,
        index=False,
        encoding="utf-8-sig",
        quoting=csv.QUOTE_MINIMAL,
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default=DEFAULT_INPUT)
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--client-id", type=int, default=CLIENT_ID)
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--retry-no-data", action="store_true", help="Bereits geprüfte NO_DATA/FAILED-Werte erneut versuchen")
    args = ap.parse_args()

    inp = Path(args.input)
    src = pd.read_csv(inp, dtype=str)

    valid = src[
        src["conId"].notna()
        & src["conId"].astype(str).str.strip().ne("")
        & src["SecurityType"].fillna("").eq("STK")
    ].copy().drop_duplicates("conId").reset_index(drop=True)

    OUT_DIR.mkdir(exist_ok=True)

    existing = existing_conids() if args.resume else set()
    attempted = attempted_conids() if (args.resume and not args.retry_no_data) else set()
    skip_ids = existing | attempted

    if skip_ids:
        valid = valid[
            ~valid["conId"]
            .astype(str)
            .str.replace(".0", "", regex=False)
            .isin(skip_ids)
        ].copy()

    if args.limit > 0:
        valid = valid.head(args.limit)

    print()
    print("IBKR FASTPASS 2019-2023")
    print("=" * 60)
    print(f"Vorhandene Kursdateien: {len(existing)}")
    print(f"Bereits geprüft gesamt: {len(attempted)}")
    print(f"Übersprungene conIds:   {len(skip_ids)}")
    print(f"Noch wirklich offen:    {len(valid)}")
    print(f"Timeout je Aktie:       {REQUEST_TIMEOUT:.0f} s")
    print()

    app = IBApp()
    app.connect(args.host, args.port, args.client_id)
    t = threading.Thread(target=app.run, daemon=True)
    t.start()

    if not app.connected_event.wait(10):
        app.disconnect()
        raise RuntimeError("Keine API-Verbindung.")

    print("API-Verbindung hergestellt.\n")

    rows = []
    if args.resume and STATUS_FILE.exists():
        try:
            old = pd.read_csv(STATUS_FILE, dtype=str)
            rows = old.fillna("").to_dict("records")
        except Exception:
            pass

    ok = fail = 0
    total = len(valid)

    for i, (_, row) in enumerate(valid.iterrows(), 1):
        isin = clean(row.get("ISIN", ""))
        conid = clean(row.get("conId", "")).replace(".0", "")
        symbol = clean(row.get("Symbol", ""))
        cur = clean(row.get("Currency", ""))

        print(f"[{i:>5}/{total}] {isin} {symbol} {cur} conId={conid}", flush=True)

        df, errs = request_5y(app, make_contract(row))

        if not df.empty:
            out = df.copy()
            out.insert(0, "ISIN", isin)
            out.insert(1, "conId", conid)
            out.insert(2, "Symbol", symbol)
            out.insert(3, "Currency", cur)

            out.to_csv(
                OUT_DIR / filename_for(isin, conid),
                index=False,
                encoding="utf-8-sig",
            )

            first = df["date"].min().strftime("%Y-%m-%d")
            last = df["date"].max().strftime("%Y-%m-%d")
            print(f"   -> OK {len(df)} Bars, {first} bis {last}")
            status = "OK"
            ok += 1
        else:
            first = last = ""
            print("   -> KEINE DATEN / TIMEOUT")
            status = "FAILED"
            fail += 1

        rows.append({
            "ISIN": isin,
            "conId": conid,
            "Symbol": symbol,
            "Currency": cur,
            "Status": status,
            "Rows": len(df),
            "FirstDate": first,
            "LastDate": last,
            "Error": " | ".join(errs[-10:]),
        })

        if i % SAVE_EVERY == 0:
            save_status(rows)
            print(f"   Status gespeichert: OK={ok}, FAILED={fail}\n")

        time.sleep(PAUSE_BETWEEN_STOCKS)

    save_status(rows)
    app.disconnect()

    print()
    print("=" * 60)
    print("FASTPASS FERTIG")
    print(f"OK:      {ok}")
    print(f"FAILED:  {fail}")
    print(f"Status:  {STATUS_FILE.resolve()}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nAbgebrochen. Mit --resume fortsetzen.")
        raise SystemExit(130)
    except Exception as exc:
        print(f"\nFEHLER: {exc}", file=sys.stderr)
        raise SystemExit(1)
