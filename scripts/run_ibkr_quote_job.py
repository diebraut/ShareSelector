#!/usr/bin/env python3
"""Run resumable IBKR quote batches outside the ShareSelector UI.

The worker uses the existing IbkrHelper.exe for IBKR API access and psql for
PostgreSQL access, so it does not need additional Python packages.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, replace
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Iterable


DEFAULT_DB_NAME = "TotalStocks"
DEFAULT_DB_USER = "postgres"
DEFAULT_DB_PASSWORD = "castell"
DEFAULT_DB_HOST = "localhost"
DEFAULT_HELPER = (
    r"K:\QT-Projekte\Desktop_Qt_6_10_1_MSVC2022_64bit-Debug\ibkr-helper\IbkrHelper.exe"
)


@dataclass
class DbConfig:
    host: str
    database: str
    user: str
    password: str
    psql: str


@dataclass
class StockRequest:
    item_id: int
    symbol: str
    con_id: int
    ibkr_symbol: str
    currency: str
    isin: str
    quote_exchange: str
    primary_exchange: str
    valid_exchanges: str
    days: int


@dataclass
class JobProgress:
    total: int
    current: int
    done: int
    success: int
    failed: int
    skipped: int


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value).encode("ascii", "replace").decode("ascii").replace("'", "''")
    return f"'{text}'"


def run_psql(db: DbConfig, sql: str, *, capture: bool = True) -> str:
    env = os.environ.copy()
    env["PGPASSWORD"] = db.password
    cmd = [
        db.psql,
        "-h",
        db.host,
        "-U",
        db.user,
        "-d",
        db.database,
        "-X",
        "-q",
        "-v",
        "ON_ERROR_STOP=1",
        "-A",
        "-t",
        "-F",
        "\t",
        "-c",
        sql,
    ]
    completed = subprocess.run(
        cmd,
        env=env,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "psql failed")
    return completed.stdout.strip() if capture and completed.stdout else ""


def run_psql_csv(db: DbConfig, query: str) -> list[dict[str, str]]:
    sql = f"COPY ({query}) TO STDOUT WITH CSV HEADER"
    env = os.environ.copy()
    env["PGPASSWORD"] = db.password
    cmd = [
        db.psql,
        "-h",
        db.host,
        "-U",
        db.user,
        "-d",
        db.database,
        "-X",
        "-q",
        "-c",
        sql,
    ]
    completed = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "psql CSV query failed")
    return list(csv.DictReader(completed.stdout.splitlines()))


def ensure_schema(db: DbConfig) -> None:
    run_psql(
        db,
        """
        CREATE TABLE IF NOT EXISTS ibkr_quote_jobs (
            id BIGSERIAL PRIMARY KEY,
            name TEXT NOT NULL,
            scope TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending',
            total_items INTEGER NOT NULL DEFAULT 0,
            done_items INTEGER NOT NULL DEFAULT 0,
            success_items INTEGER NOT NULL DEFAULT 0,
            failed_items INTEGER NOT NULL DEFAULT 0,
            changed_quotes INTEGER NOT NULL DEFAULT 0,
            created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
            started_at TIMESTAMPTZ,
            finished_at TIMESTAMPTZ,
            updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
            last_error TEXT
        );

        CREATE TABLE IF NOT EXISTS ibkr_quote_job_items (
            id BIGSERIAL PRIMARY KEY,
            job_id BIGINT NOT NULL REFERENCES ibkr_quote_jobs(id) ON DELETE CASCADE,
            symbol TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending',
            attempts INTEGER NOT NULL DEFAULT 0,
            changed_quote_count INTEGER NOT NULL DEFAULT 0,
            rows_received INTEGER NOT NULL DEFAULT 0,
            last_error TEXT,
            last_message TEXT,
            started_at TIMESTAMPTZ,
            finished_at TIMESTAMPTZ,
            updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(job_id, symbol)
        );

        CREATE INDEX IF NOT EXISTS ibkr_quote_job_items_job_status_idx
            ON ibkr_quote_job_items(job_id, status, id);
        """,
        capture=False,
    )


def create_job(db: DbConfig, name: str, scope: str, symbols: list[str] | None) -> int:
    job_id_text = run_psql(
        db,
        f"""
        INSERT INTO ibkr_quote_jobs(name, scope, status)
        VALUES ({sql_literal(name)}, {sql_literal(scope)}, 'pending')
        RETURNING id;
        """,
    )
    job_id = int(job_id_text.splitlines()[-1])

    if symbols is None and scope == "stale_ibkr":
        run_psql(
            db,
            f"""
            WITH last_trading_day AS (
                SELECT MAX(q."CloseDate") AS value
                FROM "Quotes" q
                WHERE COALESCE(q."ClosePrice", 0) > 0
                  AND q."CloseDate" < CURRENT_DATE
            ),
            latest_quotes AS (
                SELECT
                    s."Symbol",
                    MAX(q."CloseDate") AS latest_date
                FROM "Stocks" s
                LEFT JOIN "Quotes" q
                    ON q."Symbol" = s."Symbol"
                   AND COALESCE(q."ClosePrice", 0) > 0
                WHERE COALESCE(s."Symbol", '') <> ''
                  AND s."IBKRConId" IS NOT NULL
                  AND COALESCE(s."from_IBKR", TRUE) = TRUE
                GROUP BY s."Symbol"
            )
            INSERT INTO ibkr_quote_job_items(job_id, symbol)
            SELECT {job_id}, lq."Symbol"
            FROM latest_quotes lq
            CROSS JOIN last_trading_day ltd
            WHERE lq.latest_date IS NULL
               OR (ltd.value IS NOT NULL AND lq.latest_date < ltd.value)
            ORDER BY lq.latest_date NULLS FIRST, lq."Symbol"
            ON CONFLICT(job_id, symbol) DO NOTHING;
            """,
            capture=False,
        )
    elif symbols is None:
        run_psql(
            db,
            f"""
            INSERT INTO ibkr_quote_job_items(job_id, symbol)
            SELECT {job_id}, s."Symbol"
            FROM "Stocks" s
            WHERE COALESCE(s."Symbol", '') <> ''
              AND s."IBKRConId" IS NOT NULL
              AND COALESCE(s."from_IBKR", TRUE) = TRUE
            ON CONFLICT(job_id, symbol) DO NOTHING;
            """,
            capture=False,
        )
    else:
        values = ",\n".join(f"({job_id}, {sql_literal(symbol)})" for symbol in symbols)
        if values:
            run_psql(
                db,
                f"""
                INSERT INTO ibkr_quote_job_items(job_id, symbol)
                VALUES {values}
                ON CONFLICT(job_id, symbol) DO NOTHING;
                """,
                capture=False,
            )

    refresh_job_counts(db, job_id)
    return job_id


def refresh_job_counts(db: DbConfig, job_id: int) -> None:
    run_psql(
        db,
        f"""
        UPDATE ibkr_quote_jobs j
        SET total_items = counts.total_items,
            done_items = counts.done_items,
            success_items = counts.success_items,
            failed_items = counts.failed_items,
            changed_quotes = counts.changed_quotes,
            updated_at = CURRENT_TIMESTAMP
        FROM (
            SELECT
                COUNT(*)::int AS total_items,
                COUNT(*) FILTER (WHERE status IN ('success', 'failed', 'skipped'))::int AS done_items,
                COUNT(*) FILTER (WHERE status = 'success')::int AS success_items,
                COUNT(*) FILTER (WHERE status = 'failed')::int AS failed_items,
                COALESCE(SUM(changed_quote_count), 0)::int AS changed_quotes
            FROM ibkr_quote_job_items
            WHERE job_id = {job_id}
        ) counts
        WHERE j.id = {job_id};
        """,
        capture=False,
    )


def mark_job_status(db: DbConfig, job_id: int, status: str, error: str | None = None) -> None:
    finished = ", finished_at = CURRENT_TIMESTAMP" if status in {"complete", "failed", "stopped"} else ""
    started = ", started_at = COALESCE(started_at, CURRENT_TIMESTAMP)" if status == "running" else ""
    run_psql(
        db,
        f"""
        UPDATE ibkr_quote_jobs
        SET status = {sql_literal(status)},
            last_error = {sql_literal(error)},
            updated_at = CURRENT_TIMESTAMP
            {started}
            {finished}
        WHERE id = {job_id};
        """,
        capture=False,
    )


def mark_current_items_skipped(db: DbConfig, job_id: int) -> None:
    run_psql(
        db,
        f"""
        WITH expected_quote_date AS (
            SELECT CASE
                WHEN EXTRACT(ISODOW FROM CURRENT_DATE)::int = 6 THEN CURRENT_DATE - 1
                WHEN EXTRACT(ISODOW FROM CURRENT_DATE)::int = 7 THEN CURRENT_DATE - 2
                ELSE CURRENT_DATE
            END AS value
        )
        UPDATE ibkr_quote_job_items i
        SET status = 'skipped',
            last_message = 'Skipped because quote is already current',
            finished_at = CURRENT_TIMESTAMP,
            updated_at = CURRENT_TIMESTAMP
        FROM expected_quote_date eq
        WHERE i.job_id = {job_id}
          AND i.status IN ('pending', 'failed')
          AND EXISTS (
              SELECT 1
              FROM "Quotes" q
              WHERE q."Symbol" = i.symbol
                AND q."CloseDate" >= eq.value
                AND COALESCE(q."ClosePrice", 0) > 0
          );
        """,
        capture=False,
    )


def fetch_next_item(db: DbConfig, job_id: int) -> StockRequest | None:
    rows = run_psql_csv(
        db,
        f"""
        WITH picked AS (
            SELECT i.id
            FROM ibkr_quote_job_items i
            WHERE i.job_id = {job_id}
              AND i.status = 'pending'
            ORDER BY
                i.id
            LIMIT 1
            FOR UPDATE SKIP LOCKED
        ),
        marked AS (
            UPDATE ibkr_quote_job_items i
            SET status = 'running',
                attempts = attempts + 1,
                started_at = CURRENT_TIMESTAMP,
                updated_at = CURRENT_TIMESTAMP
            FROM picked
            WHERE i.id = picked.id
            RETURNING i.id, i.symbol
        ),
        stock_rows AS (
            SELECT
                marked.id AS item_id,
                s."Symbol" AS symbol,
                s."IBKRConId" AS con_id,
                COALESCE(NULLIF(s."IBKRResolvedSymbol", ''), NULLIF(s."LocalSymbol", ''), split_part(s."Symbol", '.', 1)) AS ibkr_symbol,
                COALESCE(NULLIF(s."Currency", ''), 'EUR') AS currency,
                COALESCE(s."ISIN", '') AS isin,
                COALESCE(NULLIF(s."IBKRQuoteExchange", ''), NULLIF(s."IBKRBestDirectExchange", ''), NULLIF(s."PrimaryExchange", ''), NULLIF(s."MIC", ''), 'SMART') AS quote_exchange,
                COALESCE(s."ValidExchanges", '') AS valid_exchanges,
                CASE
                    WHEN COALESCE(NULLIF(s."IBKRQuoteExchange", ''), '') = 'SMART'
                    THEN COALESCE(NULLIF(s."IBKRBestDirectExchange", ''), NULLIF(s."PrimaryExchange", ''), '')
                    ELSE ''
                END AS primary_exchange
            FROM marked
            JOIN "Stocks" s ON s."Symbol" = marked.symbol
        ),
        quote_state AS (
            SELECT
                sr.*,
                MAX(q."CloseDate") AS last_quote_date,
                COUNT(*) FILTER (WHERE COALESCE(q."ClosePrice", 0) > 0) AS valid_quote_count
            FROM stock_rows sr
            LEFT JOIN "Quotes" q ON q."Symbol" = sr.symbol
            GROUP BY sr.item_id, sr.symbol, sr.con_id, sr.ibkr_symbol, sr.currency, sr.isin, sr.quote_exchange, sr.primary_exchange, sr.valid_exchanges
        )
        SELECT
            item_id,
            symbol,
            con_id,
            ibkr_symbol,
            currency,
            isin,
            quote_exchange,
            primary_exchange,
            valid_exchanges,
            CASE
                WHEN last_quote_date IS NULL THEN 147
                WHEN valid_quote_count < 90 THEN GREATEST((CURRENT_DATE - last_quote_date + 2)::int, 147)
                ELSE GREATEST((CURRENT_DATE - last_quote_date + 2)::int, 1)
            END AS days
        FROM quote_state
        """,
    )
    if not rows:
        return None
    row = rows[0]
    return StockRequest(
        item_id=int(row["item_id"]),
        symbol=row["symbol"],
        con_id=int(row["con_id"]),
        ibkr_symbol=row["ibkr_symbol"],
        currency=row["currency"],
        isin=row["isin"],
        quote_exchange=(row["quote_exchange"] or "SMART").upper(),
        primary_exchange=(row["primary_exchange"] or "").upper(),
        valid_exchanges=row["valid_exchanges"] or "",
        days=max(1, int(row["days"])),
    )


def mark_item(
    db: DbConfig,
    item_id: int,
    status: str,
    *,
    changed: int = 0,
    rows_received: int = 0,
    message: str = "",
    error: str = "",
) -> None:
    run_psql(
        db,
        f"""
        UPDATE ibkr_quote_job_items
        SET status = {sql_literal(status)},
            changed_quote_count = {int(changed)},
            rows_received = {int(rows_received)},
            last_message = {sql_literal(message[:500])},
            last_error = {sql_literal(error[:500])},
            finished_at = CURRENT_TIMESTAMP,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = {item_id};
        """,
        capture=False,
    )


def save_bars(db: DbConfig, symbol: str, bars: list[dict[str, object]]) -> int:
    values: list[str] = []
    for bar in bars:
        date = str(bar.get("date") or "")
        if not date:
            continue
        values.append(
            "("
            + ", ".join(
                [
                    sql_literal(symbol),
                    sql_literal(date),
                    sql_literal(float(bar.get("close") or 0)),
                    sql_literal(float(bar.get("open") or 0)),
                    sql_literal(float(bar.get("high") or 0)),
                    sql_literal(float(bar.get("low") or 0)),
                    sql_literal(float(bar.get("volume") or 0)),
                ]
            )
            + ")"
        )
    if not values:
        return 0

    changed_text = run_psql(
        db,
        f"""
        WITH incoming("Symbol", "CloseDate", "ClosePrice", "OpenPrice", "HighestPrice", "LowestPrice", "Volume") AS (
            VALUES {", ".join(values)}
        ),
        changed AS (
            SELECT COUNT(*)::int AS changed_count
            FROM incoming i
            LEFT JOIN "Quotes" q
              ON q."Symbol" = i."Symbol"
             AND q."CloseDate" = i."CloseDate"::date
            WHERE q."Symbol" IS NULL
               OR q."ClosePrice" IS DISTINCT FROM i."ClosePrice"::double precision
               OR q."OpenPrice" IS DISTINCT FROM i."OpenPrice"::double precision
               OR q."HighestPrice" IS DISTINCT FROM i."HighestPrice"::double precision
               OR q."LowestPrice" IS DISTINCT FROM i."LowestPrice"::double precision
               OR q."Volume" IS DISTINCT FROM i."Volume"::double precision
        ),
        upserted AS (
            INSERT INTO "Quotes" (
                "Symbol", "CloseDate", "ClosePrice", "OpenPrice",
                "HighestPrice", "LowestPrice", "Volume"
            )
            SELECT
                "Symbol",
                "CloseDate"::date,
                "ClosePrice"::double precision,
                "OpenPrice"::double precision,
                "HighestPrice"::double precision,
                "LowestPrice"::double precision,
                "Volume"::double precision
            FROM incoming
            ON CONFLICT ("Symbol", "CloseDate") DO UPDATE
            SET
                "ClosePrice" = EXCLUDED."ClosePrice",
                "OpenPrice" = EXCLUDED."OpenPrice",
                "HighestPrice" = EXCLUDED."HighestPrice",
                "LowestPrice" = EXCLUDED."LowestPrice",
                "Volume" = EXCLUDED."Volume"
            RETURNING 1
        ),
        stock_update AS (
            UPDATE "Stocks"
            SET "LastUpdateDate" = CURRENT_DATE
            WHERE "Symbol" = {sql_literal(symbol)}
            RETURNING 1
        )
        SELECT changed_count FROM changed;
        """,
    )
    return int(changed_text.splitlines()[-1]) if changed_text else 0


def save_working_quote_exchange(db: DbConfig, symbol: str, exchange: str) -> None:
    normalized = exchange.strip().upper()
    if not normalized or normalized == "SMART":
        return
    run_psql(
        db,
        f"""
        UPDATE "Stocks"
        SET "IBKRQuoteExchange" = {sql_literal(normalized)},
            "IBKRBestDirectExchange" = {sql_literal(normalized)},
            "IBKRQuoteExchangeLastSuccessAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeFailureCount" = 0,
            "IBKRQuoteExchangeLastError" = NULL
        WHERE "Symbol" = {sql_literal(symbol)};
        """,
        capture=False,
    )


def expected_quote_date() -> str:
    value = date.today()
    while value.weekday() >= 5:
        value -= timedelta(days=1)
    return value.isoformat()


def save_snapshot_quote(db: DbConfig, symbol: str, data: dict[str, object]) -> int:
    selected = float(data.get("selected") or 0)
    if selected <= 0:
        return 0

    sizes = data.get("sizes")
    volume = 0
    if isinstance(sizes, dict):
        volume = float(sizes.get("VOLUME") or sizes.get("DELAYED_VOLUME") or 0)

    return save_bars(
        db,
        symbol,
        [
            {
                "date": expected_quote_date(),
                "open": selected,
                "high": selected,
                "low": selected,
                "close": selected,
                "volume": volume,
            }
        ],
    )


def has_expected_quote(db: DbConfig, symbol: str) -> bool:
    result = run_psql(
        db,
        f"""
        SELECT EXISTS (
            SELECT 1
            FROM "Quotes"
            WHERE "Symbol" = {sql_literal(symbol)}
              AND "CloseDate" = {sql_literal(expected_quote_date())}::date
              AND COALESCE("ClosePrice", 0) > 0
        );
        """,
    )
    return result.splitlines()[-1].strip().lower() in {"t", "true", "1"}


def helper_contract_args(request: StockRequest) -> list[str]:
    helper_args = [
        "--symbol",
        request.ibkr_symbol,
        "--con-id",
        str(request.con_id),
    ]
    if request.currency:
        helper_args += ["--currency", request.currency]
    if request.quote_exchange and request.quote_exchange != "SMART":
        helper_args += ["--exchange", request.quote_exchange, "--direct-exchange"]
    elif request.primary_exchange:
        helper_args += ["--primary-exchange", request.primary_exchange]
    if request.con_id <= 0 and request.isin:
        helper_args += ["--isin", request.isin]
    return helper_args


def exchange_request(request: StockRequest, exchange: str) -> StockRequest:
    normalized = exchange.strip().upper()
    return replace(
        request,
        quote_exchange=normalized,
        primary_exchange="",
    )


def exchange_values(value: str) -> set[str]:
    return {
        part.strip().upper()
        for part in value.split(",")
        if part.strip()
    }


def better_direct_exchange_attempts(request: StockRequest) -> list[tuple[str, StockRequest]]:
    attempts: list[tuple[str, StockRequest]] = []
    valid = exchange_values(request.valid_exchanges)
    current = request.quote_exchange.strip().upper()
    primary = request.primary_exchange.strip().upper()

    def append_attempt(exchange: str, candidate_request: StockRequest) -> None:
        normalized = exchange.strip().upper()
        if normalized and normalized not in {existing for existing, _ in attempts}:
            attempts.append((normalized, candidate_request))

    if "FWB" in valid:
        append_attempt("FWB", exchange_request(request, "FWB"))

    has_fwb = "FWB" in valid

    if current and current != "SMART" and not (current == "SBF" and has_fwb):
        append_attempt(current, request)
    elif current == "SMART":
        if primary and primary != "SMART" and not (primary == "SBF" and has_fwb):
            append_attempt(primary, exchange_request(request, primary))
        append_attempt("SMART", request)

    for fallback in ("SWB", "GETTEX", "TGATE", "IBIS"):
        if fallback in valid:
            append_attempt(fallback, exchange_request(request, fallback))

    return attempts or [("SMART", smart_snapshot_request(request))]


def call_helper(args: argparse.Namespace, request: StockRequest) -> tuple[bool, str, list[dict[str, object]], int]:
    helper_args = [
        str(args.helper),
        "--host",
        args.ibkr_host,
        "--port",
        str(args.ibkr_port),
        "--client-id",
        str(args.client_id),
        "--historical-quotes",
        "--days",
        str(request.days),
    ] + helper_contract_args(request)

    started = time.monotonic()
    completed = subprocess.run(
        helper_args,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=args.timeout_seconds + 10,
    )
    elapsed_ms = int((time.monotonic() - started) * 1000)
    stdout = completed.stdout.strip()
    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError:
        stderr = completed.stderr.strip()
        return False, f"Invalid helper JSON: {stdout[:250]} {stderr[:250]}".strip(), [], elapsed_ms

    message = str(payload.get("message") or "")
    success = bool(payload.get("success"))
    data = payload.get("data") or []
    bars = data if isinstance(data, list) else []
    return success, message, bars, elapsed_ms


def call_snapshot_helper(args: argparse.Namespace, request: StockRequest) -> tuple[bool, str, dict[str, object], int]:
    helper_args = [
        str(args.helper),
        "--host",
        args.ibkr_host,
        "--port",
        str(args.ibkr_port),
        "--client-id",
        str(args.client_id + 1),
        "--market-snapshot",
        "--timeout-seconds",
        str(args.snapshot_timeout_seconds),
    ] + helper_contract_args(request)

    started = time.monotonic()
    completed = subprocess.run(
        helper_args,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=args.snapshot_timeout_seconds + 5,
    )
    elapsed_ms = int((time.monotonic() - started) * 1000)
    stdout = completed.stdout.strip()
    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError:
        stderr = completed.stderr.strip()
        return False, f"Invalid helper JSON: {stdout[:250]} {stderr[:250]}".strip(), {}, elapsed_ms

    message = str(payload.get("message") or "")
    success = bool(payload.get("success"))
    data = payload.get("data") or {}
    return success, message, data if isinstance(data, dict) else {}, elapsed_ms


def smart_snapshot_request(request: StockRequest) -> StockRequest:
    return replace(request, quote_exchange="SMART", primary_exchange="")


def snapshot_attempts(request: StockRequest) -> list[tuple[str, StockRequest]]:
    attempts: list[tuple[str, StockRequest]] = []
    for exchange, exchange_attempt in better_direct_exchange_attempts(request):
        if exchange != "SMART":
            attempts.append((f"snapshot-{exchange}", exchange_attempt))
    attempts.append(("snapshot-SMART", smart_snapshot_request(request)))
    return attempts


def should_try_snapshot_fallback(message: str) -> bool:
    normalized = message.lower()
    hard_failures = [
        "ibkr-fehler 200",
        "keine wertpapierdefinition",
        "no security definition",
    ]
    if any(value in normalized for value in hard_failures):
        return False
    return (
        "ibkr-fehler 2188" in normalized
        or "up-to-the-second historical data" in normalized
        or "ibkr-fehler 162" in normalized
        or "hmds-anfrage ergab keine daten" in normalized
        or "no data" in normalized
    )


def append_timing_log(path: Path, request: StockRequest, elapsed_ms: int, success: bool, message: str, phase: str = "historical") -> None:
    try:
        write_header = not path.exists() or path.stat().st_size == 0
        with path.open("a", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            if write_header:
                writer.writerow(["timestamp", "symbol", "phase", "elapsed_ms", "elapsed_s", "success", "message"])
            writer.writerow(
                [
                    datetime.now().isoformat(timespec="milliseconds"),
                    request.symbol,
                    phase,
                    elapsed_ms,
                    f"{elapsed_ms / 1000:.3f}",
                    "true" if success else "false",
                    message[:500],
                ]
            )
    except OSError as exc:
        print(f"  WARNING: timing log could not be written: {exc}")


def read_symbol_file(path: Path) -> list[str]:
    symbols: list[str] = []
    with path.open("r", encoding="utf-8-sig") as handle:
        for line in handle:
            symbol = line.strip()
            if symbol and symbol.upper() not in {value.upper() for value in symbols}:
                symbols.append(symbol)
    return symbols


def print_job_status(db: DbConfig, job_id: int) -> None:
    rows = run_psql_csv(
        db,
        f"""
        SELECT
            j.id,
            j.name,
            j.status,
            j.total_items,
            j.done_items,
            j.success_items,
            j.failed_items,
            j.changed_quotes,
            j.started_at,
            j.finished_at,
            j.last_error
        FROM ibkr_quote_jobs j
        WHERE j.id = {job_id}
        """,
    )
    if not rows:
        print(f"Job {job_id} not found")
        return
    row = rows[0]
    print(
        "Job {id} {status}: {done_items}/{total_items}, OK={success_items}, "
        "failed={failed_items}, changed={changed_quotes}".format(**row)
    )
    if row.get("last_error"):
        print(f"Last error: {row['last_error']}")


def get_job_progress(db: DbConfig, job_id: int) -> JobProgress:
    rows = run_psql_csv(
        db,
        f"""
        SELECT
            COUNT(*)::int AS total,
            COUNT(*) FILTER (WHERE status IN ('success', 'failed', 'skipped'))::int AS done,
            COUNT(*) FILTER (WHERE status = 'success')::int AS success,
            COUNT(*) FILTER (WHERE status = 'failed')::int AS failed,
            COUNT(*) FILTER (WHERE status = 'skipped')::int AS skipped,
            COUNT(*) FILTER (WHERE status = 'running')::int AS running
        FROM ibkr_quote_job_items
        WHERE job_id = {job_id}
        """,
    )
    row = rows[0] if rows else {}
    total = int(row.get("total") or 0)
    done = int(row.get("done") or 0)
    running = int(row.get("running") or 0)
    return JobProgress(
        total=total,
        current=min(total, done + running),
        done=done,
        success=int(row.get("success") or 0),
        failed=int(row.get("failed") or 0),
        skipped=int(row.get("skipped") or 0),
    )


def run_worker(args: argparse.Namespace, db: DbConfig, job_id: int) -> None:
    timing_log = Path(args.timing_log)
    run_psql(
        db,
        f"""
        UPDATE ibkr_quote_job_items
        SET status = 'pending',
            last_error = COALESCE(last_error, 'Resumed after interrupted worker'),
            updated_at = CURRENT_TIMESTAMP
        WHERE job_id = {job_id}
          AND status IN ('running', 'failed');
        """,
        capture=False,
    )
    mark_job_status(db, job_id, "running")
    processed = 0
    while True:
        if args.limit and processed >= args.limit:
            mark_job_status(db, job_id, "paused")
            break

        if not args.include_current:
            mark_current_items_skipped(db, job_id)
        request = fetch_next_item(db, job_id)
        if request is None:
            refresh_job_counts(db, job_id)
            mark_job_status(db, job_id, "complete")
            break

        progress = get_job_progress(db, job_id)
        print(
            f"[{datetime.now().strftime('%H:%M:%S')}] "
            f"{progress.current}/{progress.total} "
            f"{request.symbol}: {request.ibkr_symbol}/{request.quote_exchange}, days={request.days} "
            f"(OK={progress.success}, failed={progress.failed}, skipped={progress.skipped})"
        )
        try:
            success = False
            message = ""
            bars: list[dict[str, object]] = []
            elapsed_ms = 0
            historical_request = request
            historical_errors: list[str] = []
            for phase_exchange, candidate_request in better_direct_exchange_attempts(request):
                if phase_exchange != request.quote_exchange:
                    print(f"  Trying historical-{phase_exchange} ...")
                success, message, bars, elapsed_ms = call_helper(args, candidate_request)
                append_timing_log(
                    timing_log,
                    candidate_request,
                    elapsed_ms,
                    success,
                    message,
                    phase=f"historical-{phase_exchange}",
                )
                historical_request = candidate_request
                if success:
                    break
                historical_errors.append(f"{phase_exchange}: {message}")

            if not success:
                combined_historical_message = "; ".join(historical_errors) or message
                if not should_try_snapshot_fallback(combined_historical_message):
                    mark_item(
                        db,
                        request.item_id,
                        "failed",
                        message=combined_historical_message,
                        error=combined_historical_message,
                    )
                    print(f"  FAILED: {combined_historical_message}")
                    processed += 1
                    if processed % args.status_every == 0:
                        refresh_job_counts(db, job_id)
                        print_job_status(db, job_id)
                    if args.delay_seconds > 0:
                        time.sleep(args.delay_seconds)
                    continue

                print(f"  Historical failed, trying snapshot fallback ...")
                fallback_saved = False
                fallback_errors: list[str] = []
                for phase, snapshot_request in snapshot_attempts(request):
                    print(f"  Trying {phase} ...")
                    snapshot_success, snapshot_message, snapshot_data, snapshot_elapsed_ms = call_snapshot_helper(
                        args,
                        snapshot_request,
                    )
                    append_timing_log(
                        timing_log,
                        snapshot_request,
                        snapshot_elapsed_ms,
                        snapshot_success,
                        snapshot_message,
                        phase=phase,
                    )
                    if snapshot_success:
                        changed = save_snapshot_quote(db, request.symbol, snapshot_data)
                        mark_item(
                            db,
                            request.item_id,
                            "success",
                            changed=changed,
                            rows_received=1,
                            message=f"{message} {phase}: {snapshot_message}",
                        )
                        save_working_quote_exchange(db, request.symbol, snapshot_request.quote_exchange)
                        selected = float(snapshot_data.get("selected") or 0)
                        print(
                            f"  OK {phase}: price={selected:.4f}, changed={changed}, "
                            f"api={snapshot_elapsed_ms / 1000:.2f}s"
                        )
                        fallback_saved = True
                        break
                    fallback_errors.append(f"{phase}: {snapshot_message}")
                if not fallback_saved:
                    fallback_detail = "; ".join(fallback_errors) or "no snapshot attempt returned a usable result"
                    combined_message = f"{combined_historical_message}; Snapshot fallback failed: {fallback_detail}"
                    mark_item(db, request.item_id, "failed", message=combined_message, error=combined_message)
                    print(f"  FAILED: {combined_message}")
            elif not bars:
                mark_item(db, request.item_id, "failed", message=message, error="No bars returned")
                print("  FAILED: no bars returned")
            else:
                changed = save_bars(db, request.symbol, bars)
                save_working_quote_exchange(db, request.symbol, historical_request.quote_exchange)
                print(
                    f"  OK historical-{historical_request.quote_exchange}: "
                    f"rows={len(bars)}, changed={changed}, api={elapsed_ms / 1000:.2f}s"
                )
                if not has_expected_quote(db, request.symbol):
                    print("  Historical data is still stale, trying snapshot fallback ...")
                    fallback_saved = False
                    fallback_errors: list[str] = []
                    for phase, snapshot_request in snapshot_attempts(historical_request):
                        print(f"  Trying {phase} ...")
                        snapshot_success, snapshot_message, snapshot_data, snapshot_elapsed_ms = call_snapshot_helper(
                            args,
                            snapshot_request,
                        )
                        append_timing_log(
                            timing_log,
                            snapshot_request,
                            snapshot_elapsed_ms,
                            snapshot_success,
                            snapshot_message,
                            phase=phase,
                        )
                        if snapshot_success:
                            snapshot_changed = save_snapshot_quote(db, request.symbol, snapshot_data)
                            if has_expected_quote(db, request.symbol):
                                changed += snapshot_changed
                                mark_item(
                                    db,
                                    request.item_id,
                                    "success",
                                    changed=changed,
                                    rows_received=len(bars) + 1,
                                    message=f"{message} {phase}: {snapshot_message}",
                                )
                                save_working_quote_exchange(db, request.symbol, snapshot_request.quote_exchange)
                                selected = float(snapshot_data.get("selected") or 0)
                                print(
                                    f"  OK {phase}: price={selected:.4f}, changed={snapshot_changed}, "
                                    f"api={snapshot_elapsed_ms / 1000:.2f}s"
                                )
                                fallback_saved = True
                                break
                            fallback_errors.append(f"{phase}: Snapshot did not create expected quote")
                            continue
                        fallback_errors.append(f"{phase}: {snapshot_message}")
                    if not fallback_saved:
                        fallback_detail = "; ".join(fallback_errors) or "no snapshot attempt returned a usable result"
                        combined_message = (
                            f"{message}; Historical data is still stale; "
                            f"Snapshot fallback failed: {fallback_detail}"
                        )
                        mark_item(
                            db,
                            request.item_id,
                            "failed",
                            changed=changed,
                            rows_received=len(bars),
                            message=combined_message,
                            error=combined_message,
                        )
                        print(f"  FAILED: {combined_message}")
                else:
                    mark_item(
                        db,
                        request.item_id,
                        "success",
                        changed=changed,
                        rows_received=len(bars),
                        message=message,
                    )
        except KeyboardInterrupt:
            mark_item(db, request.item_id, "pending", error="Interrupted")
            mark_job_status(db, job_id, "paused", "Interrupted")
            raise
        except Exception as exc:
            message = str(exc)
            mark_item(db, request.item_id, "failed", error=message)
            print(f"  FAILED: {message}")

        processed += 1
        if processed % args.status_every == 0:
            refresh_job_counts(db, job_id)
            print_job_status(db, job_id)
        if args.delay_seconds > 0:
            time.sleep(args.delay_seconds)

    refresh_job_counts(db, job_id)
    print_job_status(db, job_id)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run resumable ShareSelector IBKR quote jobs.")
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument("--all-ibkr", action="store_true", help="Create a job for all IBKR-backed stocks.")
    scope.add_argument(
        "--stale-ibkr",
        action="store_true",
        help="Create a job only for IBKR stocks older than the previous trading day or without quotes.",
    )
    scope.add_argument("--symbol-file", type=Path, help="Create a job from a newline-separated symbol file.")
    parser.add_argument("--resume-job", type=int, help="Resume an existing job id.")
    parser.add_argument("--status", type=int, metavar="JOB_ID", help="Print job status and exit.")
    parser.add_argument("--create-only", action="store_true", help="Create the job but do not process it yet.")
    parser.add_argument("--job-name", default="", help="Name for a newly created job.")
    parser.add_argument("--helper", type=Path, default=Path(DEFAULT_HELPER))
    parser.add_argument("--ibkr-host", default="127.0.0.1")
    parser.add_argument("--ibkr-port", type=int, default=7496)
    parser.add_argument("--client-id", type=int, default=240)
    parser.add_argument("--timeout-seconds", type=int, default=90)
    parser.add_argument("--snapshot-timeout-seconds", type=int, default=5)
    parser.add_argument("--delay-seconds", type=float, default=0.2)
    parser.add_argument(
        "--include-current",
        action="store_true",
        help="Also refresh symbols that already have a quote for the expected trading day.",
    )
    parser.add_argument("--limit", type=int, default=0, help="Process at most N items, then pause.")
    parser.add_argument("--status-every", type=int, default=25)
    parser.add_argument("--timing-log", default="ibkr_quote_worker_timings.csv")
    parser.add_argument("--psql", default="psql")
    parser.add_argument("--db-host", default=DEFAULT_DB_HOST)
    parser.add_argument("--db-name", default=DEFAULT_DB_NAME)
    parser.add_argument("--db-user", default=DEFAULT_DB_USER)
    parser.add_argument("--db-password", default=DEFAULT_DB_PASSWORD)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    db = DbConfig(
        host=args.db_host,
        database=args.db_name,
        user=args.db_user,
        password=args.db_password,
        psql=args.psql,
    )
    ensure_schema(db)

    if args.status is not None:
        print_job_status(db, args.status)
        return 0

    if args.resume_job:
        job_id = args.resume_job
    else:
        if not args.all_ibkr and not args.stale_ibkr and not args.symbol_file:
            print("Use --all-ibkr, --stale-ibkr, --symbol-file, --resume-job, or --status.", file=sys.stderr)
            return 2
        symbols = read_symbol_file(args.symbol_file) if args.symbol_file else None
        scope = "symbol_file" if symbols is not None else ("stale_ibkr" if args.stale_ibkr else "all_ibkr")
        name = args.job_name or f"IBKR quote job {datetime.now().strftime('%Y-%m-%d %H:%M')}"
        job_id = create_job(db, name, scope, symbols)
        print(f"Created job {job_id}")
        print_job_status(db, job_id)
        if args.create_only:
            return 0

    if not args.helper.exists():
        print(f"Helper not found: {args.helper}", file=sys.stderr)
        return 2

    try:
        run_worker(args, db, job_id)
    except KeyboardInterrupt:
        print("Paused by Ctrl+C")
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
