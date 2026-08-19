param(
    [int]$Limit = 0,
    [int]$Skip = 0,
    [int]$Days = 90,
    [switch]$IncludeSnapshot,
    [switch]$RetryExisting,
    [string]$BaseDir = 'K:\QT-Projekte\ShareSelector\best_strategies',
    [string]$Helper = 'K:\QT-Projekte\Desktop_Qt_6_10_1_MSVC2022_64bit-Debug\ibkr-helper\IbkrHelper.exe',
    [string]$OutputCsv = 'K:\QT-Projekte\ShareSelector\best_strategies\missing_ibkr_history_probe_results.csv',
    [string]$HostName = '127.0.0.1',
    [int]$Port = 7496
)

$ErrorActionPreference = 'Stop'

function Add-CsvRow {
    param(
        [string]$Path,
        [pscustomobject]$Row
    )

    $exists = Test-Path -LiteralPath $Path
    $Row | Export-Csv -LiteralPath $Path -NoTypeInformation -Append:$exists -Force
}

function Parse-HistoricalResult {
    param(
        [string]$Raw,
        [double]$Seconds
    )

    try {
        $json = $Raw | ConvertFrom-Json
        $rows = 0
        $first = ''
        $last = ''
        $lastClose = ''
        if ($json.data) {
            $dataRows = @($json.data)
            $rows = $dataRows.Count
            if ($rows -gt 0) {
                $ordered = @($dataRows | Sort-Object date)
                $first = [string]$ordered[0].date
                $last = [string]$ordered[-1].date
                $lastClose = [string]$ordered[-1].close
            }
        }

        return [pscustomobject]@{
            ParseOk = $true
            Success = [bool]$json.success
            Rows = $rows
            FirstDate = $first
            LastDate = $last
            LastClose = $lastClose
            Selected = ''
            Message = [string]$json.message
            Seconds = $Seconds
        }
    } catch {
        return [pscustomobject]@{
            ParseOk = $false
            Success = $false
            Rows = 0
            FirstDate = ''
            LastDate = ''
            LastClose = ''
            Selected = ''
            Message = "JSON parse failed: $Raw"
            Seconds = $Seconds
        }
    }
}

function Parse-SnapshotResult {
    param(
        [string]$Raw,
        [double]$Seconds
    )

    try {
        $json = $Raw | ConvertFrom-Json
        $data = $json.data
        return [pscustomobject]@{
            ParseOk = $true
            Success = [bool]$json.success
            Rows = 0
            FirstDate = ''
            LastDate = ''
            LastClose = ''
            Selected = [string]$data.selected
            Message = [string]$json.message
            Seconds = $Seconds
        }
    } catch {
        return [pscustomobject]@{
            ParseOk = $false
            Success = $false
            Rows = 0
            FirstDate = ''
            LastDate = ''
            LastClose = ''
            Selected = ''
            Message = "JSON parse failed: $Raw"
            Seconds = $Seconds
        }
    }
}

function Invoke-Helper {
    param(
        [string]$IbkrSymbol,
        [string]$ConId,
        [string]$Currency,
        [string]$Exchange,
        [bool]$DirectExchange,
        [string]$Mode
    )

    $args = @(
        '--host', $HostName,
        '--port', [string]$Port,
        '--client-id', [string](10000 + (Get-Random -Minimum 1 -Maximum 900)),
        '--symbol', $IbkrSymbol,
        '--con-id', $ConId,
        '--currency', $Currency,
        '--exchange', $Exchange
    )
    if ($Mode -eq 'historical') {
        $args += @('--historical-quotes', '--days', [string]$Days)
    } else {
        $args += '--market-snapshot'
    }
    if ($DirectExchange) {
        $args += '--direct-exchange'
    }

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $raw = & $Helper @args 2>$null
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    $sw.Stop()
    $seconds = [math]::Round($sw.Elapsed.TotalSeconds, 3)

    if ($Mode -eq 'historical') {
        return Parse-HistoricalResult -Raw $raw -Seconds $seconds
    }
    return Parse-SnapshotResult -Raw $raw -Seconds $seconds
}

if (-not (Test-Path -LiteralPath $Helper)) {
    throw "IBKR helper not found: $Helper"
}

$sourcePath = Join-Path $BaseDir 'ibkr_stocks_with_quotes_isin.csv'
$coveragePath = Join-Path $BaseDir 'ibkr_history_coverage.csv'
$resolvedPath = Join-Path $BaseDir 'ibkr_isin_resolved.csv'
$errorPath = Join-Path $BaseDir 'ibkr_isin_errors.csv'

$source = Import-Csv -LiteralPath $sourcePath
$coverageSet = [System.Collections.Generic.HashSet[string]]::new()
Import-Csv -LiteralPath $coveragePath | ForEach-Object { [void]$coverageSet.Add($_.ISIN) }

$resolvedByIsin = @{}
Import-Csv -LiteralPath $resolvedPath | ForEach-Object { $resolvedByIsin[$_.ISIN] = $_ }

$errorByIsin = @{}
Import-Csv -LiteralPath $errorPath | ForEach-Object { $errorByIsin[$_.ISIN] = $_ }

