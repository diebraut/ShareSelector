#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
backtest_momentum_rotation_v4.py

V4 des Momentum-Backtests mit:
- 100 festen Slots
- jeder Slot ist entweder 1 Aktie oder Cash
- Signal an Tag T, Ausführung an T+1
- tägliche Neubewertung
- dynamisches Universum
- Liquiditätsfilter über 20 Handelstage:
    durchschnittlicher Tagesumsatz = close * volume
- Mindestkurs als zusätzlicher Daten-/Pennystock-Schutz
- verdächtige Kurssequenzen werden wie in V3 gefiltert
- ausführliches Trade-Log

Standardregeln im Single-Test:
    Momentum: 20 Tage
    Sell: unter -10 %
    Buy: über +5 %
    Mindest-Tagesumsatz (20T): 1.000.000
    Mindestkurs: 0,50

WICHTIG:
Der "Tagesumsatz" ist in der jeweiligen Handelswährung des gelieferten Listings.
Für das reine Filtern auf Liquidität ist das ausreichend, aber nicht perfekt
zwischen verschiedenen Währungen vergleichbar.

Ordner:
    K:\tmp\ibkr_history_2019_2023

Start:
    python backtest_momentum_rotation_v4.py --single

Optional:
    python backtest_momentum_rotation_v4.py --single --min-turnover 500000
    python backtest_momentum_rotation_v4.py --single --min-price 1.0

Ausgaben:
    backtest_v4_results/
      summary.csv
      crash_metrics.csv
      daily_<variant>.csv
      trade_log_<variant>.csv
      suspicious_moves.csv
      excluded_stocks.csv
      liquidity_stats.csv
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
import pandas as pd

DEFAULT_FOLDER = r"K:\tmp\ibkr_history_2019_2023"
OUT_DIR = Path("backtest_v4_results")

MAX_SLOTS = 100

MOM_WINDOWS = [10, 20, 40]
SELL_THRESHOLDS = [-0.05, -0.10, -0.15]
BUY_THRESHOLDS = [0.00, 0.02, 0.05]

START = pd.Timestamp("2019-01-02")
END = pd.Timestamp("2023-12-29")

CORONA_PEAK = pd.Timestamp("2020-02-19")
CORONA_TROUGH = pd.Timestamp("2020-03-23")
BEAR22_PEAK = pd.Timestamp("2022-01-03")
BEAR22_TROUGH = pd.Timestamp("2022-10-12")

LIQ_WINDOW = 20

# Datenqualitätsfilter aus V3
EXTREME_UP = 5.0       # +500%
EXTREME_DOWN = -0.85   # -85%
REVERSAL_WINDOW = 3
BLOCK_WINDOW = 10
MAX_PROBLEM_EVENTS = 3


def load_market_data(folder: Path):
    files = sorted(folder.glob("*.csv"))
    if not files:
        raise RuntimeError(f"Keine CSV-Dateien in {folder}")

    close_series = {}
    volume_series = {}
    meta_rows = []

    print(f"Lese {len(files)} Kursdateien ...", flush=True)

    for i, f in enumerate(files, 1):
        try:
            df = pd.read_csv(
                f,
                usecols=lambda c: c in {
                    "ISIN", "conId", "Symbol", "Currency",
                    "date", "close", "volume"
                }
            )
        except Exception:
            df = pd.read_csv(f)

        required = {"date", "close"}
        if df.empty or not required.issubset(df.columns):
            continue

        df["date"] = pd.to_datetime(df["date"], errors="coerce")
        df["close"] = pd.to_numeric(df["close"], errors="coerce")

        if "volume" in df.columns:
            df["volume"] = pd.to_numeric(df["volume"], errors="coerce")
        else:
            df["volume"] = np.nan

        df = df.dropna(subset=["date", "close"]).sort_values("date")
        df = df[(df["date"] >= START) & (df["date"] <= END)]

        if len(df) < 30:
            continue

        conid = str(df["conId"].iloc[0]).replace(".0", "") if "conId" in df.columns else ""
        isin = str(df["ISIN"].iloc[0]) if "ISIN" in df.columns else ""
        symbol = str(df["Symbol"].iloc[0]) if "Symbol" in df.columns else ""
        currency = str(df["Currency"].iloc[0]) if "Currency" in df.columns else ""

        key = conid if conid else f.stem

        dfx = df.drop_duplicates("date").set_index("date")
        close_series[key] = dfx["close"].astype(float)
        volume_series[key] = dfx["volume"].astype(float)

        meta_rows.append({
            "key": key,
            "conId": conid,
            "ISIN": isin,
            "Symbol": symbol,
            "Currency": currency,
            "FirstDate": dfx.index.min(),
            "LastDate": dfx.index.max(),
            "Rows": len(dfx),
        })

        if i % 500 == 0 or i == len(files):
            print(f"  {i:>5}/{len(files)} gelesen", flush=True)

    close = pd.DataFrame(close_series).sort_index()
    volume = pd.DataFrame(volume_series).sort_index()

    close = close.loc[(close.index >= START) & (close.index <= END)]
    volume = volume.reindex(close.index)

    # Nur kleine Lücken überbrücken
    close = close.ffill(limit=3)
    volume = volume.fillna(0)

    meta = pd.DataFrame(meta_rows)

    print(f"Preis-Matrix:  {close.shape[0]} Tage x {close.shape[1]} Aktien")
    print(f"Volumen-Matrix:{volume.shape[0]} Tage x {volume.shape[1]} Aktien")

    return close, volume, meta


