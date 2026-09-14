using IBApi;
using System.Globalization;

internal sealed record MarketReference(double last, double close, string currency,
    int conId, string symbol, string primaryExchange, long? lastTimestamp, bool delayed,
    string source = "snapshot", string? lastDate = null, string? closeDate = null, string exchange = "SMART");

internal sealed partial class ContractDetailsWrapper
{
    public string ReferenceIsin { get; set; } = string.Empty;
    private string? homeReferenceError;
    private readonly List<ContractDetails> usdContracts = [];
    private readonly TaskCompletionSource usdContractsReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TaskCompletionSource usdSnapshotReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly object usdLock = new();
    private readonly Dictionary<int, double> usdPrices = [];
    private long? usdLastTimestamp;
    private int usdMarketDataType;
    private readonly List<(DateOnly date, double close)> usdHistory = [];
    private readonly TaskCompletionSource<bool> usdHistoryReady = new(TaskCreationOptions.RunContinuationsAsynchronously);

    private async Task<MarketReference?> RequestHomeHistoryAsync(Contract contract, string timeZone)
    {
        try {
            // Include the current session's available daily bar, not only final bars.
            var zone = HomeListingSelector.TimeZone(timeZone);
            var cutoff = DateTime.UtcNow;
            var today = DateOnly.FromDateTime(TimeZoneInfo.ConvertTimeFromUtc(cutoff, zone));
            Console.Error.WriteLine($"Home snapshot incomplete; requesting historical {contract.Exchange}/{contract.Currency} daily bars.");
            Client!.reqHistoricalData(requestId + 4, contract, cutoff.ToString("yyyyMMdd-HH:mm:ss", CultureInfo.InvariantCulture),
                "1 M", "1 day", "TRADES", 1, 1, false, []);
            if (!await usdHistoryReady.Task.WaitAsync(TimeSpan.FromSeconds(8))) {
                homeReferenceError ??= "Heimatboerse: historische Abfrage fehlgeschlagen";
                return null;
            }
            var bars = usdHistory.Where(bar => bar.date <= today && double.IsFinite(bar.close) && bar.close > 0)
                .DistinctBy(bar => bar.date).OrderByDescending(bar => bar.date).Take(2).ToArray();
            if (bars.Length != 2) {
                homeReferenceError = "Heimatboerse: keine zwei historischen Tageskurse geliefert";
                return null;
            }
            return new(bars[0].close, bars[1].close, contract.Currency, contract.ConId, contract.LocalSymbol,
                contract.PrimaryExch, null, false, "historical",
                bars[0].date.ToString("yyyy-MM-dd"), bars[1].date.ToString("yyyy-MM-dd"), contract.Exchange);
        } catch (Exception exception) {
            homeReferenceError = $"Heimatboersen-Historie: {exception.Message}";
            Console.Error.WriteLine($"Home history unavailable: {exception.Message}");
            return null;
        } finally {
            if (!usdHistoryReady.Task.IsCompleted)
                Client?.cancelHistoricalData(requestId + 4);
        }
    }

    private bool UseHistoryForUsdSubscriptionError(int code)
    {
        if (code is not (10089 or 10186 or 10168 or 354)) return false;
        Console.Error.WriteLine("Home market subscription unavailable; proceeding directly to historical reference.");
        usdSnapshotReady.TrySetResult();
        return true;
    }

    private void ReceiveUsdPrice(int field, double price)
    {
        if (!double.IsFinite(price) || price <= 0) return;
        lock (usdLock) {
            usdPrices[field] = price;
            if (usdPrices.GetValueOrDefault(4, usdPrices.GetValueOrDefault(68)) > 0
                && usdPrices.GetValueOrDefault(9, usdPrices.GetValueOrDefault(75)) > 0)
                usdSnapshotReady.TrySetResult();
        }
    }

    private async Task<MarketReference?> RequestHomeReferenceAsync()
    {
        if (string.IsNullOrWhiteSpace(ReferenceIsin) || Client == null) {
            homeReferenceError = "Heimatboersen-Abfrage nicht moeglich: ISIN oder Verbindung fehlt";
            return null;
        }
        try {
            Client.reqContractDetails(requestId + 2, new Contract {
                SecType = "STK", SecIdType = "ISIN", SecId = ReferenceIsin
            });
            await usdContractsReady.Task.WaitAsync(TimeSpan.FromSeconds(4));
            // Exact ISIN qualification, no symbol guessing or Class A/B substitution.
            var details = HomeListingSelector.Select(ReferenceIsin, usdContracts);
            if (details == null) {
                homeReferenceError = "Heimatboerse: kein eindeutiger Heimatvertrag fuer die ISIN gefunden";
                Console.Error.WriteLine("Home reference unavailable: no uniquely identifiable domestic listing for the ISIN.");
                return null;
            }
            var contract = details.Contract;
            contract.Exchange = ReferenceIsin.StartsWith("US", StringComparison.OrdinalIgnoreCase)
                && details.ValidExchanges.Split(',').Contains("SMART") ? "SMART" : contract.PrimaryExch;
            Console.Error.WriteLine($"Home listing: {contract.LocalSymbol}/{contract.PrimaryExch}/{contract.Currency}, conId={contract.ConId}, zone={details.TimeZoneId}.");
            Client.reqMarketDataType(2);
            Client.reqMktData(requestId + 3, contract, string.Empty, true, false, []);
            try { await usdSnapshotReady.Task.WaitAsync(TimeSpan.FromSeconds(3)); }
            catch (TimeoutException) { Console.Error.WriteLine("Home snapshot timed out; trying history."); }
            lock (usdLock) {
                var last = usdPrices.GetValueOrDefault(4, usdPrices.GetValueOrDefault(68));
                var close = usdPrices.GetValueOrDefault(9, usdPrices.GetValueOrDefault(75));
                if (last > 0 && close > 0)
                    return new(last, close, contract.Currency, contract.ConId, contract.LocalSymbol,
                        contract.PrimaryExch, usdLastTimestamp, usdMarketDataType is 3 or 4, exchange: contract.Exchange);
            }
            return await RequestHomeHistoryAsync(contract, details.TimeZoneId);
        } catch (Exception exception) {
            homeReferenceError = $"Heimatboersen-Abfrage: {exception.Message}";
            Console.Error.WriteLine($"Home reference unavailable: {exception.Message}");
        } finally {
            Client.cancelMktData(requestId + 3);
        }
        return null;
    }
}