$alreadyDone = [System.Collections.Generic.HashSet[string]]::new()
if (-not $RetryExisting -and (Test-Path -LiteralPath $OutputCsv)) {
    Import-Csv -LiteralPath $OutputCsv | ForEach-Object { [void]$alreadyDone.Add($_.ISIN) }
}

$missing = @($source | Where-Object { $_.ISIN -and -not $coverageSet.Contains($_.ISIN) } | Select-Object -ExpandProperty ISIN)
if ($Skip -gt 0) {
    $missing = @($missing | Select-Object -Skip $Skip)
}
if ($Limit -gt 0) {
    $missing = @($missing | Select-Object -First $Limit)
}

$index = 0
foreach ($isin in $missing) {
    $index++
    if ($alreadyDone.Contains($isin)) {
        continue
    }

    $resolved = $resolvedByIsin[$isin]
    $resolveError = if ($errorByIsin.ContainsKey($isin)) { [string]$errorByIsin[$isin].Error } else { '' }
    if (-not $resolved -or $resolved.Status -eq 'NOT_FOUND' -or -not $resolved.conId) {
        Add-CsvRow -Path $OutputCsv -Row ([pscustomobject]@{
            RunAt = (Get-Date).ToString('s')
            ISIN = $isin
            ResolvedStatus = if ($resolved) { [string]$resolved.Status } else { 'MISSING_RESOLVE_ROW' }
            ConId = ''
            Symbol = ''
            Currency = ''
            PrimaryExchange = ''
            TestStatus = 'NO_CONTRACT'
            BestMode = ''
            BestExchange = ''
            Rows = 0
            FirstDate = ''
            LastDate = ''
            LastClose = ''
            SnapshotSelected = ''
            Seconds = 0
            Message = $resolveError
        })
        Write-Host "[$index/$($missing.Count)] $isin -> NO_CONTRACT"
        continue
    }

    $ibkrSymbol = if ($resolved.Symbol) { [string]$resolved.Symbol } else { [string]$resolved.LocalSymbol }
    $currency = [string]$resolved.Currency
    $primary = [string]$resolved.PrimaryExchange
    $exchanges = @('SMART')
    if ($primary -and $primary -ne 'SMART') {
        $exchanges += $primary
    }
    $exchanges = @($exchanges | Select-Object -Unique)

    $best = $null
    $testedMessages = @()

    foreach ($exchange in $exchanges) {
        $direct = $exchange -ne 'SMART'
        $result = Invoke-Helper -IbkrSymbol $ibkrSymbol -ConId $resolved.conId -Currency $currency -Exchange $exchange -DirectExchange:$direct -Mode 'historical'
        $testedMessages += "$exchange historical: $($result.Message)"
        if ($result.Success -and $result.Rows -gt 0) {
            $best = [pscustomobject]@{
                TestStatus = 'HISTORICAL_OK'
                BestMode = 'historical'
                BestExchange = $exchange
                Result = $result
            }
            break
        }
    }

    if (-not $best -and $IncludeSnapshot) {
        foreach ($exchange in $exchanges) {
            $direct = $exchange -ne 'SMART'
            $result = Invoke-Helper -IbkrSymbol $ibkrSymbol -ConId $resolved.conId -Currency $currency -Exchange $exchange -DirectExchange:$direct -Mode 'snapshot'
            $testedMessages += "$exchange snapshot: $($result.Message)"
            if ($result.Success -and $result.Selected) {
                $best = [pscustomobject]@{
                    TestStatus = 'SNAPSHOT_OK'
                    BestMode = 'snapshot'
                    BestExchange = $exchange
                    Result = $result
                }
                break
            }
        }
    }

    if ($best) {
        $result = $best.Result
        $row = [pscustomobject]@{
            RunAt = (Get-Date).ToString('s')
            ISIN = $isin
            ResolvedStatus = [string]$resolved.Status
            ConId = [string]$resolved.conId
            Symbol = $ibkrSymbol
            Currency = $currency
            PrimaryExchange = $primary
            TestStatus = $best.TestStatus
            BestMode = $best.BestMode
            BestExchange = $best.BestExchange
            Rows = $result.Rows
            FirstDate = $result.FirstDate
            LastDate = $result.LastDate
            LastClose = $result.LastClose
            SnapshotSelected = $result.Selected
            Seconds = $result.Seconds
            Message = $result.Message
        }
    } else {
        $row = [pscustomobject]@{
            RunAt = (Get-Date).ToString('s')
            ISIN = $isin
            ResolvedStatus = [string]$resolved.Status
            ConId = [string]$resolved.conId
            Symbol = $ibkrSymbol
            Currency = $currency
            PrimaryExchange = $primary
            TestStatus = 'NO_QUOTES'
            BestMode = ''
            BestExchange = ''
            Rows = 0
            FirstDate = ''
            LastDate = ''
            LastClose = ''
            SnapshotSelected = ''
            Seconds = 0
            Message = ($testedMessages -join ' | ')
        }
    }

    Add-CsvRow -Path $OutputCsv -Row $row
    Write-Host "[$index/$($missing.Count)] $isin -> $($row.TestStatus) $($row.BestExchange) rows=$($row.Rows) selected=$($row.SnapshotSelected)"
}

Write-Host "Results written to $OutputCsv"