def analyze_data_quality(close: pd.DataFrame, meta: pd.DataFrame):
    rets = close.pct_change(fill_method=None)
    blocked: Dict[str, set] = {c: set() for c in close.columns}
    problem_count = {c: 0 for c in close.columns}
    rows = []

    dates = close.index
    meta_idx = meta.set_index("key") if not meta.empty else pd.DataFrame()

    for key in close.columns:
        s = close[key]
        r = rets[key]

        # Nichtpositive Preise
        for d, px in s[s <= 0].items():
            problem_count[key] += 1
            loc = dates.get_loc(d)
            for j in range(max(0, loc-BLOCK_WINDOW), min(len(dates), loc+BLOCK_WINDOW+1)):
                blocked[key].add(dates[j])

            rows.append({
                "date": d,
                "key": key,
                "type": "NON_POSITIVE_PRICE",
                "prev_close": np.nan,
                "close": px,
                "return": np.nan,
                "reversal": False,
            })

        extreme_dates = r[(r >= EXTREME_UP) | (r <= EXTREME_DOWN)].dropna().index

        for d in extreme_dates:
            loc = dates.get_loc(d)
            if loc <= 0:
                continue

            prev_d = dates[loc-1]
            p0 = close.at[prev_d, key]
            p1 = close.at[d, key]
            rr = r.at[d]

            reversal = False
            for k in range(1, REVERSAL_WINDOW+1):
                if loc+k >= len(dates):
                    break
                pf = close.at[dates[loc+k], key]
                if pd.isna(pf) or pd.isna(p0) or p0 <= 0:
                    continue
                rel = pf / p0
                if 0.5 <= rel <= 1.5:
                    reversal = True
                    break

            very_extreme = rr >= 20.0 or rr <= -0.95

            if reversal or very_extreme:
                problem_count[key] += 1
                for j in range(max(0, loc-BLOCK_WINDOW), min(len(dates), loc+BLOCK_WINDOW+1)):
                    blocked[key].add(dates[j])

            rows.append({
                "date": d,
                "key": key,
                "type": "EXTREME_MOVE",
                "prev_close": p0,
                "close": p1,
                "return": rr,
                "reversal": reversal,
            })

    fully_excluded = {
        k for k, cnt in problem_count.items()
        if cnt > MAX_PROBLEM_EVENTS
    }

    suspicious = pd.DataFrame(rows)

    if not suspicious.empty and not meta_idx.empty:
        suspicious["ISIN"] = suspicious["key"].map(
            lambda k: meta_idx.loc[k, "ISIN"] if k in meta_idx.index else ""
        )
        suspicious["Symbol"] = suspicious["key"].map(
            lambda k: meta_idx.loc[k, "Symbol"] if k in meta_idx.index else ""
        )
        suspicious["conId"] = suspicious["key"].map(
            lambda k: meta_idx.loc[k, "conId"] if k in meta_idx.index else ""
        )

    return suspicious, blocked, fully_excluded


