param([string]$Psql = 'E:\db\bin\psql.exe', [string]$Database = 'TotalStocks')
$ErrorActionPreference = 'Stop'
# Uses the application's actual SQL in temporary tables. No production writes.
$source = Get-Content -LiteralPath "$PSScriptRoot/../databasemanager_ibkr.cpp" -Raw
$history = $source.Substring($source.IndexOf('bool DatabaseManager::saveIbkrHistoricalQuotes'))
$snapshot = $source.Substring($source.IndexOf('bool DatabaseManager::saveIbkrQuoteSnapshot'))
function Sql([string]$section, [string]$variable, [hashtable]$values) {
    $match = [regex]::Match($section, [regex]::Escape($variable) + '\.prepare\(R"SQL\((.*?)\)SQL"\);', 'Singleline')
    if (!$match.Success) { throw "SQL not found: $variable" }
    return [regex]::Replace($match.Groups[1].Value, '(?<!:):(\w+)', {
        param($parameter)
        $key = $parameter.Groups[1].Value
        if (!$values.ContainsKey($key)) { throw "Missing parameter: $key" }
        return [string]$values[$key]
    }) + ';'
}
$values = @{ symbol="'SNAPSHOT-TEST'"; closeDate="DATE '2026-09-10'"; closePrice='14.550';
    openPrice='15'; highestPrice='16'; lowestPrice='14'; volume='100';
    snapshotLast='13.525'; snapshotClose='14.585'; snapshotLastDate="DATE '2026-09-11'";
    snapshotCloseDate="DATE '2026-09-10'"; snapshotTimeZone="'MET'"; changeReference='NULL' }
$historicalInsert = Sql $history 'insertQuery' $values
$values.closePrice='13.525'
$todayInsert = Sql $snapshot 'insertQuery' $values
$snapshotUpdate = Sql $snapshot 'updateQuery' $values
$values.closePrice='14.585'
$finalInsert = Sql $snapshot 'finalCloseQuery' $values
$confirmation = Sql $snapshot 'confirmationQuery' $values
$values.closePrice='99'
$repeatInsert = Sql $snapshot 'finalCloseQuery' $values
$portfolioSource = Get-Content -LiteralPath "$PSScriptRoot/../databasemanager_portfolio.cpp" -Raw
$percent = [regex]::Match($portfolioSource, 'ROUND\(\(CASE WHEN s\."IBKRSnapshotLast" IS NOT NULL.*? AS latest_change_percent', 'Singleline').Value
if (!$percent) { throw 'Portfolio percentage expression not found' }
$sql = @"
BEGIN;
CREATE TEMP TABLE "Quotes" ("Symbol" text, "CloseDate" date, "ClosePrice" numeric,
 "OpenPrice" numeric, "HighestPrice" numeric, "LowestPrice" numeric, "Volume" numeric,
 "IBKRCloseSource" text, PRIMARY KEY ("Symbol", "CloseDate"));
CREATE TEMP TABLE "Stocks" ("Symbol" text PRIMARY KEY, "LastUpdateDate" date,
 "IBKRFinalCloseDate" date, "IBKRSnapshotLast" numeric, "IBKRSnapshotClose" numeric, "IBKRChangeReference" jsonb,
 "IBKRSnapshotLastDate" date, "IBKRSnapshotCloseDate" date, "IBKRSnapshotTimeZone" text);
INSERT INTO "Stocks" ("Symbol", "IBKRFinalCloseDate") VALUES ('SNAPSHOT-TEST', '2026-09-11');
$historicalInsert
$todayInsert
$snapshotUpdate
$finalInsert
$confirmation
$repeatInsert
$historicalInsert
DO `$`$
BEGIN
 IF (SELECT "ClosePrice" FROM "Quotes" WHERE "CloseDate"='2026-09-10') <> 14.585 THEN
   RAISE EXCEPTION 'Snapshot CLOSE lost to duplicate or history'; END IF;
 IF (SELECT "ClosePrice" FROM "Quotes" WHERE "CloseDate"=CURRENT_DATE) <> 13.525 THEN
   RAISE EXCEPTION 'Current quote changed'; END IF;
 IF (SELECT "IBKRFinalCloseDate" FROM "Stocks") <> DATE '2026-09-10' THEN
   RAISE EXCEPTION 'Wrong confirmation date'; END IF;
 IF (SELECT round(("IBKRSnapshotLast"-"IBKRSnapshotClose")/"IBKRSnapshotClose"*100,2) FROM "Stocks") <> -7.27 THEN
   RAISE EXCEPTION 'Wrong percent'; END IF;
END `$`$;
SELECT 'PASS: snapshot pair, reference date, current quote, duplicate and historical overwrite protection';
UPDATE "Stocks" SET "IBKRSnapshotLast" = NULL,
 "IBKRChangeReference" = '{"last":63,"close":60,"currency":"USD"}';
CREATE TEMP TABLE reference_percent AS SELECT $percent FROM "Stocks" s;
DO `$`$
BEGIN
 IF (SELECT latest_change_percent FROM reference_percent) IS DISTINCT FROM 5.00 THEN
   RAISE EXCEPTION 'USD percentage used EUR CLOSE'; END IF;
 IF (SELECT "ClosePrice" FROM "Quotes" WHERE "CloseDate"=CURRENT_DATE) <> 13.525 THEN
   RAISE EXCEPTION 'USD reference overwrote EUR quote'; END IF;
END `$`$;
UPDATE "Stocks" SET "IBKRSnapshotLast" = 13.525;
DELETE FROM reference_percent;
INSERT INTO reference_percent SELECT $percent FROM "Stocks" s;
DO `$`$
BEGIN
 IF (SELECT latest_change_percent FROM reference_percent) IS DISTINCT FROM -7.27 THEN
   RAISE EXCEPTION 'USD reference overrode native snapshot'; END IF;
END `$`$;
SELECT 'PASS: USD reference uses a same-currency pair; native LAST takes precedence; EUR quotes unchanged';
ROLLBACK;
"@
$sql | & $Psql -h localhost -U postgres -d $Database -X -q -v ON_ERROR_STOP=1
if ($LASTEXITCODE -ne 0) { throw 'Snapshot SQL regression failed' }
