$ErrorActionPreference = 'Stop'

$apiKey = '2c7445a74b7f5ed6371d655f39ab4f4f'
$psql = 'E:\db\bin\psql.exe'
$env:PGPASSWORD = 'castell'
$outFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'marketstack_mapping_audit.csv'

function Normalize-Name([string]$value) {
    if ([string]::IsNullOrWhiteSpace($value)) { return @() }
    $text = $value.ToUpperInvariant()
    $text = $text -replace '\b(INTL|INT)\b', 'INTERNATIONAL'
    $text = $text -replace '[+.,;:/()\-\u0027]', ' '
    $text = $text -replace '\b(DK|DL|LS|ZY|EO|NK|NOK|SEK|DKK|GBP|GBX|USD|CAD|CHF)\s+[0-9].*$', ''
    $ignore = @(
        'INC','LTD','PLC','CORP','CORPORATION','AG','SA','SE','NV','SPA','S','A',
        'DL','LS','ZY','MR','GROUP','HOLDING','HOLDINGS','THE','AND','CO','COMPANY'
    )
    return ($text -split '\s+' |
        Where-Object { $_.Length -ge 2 -and $ignore -notcontains $_ })
}

function Name-Matches([string]$stockName, [string]$candidateName) {
    $stockTokens = @(Normalize-Name $stockName)
    $candidateTokens = @(Normalize-Name $candidateName)
    if ($stockTokens.Count -eq 0 -or $candidateTokens.Count -eq 0) { return $false }
    if ($candidateTokens -notcontains $stockTokens[0]) { return $false }

    $matches = 0
    foreach ($stockToken in $stockTokens) {
        foreach ($candidateToken in $candidateTokens) {
            if ($candidateToken -eq $stockToken -or
                ($stockToken.Length -ge 5 -and $candidateToken.StartsWith($stockToken.Substring(0, 5))) -or
                ($candidateToken.Length -ge 5 -and $stockToken.StartsWith($candidateToken.Substring(0, 5)))) {
                $matches++
                break
            }
        }
    }

    if ($stockTokens.Count -eq 1) { return $matches -ge 1 }
    if ($stockTokens[0].Length -ge 7) { return $matches -ge 1 }
    return $matches -ge 2
}

function Is-Otc([string]$exchange) {
    return @('OTCQ','OTCB','OTCM','PINC','PSGM','XOTC') -contains $exchange.ToUpperInvariant()
}

$sql = @'
SELECT "Symbol", "ISIN", "Name", "marketplace_sym", "marketplace_exchange"
FROM "Stocks"
WHERE COALESCE("from_IBKR", TRUE) = FALSE
  AND COALESCE("marketplace_sym", '') <> ''
ORDER BY "Symbol";
'@

$rows = @($sql | & $psql -d TotalStocks -U postgres -A -F "`t" -P footer=off)
if ($rows.Count -gt 0 -and $rows[0] -like 'Symbol*') {
    $rows = $rows | Select-Object -Skip 1
}

$results = New-Object System.Collections.Generic.List[object]
$index = 0
foreach ($line in $rows) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $index++
    $parts = $line -split "`t", 5
    $symbol = $parts[0]
    $isin = $parts[1]
    $name = $parts[2]
    $marketplaceSym = $parts[3]
    $exchange = $parts[4]
    $requestSymbol = ($marketplaceSym -split '/')[0]

    Write-Host "$index / $($rows.Count): $symbol -> $marketplaceSym"
    Start-Sleep -Milliseconds 650

    $candidateName = ''
    $candidateCountry = ''
    $errorText = ''
    try {
        $uri = "http://api.marketstack.com/v1/tickers?access_key=$apiKey&symbols=$([uri]::EscapeDataString($requestSymbol))&limit=10"
        $response = Invoke-RestMethod -Uri $uri -TimeoutSec 20
        $match = @($response.data | Where-Object {
            $_.symbol -eq $requestSymbol -and $_.stock_exchange.mic -eq $exchange
        } | Select-Object -First 1)
        if ($match.Count -eq 0) {
            $match = @($response.data | Where-Object { $_.symbol -eq $requestSymbol } | Select-Object -First 1)
        }
        if ($match.Count -gt 0) {
            $candidateName = [string]$match[0].name
            $candidateCountry = [string]$match[0].stock_exchange.country_code
        } else {
            $errorText = 'ticker_not_found'
        }
    } catch {
        $errorText = $_.Exception.Message
    }

    $stockCountry = if ($isin.Length -ge 2) { $isin.Substring(0, 2).ToUpperInvariant() } else { '' }
    $nameOk = Name-Matches $name $candidateName
    $countryOk = [string]::IsNullOrWhiteSpace($candidateCountry) -or
        [string]::IsNullOrWhiteSpace($stockCountry) -or
        $candidateCountry -eq $stockCountry -or
        (Is-Otc $exchange)
    $suspicious = -not $nameOk -or -not $countryOk -or -not [string]::IsNullOrWhiteSpace($errorText)

    $results.Add([pscustomobject]@{
        Symbol = $symbol
        ISIN = $isin
        Name = $name
        MarketplaceSym = $marketplaceSym
        MarketplaceExchange = $exchange
        MarketstackName = $candidateName
        MarketstackCountry = $candidateCountry
        NameOk = $nameOk
        CountryOk = $countryOk
        Suspicious = $suspicious
        Error = $errorText
    })
}

$results | Export-Csv -Path $outFile -NoTypeInformation -Encoding UTF8
Write-Host "Audit geschrieben: $outFile"
Write-Host "Verdacht: $(($results | Where-Object Suspicious).Count) / $($results.Count)"
