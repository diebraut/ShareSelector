#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
analyze_ibkr_history_coverage.py

Analysiert den Ordner ibkr_history_2019_2023 ohne neue Downloads.

Ermittelt pro Kursdatei:
- ISIN
- conId
- Symbol
- FirstDate
- LastDate
- Anzahl Zeilen
- Jahresabdeckung 2019-2023
- Nutzbarkeit für Corona 2020
- Nutzbarkeit für 2022
- nahezu vollständige Abdeckung 2019-2023

Standardordner:
    K:\tmp\ibkr_history_2019_2023

Aufruf:
    python analyze_ibkr_history_coverage.py

Optional:
    python analyze_ibkr_history_coverage.py --folder K:\tmp\ibkr_history_2019_2023

Ergebnis:
    ibkr_history_coverage.csv
"""

from __future__ import annotations

import argparse
from pathlib import Path
import pandas as pd

DEFAULT_FOLDER = r"K:\tmp\ibkr_history_2019_2023"
OUTPUT = "ibkr_history_coverage.csv"

CORONA_REQUIRED_START = pd.Timestamp("2019-12-01")
CORONA_REQUIRED_END = pd.Timestamp("2021-03-23")

Y2022_REQUIRED_START = pd.Timestamp("2021-10-01")
Y2022_REQUIRED_END = pd.Timestamp("2023-10-12")


def inspect_file(path: Path) -> dict:
    out = {
        "file": path.name,
        "ISIN": "",
        "conId": "",
        "Symbol": "",
        "Rows": 0,
        "FirstDate": "",
        "LastDate": "",
        "Has2019": False,
        "Has2020": False,
        "Has2021": False,
        "Has2022": False,
        "Has2023": False,
        "CoronaUsable": False,
        "Y2022Usable": False,
        "Full2019_2023": False,
        "Error": "",
    }

    try:
        df = pd.read_csv(path)
    except Exception as exc:
        out["Error"] = f"read error: {exc}"
        return out

    if df.empty or "date" not in df.columns:
        out["Error"] = "empty or no date column"
        return out

    if "ISIN" in df.columns:
        out["ISIN"] = str(df["ISIN"].iloc[0])
    if "conId" in df.columns:
        out["conId"] = str(df["conId"].iloc[0]).replace(".0", "")
    if "Symbol" in df.columns:
        out["Symbol"] = str(df["Symbol"].iloc[0])

    dates = pd.to_datetime(df["date"], errors="coerce").dropna().sort_values()
    if dates.empty:
        out["Error"] = "no valid dates"
        return out

    out["Rows"] = int(len(dates))
    out["FirstDate"] = dates.iloc[0].strftime("%Y-%m-%d")
    out["LastDate"] = dates.iloc[-1].strftime("%Y-%m-%d")

    years = set(dates.dt.year.unique().tolist())
    for year in [2019, 2020, 2021, 2022, 2023]:
        out[f"Has{year}"] = year in years

    first = dates.iloc[0]
    last = dates.iloc[-1]

    out["CoronaUsable"] = (
        first <= CORONA_REQUIRED_START
        and last >= CORONA_REQUIRED_END
        and 2020 in years
    )

    out["Y2022Usable"] = (
        first <= Y2022_REQUIRED_START
        and last >= Y2022_REQUIRED_END
        and 2022 in years
    )

    out["Full2019_2023"] = (
        first <= pd.Timestamp("2019-01-10")
        and last >= pd.Timestamp("2023-12-20")
        and all(y in years for y in [2019, 2020, 2021, 2022, 2023])
    )

    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--folder", default=DEFAULT_FOLDER)
    ap.add_argument("--output", default=OUTPUT)
    args = ap.parse_args()

    folder = Path(args.folder)
    if not folder.exists():
        raise FileNotFoundError(f"Ordner nicht gefunden: {folder}")

    files = sorted(folder.glob("*.csv"))

    print()
    print("IBKR Kursdaten-Abdeckung")
    print("=" * 60)
    print(f"Ordner:      {folder}")
    print(f"CSV-Dateien: {len(files)}")
    print()

    rows = []
    for i, f in enumerate(files, 1):
        rows.append(inspect_file(f))
        if i % 500 == 0 or i == len(files):
            print(f"{i:>5}/{len(files)} geprüft", flush=True)

    df = pd.DataFrame(rows)
    df.to_csv(args.output, index=False, encoding="utf-8-sig")

    print()
    print("=" * 60)
    print("ERGEBNIS")
    print(f"Gesamt Kursdateien:        {len(df)}")
    print(f"Mit Daten in 2019:         {int(df['Has2019'].sum())}")
    print(f"Mit Daten in 2020:         {int(df['Has2020'].sum())}")
    print(f"Mit Daten in 2021:         {int(df['Has2021'].sum())}")
    print(f"Mit Daten in 2022:         {int(df['Has2022'].sum())}")
    print(f"Mit Daten in 2023:         {int(df['Has2023'].sum())}")
    print()
    print(f"Für Corona 2020 nutzbar:   {int(df['CoronaUsable'].sum())}")
    print(f"Für 2022 nutzbar:          {int(df['Y2022Usable'].sum())}")
    print(f"Komplett 2019-2023:        {int(df['Full2019_2023'].sum())}")
    print()

    first_dates = pd.to_datetime(df["FirstDate"], errors="coerce")
    print("Startdatum-Verteilung:")
    for cutoff in ["2019-01-01", "2020-01-01", "2021-01-01", "2022-01-01", "2023-01-01"]:
        c = pd.Timestamp(cutoff)
        n = int((first_dates <= c).sum())
        print(f"  spätestens {cutoff}: {n}")

    print()
    print(f"Ausgabedatei: {Path(args.output).resolve()}")


if __name__ == "__main__":
    main()