def variant_name(lookback, sell_thr, buy_thr, min_turnover, min_price):
    return (
        f"mom{lookback}_sell{int(abs(sell_thr)*100):02d}"
        f"_buy{int(buy_thr*100):02d}"
        f"_liq{int(min_turnover)}"
        f"_p{str(min_price).replace('.','_')}"
    )


def nearest_date(index, target):
    return index[index.get_indexer([target], method="nearest")[0]]


def run_backtest(
    close: pd.DataFrame,
    volume: pd.DataFrame,
    meta: pd.DataFrame,
    blocked: Dict[str, set],
    fully_excluded: set,
    lookback: int,
    sell_thr: float,
    buy_thr: float,
    min_turnover: float,
    min_price: float,
    transaction_cost: float = 0.0,
):
    """
    Vektorisierte V4-Version.

    Die teure tägliche Python-Schleife über alle ~7.000 Aktien entfällt.
    Momentum, Liquidität, Preisfilter und Datenqualitäts-Sperren werden
    als DataFrame-Masken vorbereitet. Pro Tag werden nur noch gehaltene
    Positionen und die tatsächlich besten Kandidaten in Python behandelt.
    """
    momentum = close / close.shift(lookback) - 1
    turnover = close * volume
    avg_turnover = turnover.rolling(
        LIQ_WINDOW,
        min_periods=LIQ_WINDOW
    ).mean()

    dates = close.index
    columns = close.columns

    # ----------------------------
    # Vektorisierte Eligibility-Maske
    # ----------------------------
    eligible_mask = (
        close.notna()
        & (close >= min_price)
        & avg_turnover.notna()
        & (avg_turnover >= min_turnover)
    )

    # Vollständig ausgeschlossene Aktien komplett sperren
    if fully_excluded:
        excluded_cols = [c for c in fully_excluded if c in eligible_mask.columns]
        if excluded_cols:
            eligible_mask.loc[:, excluded_cols] = False

    # Temporäre Datenqualitäts-Sperren eintragen.
    # Anzahl dieser Events ist klein, deshalb ist diese Schleife unkritisch.
    date_pos = {d: i for i, d in enumerate(dates)}
    col_pos = {c: i for i, c in enumerate(columns)}

    em_values = eligible_mask.to_numpy(copy=True)

    for key, blocked_dates in blocked.items():
        j = col_pos.get(key)
        if j is None or not blocked_dates:
            continue
        for d in blocked_dates:
            i = date_pos.get(d)
            if i is not None:
                em_values[i, j] = False

    eligible_mask = pd.DataFrame(
        em_values,
        index=dates,
        columns=columns
    )

    buy_mask = eligible_mask & (momentum > buy_thr)

    # Für Verkäufe zählt zusätzlich Momentum < Schwelle oder fehlend.
    # "invalid" umfasst schlechte Liquidität, Preis, Sperre usw.
    sell_mask = (~eligible_mask) | momentum.isna() | (momentum < sell_thr)

    meta_idx = meta.set_index("key") if not meta.empty else pd.DataFrame()

    def meta_for(key):
        if not meta_idx.empty and key in meta_idx.index:
            x = meta_idx.loc[key]
            return (
                x.get("ISIN", ""),
                x.get("Symbol", ""),
                x.get("conId", ""),
                x.get("Currency", "")
            )
        return "", "", "", ""

    # 100 feste Slots
    slot_values = np.full(MAX_SLOTS, 1.0 / MAX_SLOTS, dtype=float)
    slot_tickers = np.empty(MAX_SLOTS, dtype=object)
    slot_tickers[:] = None

    trades = 0
    trade_log: List[dict] = []
    records = []

    start_idx = max(lookback, LIQ_WINDOW)
    signal_date = dates[start_idx]

    # Startkandidaten vektorisiert
    m0 = momentum.iloc[start_idx]
    bm0 = buy_mask.iloc[start_idx]
    cand0 = m0[bm0].nlargest(MAX_SLOTS)
    pending_sell_slots = []
    pending_buy_map = {
        slot_idx: ticker
        for slot_idx, ticker in enumerate(cand0.index)
    }

    def total_nav():
        return float(slot_values.sum())

    def cash_value():
        cash_slots = pd.isna(slot_tickers)
        return float(slot_values[cash_slots].sum())

    records.append({
        "date": signal_date,
        "nav": total_nav(),
        "cash": cash_value(),
        "cash_frac": cash_value() / total_nav(),
        "n_stocks": int(pd.notna(slot_tickers).sum()),
        "trades": trades,
        "eligible_count": int(bm0.sum()),
    })

    prev_date = signal_date

    # Cache integer column positions for fast .iat/.iloc access
    col_indexer = {c: j for j, c in enumerate(columns)}

    for i in range(start_idx + 1, len(dates)):
        d = dates[i]
        prev_i = i - 1

        # 1) Mark-to-market nur für maximal 100 gehaltene Titel
        for s in range(MAX_SLOTS):
            t = slot_tickers[s]
            if t is None:
                continue
            j = col_indexer[t]
            p0 = close.iat[prev_i, j]
            p1 = close.iat[i, j]
            if pd.notna(p0) and pd.notna(p1) and p0 > 0:
                slot_values[s] *= p1 / p0

        # 2) Pending sells
        for s in pending_sell_slots:
            t = slot_tickers[s]
            if t is None:
                continue
            j = col_indexer[t]
            isin, symbol, conid, currency = meta_for(t)

            trade_log.append({
                "date": d,
                "slot": s,
                "action": "SELL",
                "key": t,
                "ISIN": isin,
                "Symbol": symbol,
                "conId": conid,
                "Currency": currency,
                "price": close.iat[i, j],
                "slot_value": slot_values[s],
                "momentum_signal": momentum.iat[prev_i, j],
                "avg_turnover_signal": avg_turnover.iat[prev_i, j],
                "reason": "SELL_THRESHOLD_OR_INVALID",
            })
            # Proportionale Kosten/Slippage beim Verkauf.
            # Beispiel 0.001 = 0,10 % vom Positionswert.
            slot_values[s] *= (1.0 - transaction_cost)
            slot_tickers[s] = None
            trades += 1

        # 3) Pending buys
        for s, t in pending_buy_map.items():
            if slot_tickers[s] is not None:
                continue
            j = col_indexer[t]

            # Noch einmal am Ausführungstag prüfen, ob Kurs vorhanden/positiv
            px = close.iat[i, j]
            if pd.isna(px) or px <= 0:
                continue

            isin, symbol, conid, currency = meta_for(t)
            trade_log.append({
                "date": d,
                "slot": s,
                "action": "BUY",
                "key": t,
                "ISIN": isin,
                "Symbol": symbol,
                "conId": conid,
                "Currency": currency,
                "price": px,
                "slot_value": slot_values[s],
                "momentum_signal": momentum.iat[prev_i, j],
                "avg_turnover_signal": avg_turnover.iat[prev_i, j],
                "reason": "TOP_LIQUID_MOMENTUM_CANDIDATE",
            })
            # Proportionale Kosten/Slippage beim Kauf.
            slot_values[s] *= (1.0 - transaction_cost)
            slot_tickers[s] = t
            trades += 1

        # 4) Signale für morgen
        held_slots = [
            s for s in range(MAX_SLOTS)
            if slot_tickers[s] is not None
        ]

        next_sell_slots = []
        for s in held_slots:
            t = slot_tickers[s]
            j = col_indexer[t]
            if bool(sell_mask.iat[i, j]):
                next_sell_slots.append(s)

        future_held = {
            slot_tickers[s]
            for s in held_slots
            if s not in next_sell_slots
        }

        free_slots = [
            s for s in range(MAX_SLOTS)
            if slot_tickers[s] is None or s in next_sell_slots
        ]

        next_buy_map = {}

        if free_slots:
            # Nur eine vektorisierte Zeile statt Schleife über Tausende Aktien.
            row_mom = momentum.iloc[i]
            row_buy = buy_mask.iloc[i]

            # Bereits gehaltene zukünftige Positionen nicht erneut kaufen.
            if future_held:
                row_buy = row_buy.copy()
                existing = [t for t in future_held if t in row_buy.index]
                if existing:
                    row_buy.loc[existing] = False

            # Titel, die morgen verkauft werden, nicht am selben Ausführungstag zurückkaufen.
            selling = [
                slot_tickers[s] for s in next_sell_slots
                if slot_tickers[s] is not None
            ]
            if selling:
                if row_buy is buy_mask.iloc[i]:
                    row_buy = row_buy.copy()
                row_buy.loc[[t for t in selling if t in row_buy.index]] = False

            candidates = row_mom[row_buy].nlargest(len(free_slots))

            next_buy_map = {
                s: t
                for s, t in zip(free_slots, candidates.index)
            }

        pending_sell_slots = next_sell_slots
        pending_buy_map = next_buy_map

        nav = total_nav()
        cval = cash_value()

        records.append({
            "date": d,
            "nav": nav,
            "cash": cval,
            "cash_frac": cval / nav if nav > 0 else np.nan,
            "n_stocks": int(pd.notna(slot_tickers).sum()),
            "trades": trades,
            "eligible_count": int(buy_mask.iloc[i].sum()),
        })

        prev_date = d

        if (i - start_idx) % 250 == 0:
            print(
                f"  Backtest: {i-start_idx:>4}/{len(dates)-start_idx-1} Tage | "
                f"NAV={nav:.3f} | Aktien={int(pd.notna(slot_tickers).sum())}",
                flush=True
            )

    daily = pd.DataFrame(records).set_index("date")
    trades_df = pd.DataFrame(trade_log)

    return daily, trades_df, avg_turnover

