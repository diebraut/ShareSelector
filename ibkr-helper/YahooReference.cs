using System.Globalization;
using System.Text.Json;

internal sealed partial class ContractDetailsWrapper
{
    public string YahooSymbol { get; set; } = string.Empty;
    public string ExpectedYahooQuoteDate { get; set; } = string.Empty;
    public string ResolvedYahooSymbol { get; private set; } = string.Empty;
    private string? yahooReferenceError;

    private async Task<(MarketReference? reference, HistoricalBar[] bars)> RequestYahooReferenceAsync()
    {
        try {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(6) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 ShareSelector/1.0");
            var errors = new List<string>();
            var attempted = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            var storedSymbol = YahooSymbol.Trim();
            if (!string.IsNullOrWhiteSpace(storedSymbol)) {
                attempted.Add(storedSymbol);
                var stored = await RequestYahooSymbolAsync(http, storedSymbol);
                if (stored.reference != null) {
                    ResolvedYahooSymbol = storedSymbol;
                    return (stored.reference, stored.bars);
                }
                if (!string.IsNullOrWhiteSpace(stored.error)) errors.Add(stored.error);
            }

            if (!string.IsNullOrWhiteSpace(ReferenceIsin)) {
                foreach (var candidate in await SearchYahooSymbolsByIsinAsync(http, ReferenceIsin)) {
                    if (!attempted.Add(candidate)) continue;
                    Console.Error.WriteLine($"Yahoo ISIN fallback: trying {candidate} for {ReferenceIsin}.");
                    var discovered = await RequestYahooSymbolAsync(http, candidate);
                    if (discovered.reference != null) {
                        ResolvedYahooSymbol = candidate;
                        return (discovered.reference, discovered.bars);
                    }
                    if (!string.IsNullOrWhiteSpace(discovered.error)) errors.Add(discovered.error);
                }
            }

            yahooReferenceError = errors.Count > 0
                ? string.Join("; ", errors)
                : "Yahoo-Fallback: keine passende Symbolzuordnung gefunden";
            return (null, []);
        } catch (Exception exception) {
            yahooReferenceError = $"Yahoo-Fallback: {exception.Message}";
            return (null, []);
        }
    }

