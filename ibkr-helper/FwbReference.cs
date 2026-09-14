using IBApi;
using System.Globalization;

internal sealed partial class ContractDetailsWrapper
{
    public string ReferenceExchanges { get; set; } = string.Empty;
    public string PreferredQuoteExchange { get; set; } = string.Empty;
    private readonly List<HistoricalBar> fwbHistory = [];
    private TaskCompletionSource<bool> fwbHistoryReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int nativeHistoryRequestId = -1;
    private int nativeScheduleRequestId = -1;
    private TaskCompletionSource<HistoricalSession[]> nativeScheduleReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private HistoricalBar[] fwbCompletedBars = [];

    public void StartSnapshotDeadline()
    {
        _ = Task.Run(async () => {
            await Task.Delay(3000);
            CompleteSnapshot();
        });
    }

    private async Task<MarketReference?> RequestFwbReferenceAsync(bool preferredOnly)
    {
        if (requestedConId <= 0) return null;
        var exchanges = HistoricalExchangeOrder.Select(PreferredQuoteExchange, ReferenceExchanges)
            .Where(exchange => string.Equals(exchange, PreferredQuoteExchange.Trim(), StringComparison.OrdinalIgnoreCase) == preferredOnly)
            .ToArray();
        for (var index = 0; index < exchanges.Length; ++index) {
            var exchange = exchanges[index];
            lock (snapshotLock) {
                nativeHistoryRequestId = requestId + (preferredOnly ? 5 : 7) + index;
                nativeScheduleRequestId = requestId + (preferredOnly ? 20 : 22) + index;
                fwbHistory.Clear();
                fwbHistoryReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
                nativeScheduleReady = new(TaskCreationOptions.RunContinuationsAsynchronously);
            }
            var reference = await RequestNativeHistoryAsync(exchange);
            if (reference != null) return reference;
        }
        return null;
    }

    private async Task<MarketReference?> RequestNativeHistoryAsync(string exchange)
    {
        try {
            var zone = TimeZoneInfo.FindSystemTimeZoneById("Europe/Berlin");
            var cutoff = DateTime.UtcNow;
            var localNow = TimeZoneInfo.ConvertTimeFromUtc(cutoff, zone);
            var today = DateOnly.FromDateTime(localNow);
            var contract = CreateCurrentContract(exchange);
            Console.Error.WriteLine($"EUR snapshot incomplete; requesting historical {exchange}/EUR daily bars.");
            Client!.reqHistoricalData(nativeHistoryRequestId, contract, cutoff.ToString("yyyyMMdd-HH:mm:ss", CultureInfo.InvariantCulture),
                "1 M", "1 day", "TRADES", 1, 1, false, []);
            // Request the venue calendar alongside prices. Missing trades must not
            // turn two old bars into the latest two trading days (holidays included).
            Client.reqHistoricalData(nativeScheduleRequestId, contract, cutoff.ToString("yyyyMMdd-HH:mm:ss", CultureInfo.InvariantCulture),
                "1 M", "1 day", "SCHEDULE", 1, 1, false, []);
            await Task.WhenAll(fwbHistoryReady.Task, nativeScheduleReady.Task).WaitAsync(TimeSpan.FromSeconds(3));
            if (!await fwbHistoryReady.Task) return null;
            var expectedDates = (await nativeScheduleReady.Task)
                .Where(session => DateTime.TryParseExact(session.StartDateTime,
                    new[] { "yyyyMMdd-HH:mm:ss", "yyyyMMdd HH:mm:ss" }, CultureInfo.InvariantCulture,
                    DateTimeStyles.None, out var start) && start <= localNow)
                .Select(session => DateOnly.TryParseExact(session.RefDate, "yyyyMMdd", CultureInfo.InvariantCulture,
                    DateTimeStyles.None, out var day) ? day : default)
                .Where(day => day != default && day <= today)
                .Distinct().OrderByDescending(day => day).Take(2)
                .Select(day => day.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture)).ToArray();
            HistoricalBar[] received;
            lock (snapshotLock) { received = fwbHistory.ToArray(); }
            var bars = received.Where(bar => DateOnly.TryParseExact(bar.date, "yyyy-MM-dd", out var date)
                    && date <= today && double.IsFinite(bar.close) && bar.close > 0)
                .DistinctBy(bar => bar.date).OrderByDescending(bar => bar.date).Take(2).ToArray();
            if (expectedDates.Length != 2 || bars.Length != 2
                || bars[0].date != expectedDates[0] || bars[1].date != expectedDates[1]) {
                Console.Error.WriteLine($"{exchange} history rejected: expected latest trading days including the current session [{string.Join(", ", expectedDates)}], received [{string.Join(", ", bars.Select(bar => bar.date))}]; trying next fallback.");
                return null;
            }
            fwbCompletedBars = bars;
            return new(bars[0].close, bars[1].close, "EUR", requestedConId, requestedSymbol,
                requestedPrimaryExchange, null, false, "historical", bars[0].date, bars[1].date, exchange);
        } catch (Exception exception) {
            Console.Error.WriteLine($"{exchange} history unavailable: {exception.Message}");
            return null;
        } finally {
            if (!fwbHistoryReady.Task.IsCompleted) Client?.cancelHistoricalData(nativeHistoryRequestId);
            if (!nativeScheduleReady.Task.IsCompleted) Client?.cancelHistoricalData(nativeScheduleRequestId);
        }
    }
}