def calc_metrics(daily):
    nav = daily["nav"]
    rets = nav.pct_change().dropna()

    total_return = nav.iloc[-1] / nav.iloc[0] - 1

    years = (daily.index[-1] - daily.index[0]).days / 365.25
    cagr = (nav.iloc[-1] / nav.iloc[0]) ** (1 / years) - 1

    dd = nav / nav.cummax() - 1

    vol = rets.std() * np.sqrt(252) if len(rets) > 1 else np.nan
    sharpe = (
        rets.mean() / rets.std() * np.sqrt(252)
        if len(rets) > 1 and rets.std() > 0
        else np.nan
    )

    return {
        "TotalReturn": total_return,
        "CAGR": cagr,
        "MaxDrawdown": dd.min(),
        "Volatility": vol,
        "Sharpe": sharpe,
        "Trades": int(daily["trades"].iloc[-1]),
        "AvgCash": daily["cash_frac"].mean(),
        "MaxCash": daily["cash_frac"].max(),
        "AvgStocks": daily["n_stocks"].mean(),
        "MinStocks": int(daily["n_stocks"].min()),
        "AvgEligibleCount": daily["eligible_count"].mean(),
        "MinEligibleCount": int(daily["eligible_count"].min()),
    }


def crash_metrics(daily, peak, trough, label):
    idx = daily.index
    peak_d = nearest_date(idx, peak)
    trough_d = nearest_date(idx, trough)

    nav0 = daily.loc[peak_d, "nav"]

    out = {
        "Crash": label,
        "PeakDate": peak_d.date().isoformat(),
        "TroughDate": trough_d.date().isoformat(),
        "ReturnToTrough": daily.loc[trough_d, "nav"] / nav0 - 1,
        "StocksAtTrough": int(daily.loc[trough_d, "n_stocks"]),
        "CashAtTrough": float(daily.loc[trough_d, "cash_frac"]),
        "EligibleAtTrough": int(daily.loc[trough_d, "eligible_count"]),
    }

    for months in [3, 6, 12]:
        d = nearest_date(idx, trough + pd.DateOffset(months=months))
        out[f"ReturnPeakToPlus{months}M"] = daily.loc[d, "nav"] / nav0 - 1
        out[f"StocksPlus{months}M"] = int(daily.loc[d, "n_stocks"])
        out[f"CashPlus{months}M"] = float(daily.loc[d, "cash_frac"])

    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--folder", default=DEFAULT_FOLDER)
    ap.add_argument("--out", default="backtest_sell10_vs_sell15")
    ap.add_argument("--min-price", type=float, default=0.50)
    args = ap.parse_args()

    folder = Path(args.folder)
    out_dir = Path(args.out)
    out_dir.mkdir(exist_ok=True)

    close, volume, meta = load_market_data(folder)

    print("Analysiere Datenqualität ...", flush=True)
    suspicious, blocked, fully_excluded = analyze_data_quality(close, meta)

    # Feste gemeinsame Parameter
    lookback = 20
    buy_thr = 0.02
    min_turnover = 1_000_000
    transaction_cost = 0.0010  # 0,10 % je Kauf/Verkauf

    variants = [
        ("SELL_MINUS_10", -0.10),
        ("SELL_MINUS_15", -0.15),
    ]

    summary_rows = []
    yearly_rows = []
    crash_rows = []

    for n, (label, sell_thr) in enumerate(variants, 1):
        print()
        print("=" * 72)
        print(f"[{n}/2] {label}")
        print(
            f"20T Momentum | Sell<{sell_thr:.0%} | Buy>+2% | "
            f"Liquiditaet >= 1 Mio. | Kosten 0,10%"
        )

        daily, trades, _ = run_backtest(
            close,
            volume,
            meta,
            blocked,
            fully_excluded,
            lookback,
            sell_thr,
            buy_thr,
            min_turnover,
            args.min_price,
            transaction_cost=transaction_cost,
        )

        met = calc_metrics(daily)

        summary_rows.append({
            "Variant": label,
            "MomentumDays": lookback,
            "SellThreshold": sell_thr,
            "BuyThreshold": buy_thr,
            "TransactionCost": transaction_cost,
            **met,
        })

        daily.to_csv(
            out_dir / f"daily_{label}.csv",
            encoding="utf-8-sig"
        )

        trades.to_csv(
            out_dir / f"trades_{label}.csv",
            index=False,
            encoding="utf-8-sig"
        )

        # Jahresrenditen und Jahres-Drawdowns
        for year, grp in daily.groupby(daily.index.year):
            if grp.empty:
                continue

            # Rendite vom ersten verfügbaren NAV des Jahres bis zum letzten.
            yr_return = grp["nav"].iloc[-1] / grp["nav"].iloc[0] - 1
            yr_dd = (grp["nav"] / grp["nav"].cummax() - 1).min()

            yearly_rows.append({
                "Variant": label,
                "Year": int(year),
                "Return": yr_return,
                "MaxDrawdown": yr_dd,
                "StartNAV": grp["nav"].iloc[0],
                "EndNAV": grp["nav"].iloc[-1],
                "EndStocks": int(grp["n_stocks"].iloc[-1]),
                "EndCash": float(grp["cash_frac"].iloc[-1]),
            })

        for peak, trough, crash_label in [
            (CORONA_PEAK, CORONA_TROUGH, "Corona2020"),
            (BEAR22_PEAK, BEAR22_TROUGH, "Bear2022"),
        ]:
            cm = crash_metrics(daily, peak, trough, crash_label)
            crash_rows.append({
                "Variant": label,
                **cm,
            })

        print(
            f"  CAGR={met['CAGR']:.2%} | "
            f"Gesamt={met['TotalReturn']:.1%} | "
            f"MaxDD={met['MaxDrawdown']:.1%} | "
            f"Sharpe={met['Sharpe']:.2f} | "
            f"Trades={met['Trades']}"
        )

    summary = pd.DataFrame(summary_rows)
    yearly = pd.DataFrame(yearly_rows)
    crashes = pd.DataFrame(crash_rows)

    summary.to_csv(
        out_dir / "sell10_vs_sell15_summary.csv",
        index=False,
        encoding="utf-8-sig"
    )
    yearly.to_csv(
        out_dir / "sell10_vs_sell15_yearly.csv",
        index=False,
        encoding="utf-8-sig"
    )
    crashes.to_csv(
        out_dir / "sell10_vs_sell15_crashes.csv",
        index=False,
        encoding="utf-8-sig"
    )

    print()
    print("=" * 72)
    print("GESAMTVERGLEICH")
    print(
        summary[
            ["Variant", "CAGR", "TotalReturn", "MaxDrawdown",
             "Sharpe", "Trades", "AvgCash"]
        ].to_string(index=False)
    )

    print()
    print("JAHRESRENDITEN")
    pivot = yearly.pivot(
        index="Year",
        columns="Variant",
        values="Return"
    )
    print(pivot.to_string())

    print()
    print(f"Ergebnisse: {out_dir.resolve()}")


if __name__ == "__main__":
    main()