    private static async Task<string[]> SearchYahooSymbolsByIsinAsync(HttpClient http, string isin)
    {
        try {
            var url = "https://query2.finance.yahoo.com/v1/finance/search"
                      + $"?q={Uri.EscapeDataString(isin)}&quotesCount=12&newsCount=0";
            using var response = await http.GetAsync(url);
            if (!response.IsSuccessStatusCode) return [];
            using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync());
            if (!document.RootElement.TryGetProperty("quotes", out var quotes)
                || quotes.ValueKind != JsonValueKind.Array) return [];

            return quotes.EnumerateArray()
                .Where(value => !value.TryGetProperty("quoteType", out var type)
                                || string.Equals(type.GetString(), "EQUITY", StringComparison.OrdinalIgnoreCase))
                .Select(value => value.TryGetProperty("symbol", out var symbol) ? symbol.GetString() ?? string.Empty : string.Empty)
                .Where(value => !string.IsNullOrWhiteSpace(value))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(YahooCandidateRank)
                .Take(6)
                .ToArray();
        } catch (Exception exception) {
            Console.Error.WriteLine($"Yahoo ISIN search failed for {isin}: {exception.Message}");
            return [];
        }
    }

    private static int YahooCandidateRank(string symbol)
    {
        var normalized = symbol.Trim().ToUpperInvariant();
        // German regional listings are useful as the first stored choice, but
        // after such a symbol failed the ISIN search should prefer the liquid
        // home listing returned by Yahoo.
        return normalized.EndsWith(".F", StringComparison.Ordinal)
               || normalized.EndsWith(".SG", StringComparison.Ordinal)
               || normalized.EndsWith(".MU", StringComparison.Ordinal)
               || normalized.EndsWith(".DU", StringComparison.Ordinal)
               || normalized.EndsWith(".BE", StringComparison.Ordinal)
            ? 1 : 0;
    }

    private async Task<(MarketReference? reference, HistoricalBar[] bars, string error)> RequestYahooSymbolAsync(
        HttpClient http, string symbol)
    {
        try {
            var url = $"https://query1.finance.yahoo.com/v8/finance/chart/{Uri.EscapeDataString(symbol)}?range=10d&interval=1d&events=div%2Csplits";
            using var response = await http.GetAsync(url);
            if (!response.IsSuccessStatusCode) {
                return (null, [], $"Yahoo-Fallback {symbol}: HTTP {(int)response.StatusCode}");
            }

            using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync());
            var chart = document.RootElement.GetProperty("chart");
            if (chart.GetProperty("result").ValueKind != JsonValueKind.Array
                || chart.GetProperty("result").GetArrayLength() == 0) {
                return (null, [], $"Yahoo-Fallback {symbol}: keine Kursdaten");
            }
            var result = chart.GetProperty("result")[0];
            var meta = result.GetProperty("meta");
            var currency = meta.TryGetProperty("currency", out var currencyValue)
                ? currencyValue.GetString() ?? string.Empty : string.Empty;
            var exchange = meta.TryGetProperty("exchangeName", out var exchangeValue)
                ? exchangeValue.GetString() ?? "YAHOO" : "YAHOO";
            var zoneName = meta.TryGetProperty("exchangeTimezoneName", out var zoneValue)
                ? zoneValue.GetString() ?? string.Empty : string.Empty;
            TimeZoneInfo zone;
            try { zone = HomeListingSelector.TimeZone(zoneName); }
            catch { zone = TimeZoneInfo.Utc; }
            var today = DateOnly.FromDateTime(TimeZoneInfo.ConvertTime(DateTimeOffset.UtcNow, zone).DateTime);

            var timestamps = result.GetProperty("timestamp").EnumerateArray().ToArray();
            var quote = result.GetProperty("indicators").GetProperty("quote")[0];
            var opens = quote.GetProperty("open").EnumerateArray().ToArray();
            var highs = quote.GetProperty("high").EnumerateArray().ToArray();
            var lows = quote.GetProperty("low").EnumerateArray().ToArray();
            var closes = quote.GetProperty("close").EnumerateArray().ToArray();
            var volumes = quote.GetProperty("volume").EnumerateArray().ToArray();
            var count = new[] { timestamps.Length, opens.Length, highs.Length, lows.Length, closes.Length, volumes.Length }.Min();
            var received = new List<HistoricalBar>();
            for (var index = 0; index < count; ++index) {
                if (!TryYahooNumber(closes[index], out var close) || close <= 0
                    || !timestamps[index].TryGetInt64(out var timestamp)) continue;
                var date = DateOnly.FromDateTime(TimeZoneInfo.ConvertTime(
                    DateTimeOffset.FromUnixTimeSeconds(timestamp), zone).DateTime);
                var open = TryYahooNumber(opens[index], out var openValue) && openValue > 0 ? openValue : close;
                var high = TryYahooNumber(highs[index], out var highValue) && highValue > 0 ? highValue : close;
                var low = TryYahooNumber(lows[index], out var lowValue) && lowValue > 0 ? lowValue : close;
                var volume = TryYahooNumber(volumes[index], out var volumeValue) && volumeValue >= 0 ? volumeValue : 0;
                received.Add(new(date.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture), open, high, low, close, volume));
            }
            var bars = received.DistinctBy(bar => bar.date).OrderByDescending(bar => bar.date).Take(2).ToArray();
            var minimumDate = DateOnly.TryParseExact(ExpectedYahooQuoteDate, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                                                     DateTimeStyles.None, out var expectedDate)
                ? expectedDate : today.AddDays(-7);
            var latestDateValid = bars.Length == 2
                && DateOnly.TryParseExact(bars[0].date, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                                          DateTimeStyles.None, out var latestDate)
                && latestDate <= today
                && latestDate >= minimumDate;
            if (!latestDateValid) {
                var latestText = bars.Length > 0 ? bars[0].date : "keiner";
                return (null, [], $"Yahoo-Fallback {symbol}: letzter Tagesbalken {latestText} ist nicht aktuell genug");
            }
            if (currency.Length != 3) {
                return (null, [], $"Yahoo-Fallback {symbol}: Kurswaehrung fehlt");
            }
            Console.Error.WriteLine($"Yahoo fallback: {symbol}/{exchange}/{currency}, {bars[0].date}={bars[0].close}, {bars[1].date}={bars[1].close}.");
            return (new(bars[0].close, bars[1].close, currency, 0, symbol, exchange,
                null, true, "historical", bars[0].date, bars[1].date, $"YAHOO:{symbol}"), bars, string.Empty);
        } catch (Exception exception) {
            return (null, [], $"Yahoo-Fallback {symbol}: {exception.Message}");
        }
    }

    private static bool TryYahooNumber(JsonElement value, out double number)
    {
        number = 0;
        return value.ValueKind == JsonValueKind.Number && value.TryGetDouble(out number)
            && double.IsFinite(number);
    }
}
