#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
resolve_ibkr_isins.py

Liest eine CSV mit ISINs ein, verbindet sich mit laufender
Interactive Brokers TWS oder IB Gateway und ermittelt:

- ISIN
- conId
- Symbol
- LocalSymbol
- SecurityType
- PrimaryExchange
- Exchange
- Currency
- LongName
- frühestes verfügbares historisches Datum (reqHeadTimestamp)

Das Skript speichert laufend Zwischenergebnisse, damit bei einem Abbruch
nicht alles verloren geht.

Installation:
    python -m pip install pandas ibapi

TWS-API aktivieren:
    TWS: Global Configuration -> API -> Settings
         "Enable ActiveX and Socket Clients" aktivieren
         Socket-Port prüfen

Standard-Ports:
    TWS Live:          7496
    TWS Paper:         7497
    IB Gateway Live:   4001
    IB Gateway Paper:  4002

Aufruf:
    python resolve_ibkr_isins.py

Optional:
    python resolve_ibkr_isins.py --input ibkr_stocks_with_quotes_isin.csv
    python resolve_ibkr_isins.py --port 7496
    python resolve_ibkr_isins.py --resume

Ergebnis:
    ibkr_isin_resolved.csv
    ibkr_isin_errors.csv
"""

from __future__ import annotations

import argparse
import csv
import os
import queue
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

import pandas as pd

try:
    from ibapi.client import EClient
    from ibapi.contract import Contract
    from ibapi.wrapper import EWrapper
except ImportError:
    print(
        "FEHLER: Das Paket 'ibapi' fehlt.\n\n"
        "Installiere es mit:\n"
        "  python -m pip install ibapi pandas\n",
        file=sys.stderr,
    )
    raise SystemExit(1)


DEFAULT_INPUT = "ibkr_stocks_with_quotes_isin.csv"
DEFAULT_OUTPUT = "ibkr_isin_resolved.csv"
DEFAULT_ERRORS = "ibkr_isin_errors.csv"

HOST = "127.0.0.1"
DEFAULT_PORT = 7497
CLIENT_ID = 37

REQUEST_TIMEOUT = 20.0
HEAD_TIMESTAMP_TIMEOUT = 20.0
PAUSE_BETWEEN_ISINS = 0.15
SAVE_EVERY = 25


@dataclass
class ContractResult:
    contract_details: list[Any]
    done: threading.Event
    errors: list[str]


@dataclass
class HeadResult:
    timestamp: Optional[str]
    done: threading.Event
    errors: list[str]


class IBApp(EWrapper, EClient):
    def __init__(self):
        EClient.__init__(self, self)
        self._lock = threading.Lock()
        self._next_req_id = 1000

        self.contract_requests: dict[int, ContractResult] = {}
        self.head_requests: dict[int, HeadResult] = {}

        self.connected_event = threading.Event()

    def next_req_id(self) -> int:
        with self._lock:
            rid = self._next_req_id
            self._next_req_id += 1
            return rid

    def nextValidId(self, orderId: int):
        self.connected_event.set()

    def managedAccounts(self, accountsList: str):
        self.connected_event.set()

    def contractDetails(self, reqId, contractDetails):
        result = self.contract_requests.get(reqId)
        if result is not None:
            result.contract_details.append(contractDetails)

    def contractDetailsEnd(self, reqId):
        result = self.contract_requests.get(reqId)
        if result is not None:
            result.done.set()

    def headTimestamp(self, reqId: int, headTimestamp: str):
        result = self.head_requests.get(reqId)
        if result is not None:
            result.timestamp = headTimestamp
            result.done.set()

    def error(self, reqId, errorCode, errorString, advancedOrderRejectJson=""):
        # IBKR sendet einige informative Meldungen mit reqId -1.
        # Diese sollen den Ablauf nicht abbrechen.
        msg = f"{errorCode}: {errorString}"

        if reqId in self.contract_requests:
            self.contract_requests[reqId].errors.append(msg)
            # Typische "keine Definition gefunden"-Fehler beenden Anfrage logisch.
            if errorCode in {200, 321, 322}:
                self.contract_requests[reqId].done.set()

        if reqId in self.head_requests:
            self.head_requests[reqId].errors.append(msg)
            # Bei Fehlern zu historischen Daten nicht ewig warten.
            if errorCode in {
                162, 165, 166, 200, 321, 322, 354, 366, 10167
            }:
                self.head_requests[reqId].done.set()

        if reqId == -1 and errorCode not in {2104, 2106, 2107, 2108, 2158}:
            print(f"IBKR: {msg}", flush=True)


def find_isin_column(df: pd.DataFrame) -> str:
    # Erst exakte Namen
    for c in df.columns:
        if str(c).strip().lower() == "isin":
            return c

    # Dann ähnlich benannte Spalten
    for c in df.columns:
        if "isin" in str(c).strip().lower():
            return c

    # Fallback: Spalte anhand des Inhalts erkennen
    for c in df.columns:
        sample = (
            df[c]
            .dropna()
            .astype(str)
            .str.strip()
            .head(100)
        )
        if len(sample) == 0:
            continue
        hit_rate = sample.str.match(r"^[A-Z]{2}[A-Z0-9]{9}[0-9]$").mean()
        if hit_rate >= 0.8:
            return c

    raise RuntimeError(
        "Keine ISIN-Spalte erkannt. Vorhandene Spalten: "
        + ", ".join(map(str, df.columns))
    )


def normalize_isin(value: Any) -> str:
    return str(value).strip().upper()


def make_isin_contract(isin: str) -> Contract:
    c = Contract()
    c.secType = "STK"
    c.exchange = "SMART"
    c.secIdType = "ISIN"
    c.secId = isin
    return c


def choose_best_contract(details: list[Any]) -> Optional[Any]:
    if not details:
        return None

    # Bevorzugt Stammaktien mit SMART / sinnvoller Primärbörse.
    scored = []
    for cd in details:
        c = cd.contract
        score = 0

        if c.secType == "STK":
            score += 100
        if c.exchange == "SMART":
            score += 20
        if getattr(c, "primaryExchange", ""):
            score += 10
        if getattr(c, "currency", ""):
            score += 5

        scored.append((score, cd))

    scored.sort(key=lambda x: x[0], reverse=True)
    return scored[0][1]


def request_contract_details(
    app: IBApp, isin: str
) -> tuple[Optional[Any], list[Any], list[str]]:
    req_id = app.next_req_id()
    result = ContractResult([], threading.Event(), [])
    app.contract_requests[req_id] = result

    contract = make_isin_contract(isin)
    app.reqContractDetails(req_id, contract)

    finished = result.done.wait(REQUEST_TIMEOUT)

    try:
        if not finished:
            try:
                app.cancelContractDetails(req_id)
            except Exception:
                pass

        best = choose_best_contract(result.contract_details)
        return best, result.contract_details, result.errors
    finally:
        app.contract_requests.pop(req_id, None)


def make_head_contract(cd: Any) -> Contract:
    # Für Folgeabfragen empfiehlt IBKR die eindeutige conId.
    src = cd.contract
    c = Contract()
    c.conId = src.conId
    c.secType = src.secType or "STK"
    c.exchange = "SMART"
    c.currency = src.currency
    return c


def request_head_timestamp(
    app: IBApp, cd: Any
) -> tuple[Optional[str], list[str]]:
    req_id = app.next_req_id()
    result = HeadResult(None, threading.Event(), [])
    app.head_requests[req_id] = result

    contract = make_head_contract(cd)

    # whatToShow="TRADES", useRTH=0, formatDate=1
    app.reqHeadTimeStamp(
        req_id,
        contract,
        "TRADES",
        0,
        1,
    )

    finished = result.done.wait(HEAD_TIMESTAMP_TIMEOUT)

    try:
        try:
            app.cancelHeadTimeStamp(req_id)
        except Exception:
            pass

        if not finished:
            result.errors.append("Timeout bei reqHeadTimeStamp")

        return result.timestamp, result.errors
    finally:
        app.head_requests.pop(req_id, None)


def parse_head_timestamp(ts: Optional[str]) -> Optional[str]:
    if not ts:
        return None

    # Typisches Format: "19801212 09:30:00"
    text = str(ts).strip()

    candidates = [
        "%Y%m%d %H:%M:%S",
        "%Y%m%d-%H:%M:%S",
        "%Y%m%d",
    ]

    for fmt in candidates:
        try:
            dt = pd.to_datetime(text, format=fmt)
            return dt.strftime("%Y-%m-%d")
        except Exception:
            pass

    # Letzter Versuch tolerant
    try:
        dt = pd.to_datetime(text, errors="raise")
        return dt.strftime("%Y-%m-%d")
    except Exception:
        return text


def save_rows(rows: list[dict[str, Any]], output_path: Path):
    pd.DataFrame(rows).to_csv(
        output_path,
        index=False,
        encoding="utf-8-sig",
        quoting=csv.QUOTE_MINIMAL,
    )


def save_errors(rows: list[dict[str, Any]], error_path: Path):
    pd.DataFrame(rows).to_csv(
        error_path,
        index=False,
        encoding="utf-8-sig",
        quoting=csv.QUOTE_MINIMAL,
    )


def load_existing(output_path: Path) -> tuple[list[dict[str, Any]], set[str]]:
    if not output_path.exists():
        return [], set()

    df = pd.read_csv(output_path, dtype=str)
    rows = df.fillna("").to_dict("records")

    done = set()
    if "ISIN" in df.columns:
        done = {
            normalize_isin(x)
            for x in df["ISIN"].dropna().astype(str)
            if str(x).strip()
        }

    return rows, done


def main():
    parser = argparse.ArgumentParser(
        description="IBKR ISIN -> Contract + frühestes historisches Datum"
    )
    parser.add_argument(
        "--input",
        default=DEFAULT_INPUT,
        help=f"Eingabe-CSV (Standard: {DEFAULT_INPUT})",
    )
    parser.add_argument(
        "--output",
        default=DEFAULT_OUTPUT,
        help=f"Ausgabe-CSV (Standard: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--errors",
        default=DEFAULT_ERRORS,
        help=f"Fehler-CSV (Standard: {DEFAULT_ERRORS})",
    )
    parser.add_argument(
        "--host",
        default=HOST,
        help=f"TWS/IB-Gateway Host (Standard: {HOST})",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help="Socket-Port, z.B. 7496 live / 7497 paper",
    )
    parser.add_argument(
        "--client-id",
        type=int,
        default=CLIENT_ID,
        help=f"API Client ID (Standard: {CLIENT_ID})",
    )
    parser.add_argument(
        "--resume",
        action="store_true",
        help="Vorhandene Ergebnisdatei fortsetzen",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="Nur die ersten N neuen ISINs testen (0 = alle)",
    )

    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    error_path = Path(args.errors)

    if not input_path.exists():
        raise FileNotFoundError(
            f"Eingabedatei nicht gefunden: {input_path.resolve()}"
        )

    df = pd.read_csv(input_path, dtype=str)
    isin_col = find_isin_column(df)

    isins = [
        normalize_isin(x)
        for x in df[isin_col].dropna().tolist()
        if str(x).strip()
    ]

    # Duplikate entfernen, Reihenfolge erhalten
    isins = list(dict.fromkeys(isins))

    rows: list[dict[str, Any]] = []
    done: set[str] = set()
    error_rows: list[dict[str, Any]] = []

    if args.resume:
        rows, done = load_existing(output_path)
        print(
            f"Fortsetzen: {len(done)} ISINs bereits verarbeitet.",
            flush=True,
        )

    pending = [x for x in isins if x not in done]

    if args.limit > 0:
        pending = pending[: args.limit]

    print()
    print("IBKR ISIN-Auflösung")
    print("=" * 60)
    print(f"Eingabe:        {input_path.resolve()}")
    print(f"ISIN-Spalte:    {isin_col}")
    print(f"ISINs gesamt:   {len(isins)}")
    print(f"Noch offen:     {len(pending)}")
    print(f"Host:           {args.host}")
    print(f"Port:           {args.port}")
    print(f"Client ID:      {args.client_id}")
    print()

    app = IBApp()

    try:
        app.connect(args.host, args.port, args.client_id)
    except Exception as exc:
        raise RuntimeError(
            f"Verbindung zu TWS/IB Gateway fehlgeschlagen: {exc}"
        )

    thread = threading.Thread(target=app.run, daemon=True)
    thread.start()

    if not app.connected_event.wait(10):
        app.disconnect()
        raise RuntimeError(
            "Keine vollständige API-Verbindung zu TWS/IB Gateway.\n"
            "Prüfe in TWS: Global Configuration -> API -> Settings -> "
            "'Enable ActiveX and Socket Clients' sowie den Socket-Port."
        )

    print("API-Verbindung hergestellt.\n", flush=True)

    total = len(pending)

    for idx, isin in enumerate(pending, start=1):
        print(
            f"[{idx:>5}/{total}] {isin}",
            end=" ",
            flush=True,
        )

        best, all_details, contract_errors = request_contract_details(app, isin)

        if best is None:
            err_text = " | ".join(contract_errors) or "Kein Contract gefunden"
            print(f"-> NICHT GEFUNDEN ({err_text})")

            row = {
                "ISIN": isin,
                "Status": "NOT_FOUND",
                "conId": "",
                "Symbol": "",
                "LocalSymbol": "",
                "SecurityType": "",
                "PrimaryExchange": "",
                "Exchange": "",
                "Currency": "",
                "LongName": "",
                "FirstHistoricalDate": "",
                "ContractMatches": len(all_details),
                "Error": err_text,
            }
            rows.append(row)
            error_rows.append({
                "ISIN": isin,
                "Stage": "ContractDetails",
                "Error": err_text,
            })
        else:
            c = best.contract

            head_ts, head_errors = request_head_timestamp(app, best)
            first_date = parse_head_timestamp(head_ts)

            status = "OK" if first_date else "NO_HEAD_TIMESTAMP"
            err_text = " | ".join(head_errors)

            print(
                f"-> {getattr(c, 'symbol', '')} "
                f"{getattr(c, 'currency', '')} "
                f"conId={getattr(c, 'conId', '')} "
                f"ab {first_date or '?'}"
            )

            row = {
                "ISIN": isin,
                "Status": status,
                "conId": getattr(c, "conId", ""),
                "Symbol": getattr(c, "symbol", ""),
                "LocalSymbol": getattr(c, "localSymbol", ""),
                "SecurityType": getattr(c, "secType", ""),
                "PrimaryExchange": getattr(c, "primaryExchange", ""),
                "Exchange": getattr(c, "exchange", ""),
                "Currency": getattr(c, "currency", ""),
                "LongName": getattr(best, "longName", ""),
                "FirstHistoricalDate": first_date or "",
                "ContractMatches": len(all_details),
                "Error": err_text,
            }
            rows.append(row)

            if not first_date:
                error_rows.append({
                    "ISIN": isin,
                    "Stage": "HeadTimestamp",
                    "Error": err_text or "Kein HeadTimestamp erhalten",
                })

        if idx % SAVE_EVERY == 0:
            save_rows(rows, output_path)
            save_errors(error_rows, error_path)
            print(
                f"    Zwischengespeichert: {len(rows)} Ergebnisse",
                flush=True,
            )

        time.sleep(PAUSE_BETWEEN_ISINS)

    save_rows(rows, output_path)
    save_errors(error_rows, error_path)

    app.disconnect()

    result_df = pd.DataFrame(rows)

    ok_count = 0
    hist_count = 0
    if not result_df.empty:
        ok_count = int(result_df["conId"].astype(str).str.len().gt(0).sum())
        hist_count = int(
            result_df["FirstHistoricalDate"]
            .astype(str)
            .str.len()
            .gt(0)
            .sum()
        )

    print()
    print("=" * 60)
    print("FERTIG")
    print(f"Ergebnisse:            {output_path.resolve()}")
    print(f"Fehlerliste:           {error_path.resolve()}")
    print(f"Verarbeitete ISINs:    {len(rows)}")
    print(f"Contracts gefunden:    {ok_count}")
    print(f"Mit historischem Start:{hist_count}")
    print()
    print(
        "Für einen kurzen Funktionstest kannst du zuerst ausführen:\n"
        "  python resolve_ibkr_isins.py --limit 20\n\n"
        "Wenn das funktioniert, anschließend alle:\n"
        "  python resolve_ibkr_isins.py --resume"
    )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nAbgebrochen. Bereits gespeicherte Ergebnisse bleiben erhalten.")
        raise SystemExit(130)
    except Exception as exc:
        print(f"\nFEHLER: {exc}", file=sys.stderr)
        raise SystemExit(1)
