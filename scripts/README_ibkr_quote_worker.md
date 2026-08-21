# IBKR Quote Worker

Eigenstaendiger, resumable Python-Worker fuer ShareSelector-Quote-Batches.

Der Worker nutzt:

- `IbkrHelper.exe` fuer IBKR API Requests
- `psql` fuer PostgreSQL
- Datenbank `TotalStocks`, User `postgres`, Passwort `castell`
- Job-Tabellen `ibkr_quote_jobs` und `ibkr_quote_job_items`

## Gesamtlauf anlegen

```powershell
python .\scripts\run_ibkr_quote_job.py --all-ibkr --create-only --job-name "IBKR Gesamtlauf"
```

Die Ausgabe zeigt die Job-ID.

## Gesamtlauf starten

Direkt erstellen und starten:

```powershell
python .\scripts\run_ibkr_quote_job.py --all-ibkr --job-name "IBKR Gesamtlauf"
```

Der Worker ueberspringt standardmaessig Symbole, die inzwischen schon einen Quote fuer den
erwarteten Handelstag haben. Dadurch kann ShareSelector parallel einzelne Quotes aktualisieren;
der Worker laesst diese Symbole beim naechsten Check liegen.

Vorhandenen Job fortsetzen:

```powershell
python .\scripts\run_ibkr_quote_job.py --resume-job 1
```

## Fehlerliste starten

```powershell
python .\scripts\run_ibkr_quote_job.py --symbol-file K:\QT-Projekte\MemoryPackageBuilder\ibkr_failed_symbols_2026-08-16.txt --job-name "IBKR Fehler-Retry"
```

## Status pruefen

```powershell
python .\scripts\run_ibkr_quote_job.py --status 1
```

## Stoppen

Im Terminal `Ctrl+C` druecken. Der Job wird als `paused` markiert und kann spaeter mit
`--resume-job <id>` fortgesetzt werden.

## Testlauf begrenzen

```powershell
python .\scripts\run_ibkr_quote_job.py --all-ibkr --job-name "IBKR Smoke Test" --limit 5
```

## Erzwungener Refresh

Nur verwenden, wenn der Worker auch bereits aktuelle Symbole erneut abfragen soll:

```powershell
python .\scripts\run_ibkr_quote_job.py --all-ibkr --include-current --job-name "IBKR Forced Refresh"
```

## Hinweise

- IB Gateway/TWS muss laufen und die API muss erreichbar sein.
- Der Default-Port ist `7496`; bei Paper/anderer Gateway-Config `--ibkr-port` setzen.
- Der Worker arbeitet seriell. Das ist absichtlich konservativ wegen IBKR-Pacing.
- ShareSelector kann parallel laufen. Der Worker nutzt standardmaessig Client-ID `240`,
  die App nutzt andere Client-IDs.
- Bereits vorhandene identische Quote-Zeilen werden nicht als geaendert gezaehlt.
