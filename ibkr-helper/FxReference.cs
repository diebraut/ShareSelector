using IBApi;
using System.Globalization;

internal sealed partial class ContractDetailsWrapper
{
    private readonly List<HistoricalBar> fxHistory = [];
    private TaskCompletionSource<bool> fxHistoryReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int fxHistoryRequestId = -1;

    private async Task<(MarketReference? reference, HistoricalBar[] bars)> ConvertReferenceToEurAsync(
        MarketReference reference)
    {
        var currency = reference.currency.Trim().ToUpperInvariant();
        if (currency == "EUR") return (reference, []);
        if (currency.Length != 3
            || !DateOnly.TryParseExact(reference.lastDate, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                DateTimeStyles.None, out var lastDate)
            || !DateOnly.TryParseExact(reference.closeDate, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                DateTimeStyles.None, out var closeDate)) {
            Console.Error.WriteLine($"FX conversion skipped for {reference.symbol}: foreign quote dates are not known.");
            return (null, []);
        }

        var rates = await RequestFxRatesAsync(currency, "EUR", false, requestId + 40);
        var fxPair = $"{currency}/EUR";
        if (!HasRatesForDates(rates, lastDate, closeDate)) {
            rates = await RequestFxRatesAsync("EUR", currency, true, requestId + 41);
            fxPair = $"EUR/{currency} inverted";
        }
        if (!HasRatesForDates(rates, lastDate, closeDate)) {
            Console.Error.WriteLine($"FX conversion skipped for {reference.symbol}: no matching {currency}/EUR rates for {lastDate:yyyy-MM-dd} and {closeDate:yyyy-MM-dd}.");
            return (null, []);
        }

        var lastRate = rates[lastDate];
        var closeRate = rates[closeDate];
        // LSE stock prices are quoted in pence although IBKR identifies the
        // contract currency as GBP.  Convert the quote unit to pounds before
        // applying GBP/EUR, otherwise the EUR value is too large by 100.
        var quoteUnitScale = IsLondonPenceQuote(reference) ? 0.01 : 1.0;
        var eurLast = reference.last * quoteUnitScale * lastRate;
        var eurClose = reference.close * quoteUnitScale * closeRate;
        if (!double.IsFinite(eurLast) || eurLast <= 0 || !double.IsFinite(eurClose) || eurClose <= 0)
            return (null, []);

        var source = $"fx:{currency}:IBKR:{fxPair}" + (quoteUnitScale == 0.01 ? ":GBX" : string.Empty);
        Console.Error.WriteLine($"FX conversion {reference.symbol}: {reference.last} {currency} * {quoteUnitScale} quote-unit * {lastRate} = {eurLast} EUR ({lastDate:yyyy-MM-dd}).");
        var bars = new[] {
            new HistoricalBar(closeDate.ToString("yyyy-MM-dd"), eurClose, eurClose, eurClose, eurClose, 0, source),
            new HistoricalBar(lastDate.ToString("yyyy-MM-dd"), eurLast, eurLast, eurLast, eurLast, 0, source)
        };
        return (new MarketReference(eurLast, eurClose, "EUR", reference.conId, reference.symbol,
            reference.primaryExchange, reference.lastTimestamp, reference.delayed, "historical",
            reference.lastDate, reference.closeDate, source), bars);
    }

    private static bool IsLondonPenceQuote(MarketReference reference)
    {
        if (!string.Equals(reference.currency, "GBP", StringComparison.OrdinalIgnoreCase))
            return false;
        return string.Equals(reference.primaryExchange, "LSE", StringComparison.OrdinalIgnoreCase)
            || string.Equals(reference.exchange, "LSE", StringComparison.OrdinalIgnoreCase);
    }

    private static bool HasRatesForDates(IReadOnlyDictionary<DateOnly, double> rates,
                                         DateOnly lastDate,
                                         DateOnly closeDate)
    {
        return rates.TryGetValue(lastDate, out var lastRate) && double.IsFinite(lastRate) && lastRate > 0
            && rates.TryGetValue(closeDate, out var closeRate) && double.IsFinite(closeRate) && closeRate > 0;
    }

    private async Task<Dictionary<DateOnly, double>> RequestFxRatesAsync(string symbol,
                                                                         string currency,
                                                                         bool invert,
                                                                         int request)
    {
        fxHistoryRequestId = request;
        fxHistory.Clear();
        fxHistoryReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
        var contract = new Contract {
            Symbol = symbol,
            Currency = currency,
            SecType = "CASH",
            Exchange = "IDEALPRO"
        };
        try {
            Client!.reqHistoricalData(request, contract,
                DateTime.UtcNow.ToString("yyyyMMdd-HH:mm:ss", CultureInfo.InvariantCulture),
                "1 M", "1 day", "MIDPOINT", 1, 1, false, []);
            if (!await fxHistoryReady.Task.WaitAsync(TimeSpan.FromSeconds(4))) return [];
            return fxHistory
                .Where(bar => DateOnly.TryParseExact(bar.date, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                    DateTimeStyles.None, out _) && double.IsFinite(bar.close) && bar.close > 0)
                .GroupBy(bar => DateOnly.ParseExact(bar.date, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                    DateTimeStyles.None))
                .ToDictionary(group => group.Key,
                    group => invert ? 1.0 / group.Last().close : group.Last().close);
        } catch (Exception exception) {
            Console.Error.WriteLine($"FX history {symbol}/{currency} unavailable: {exception.Message}");
            return [];
        } finally {
            if (!fxHistoryReady.Task.IsCompleted) Client?.cancelHistoricalData(request);
        }
    }
}
