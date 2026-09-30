using System.Globalization;
using System.Text;
using System.Text.Json;
using IBApi;

internal sealed class LiveQuoteContract
{
    public string Symbol { get; set; } = "";
    public string IbkrSymbol { get; set; } = "";
    public int ConId { get; set; }
    public string Currency { get; set; } = "";
    public string Exchange { get; set; } = "";
    public string FallbackExchange { get; set; } = "";
    public string PrimaryExchange { get; set; } = "";

    public Contract ToContract() => new() {
        ConId = ConId,
        Symbol = IbkrSymbol,
        SecType = "STK",
        Currency = Currency,
        Exchange = Exchange,
        PrimaryExch = PrimaryExchange
    };
}

internal sealed class LiveQuoteState
{
    public required LiveQuoteContract Request { get; init; }
    public string TradingHours { get; set; } = "";
    public string TimeZoneId { get; set; } = "";
    public bool DetailsComplete { get; set; }
    public bool PendingDetails { get; set; }
    public bool Subscribed { get; set; }
    public bool NeedsCancel { get; set; }
    public DateTimeOffset RetryAfterUtc { get; set; }
    public double? Bid { get; set; }
    public double? Ask { get; set; }
    public DateTimeOffset? BidReceivedUtc { get; set; }
    public DateTimeOffset? AskReceivedUtc { get; set; }
    public DateTimeOffset? LastTradeUtc { get; set; }
    public int MarketDataType { get; set; }
    public string Error { get; set; } = "";
    public string Warning { get; set; } = "";
}

internal sealed class LiveQuoteWrapper : DefaultEWrapper
{
    private readonly object gate = new();
    private readonly List<LiveQuoteState> states;
    public TaskCompletionSource Connected { get; } =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public LiveQuoteWrapper(List<LiveQuoteState> states) => this.states = states;

    public override void nextValidId(int orderId) => Connected.TrySetResult();

    public override void contractDetails(int reqId, ContractDetails details)
    {
        var index = reqId - 10000;
        if (index < 0 || index >= states.Count)
            return;
        lock (gate) {
            states[index].TradingHours = details.TradingHours ?? "";
            states[index].TimeZoneId = details.TimeZoneId ?? "";
            states[index].Error = "";
        }
    }

    public override void contractDetailsEnd(int reqId)
    {
        var index = reqId - 10000;
        if (index < 0 || index >= states.Count)
            return;
        lock (gate) {
            states[index].DetailsComplete = true;
            if (string.IsNullOrEmpty(states[index].TradingHours))
                states[index].Error = "Keine Handelszeiten von IBKR";
        }
    }

    public override void tickPrice(int tickerId, int field, double price, TickAttrib attribs)
    {
        var index = tickerId - 20000;
        if (index < 0 || index >= states.Count || price <= 0)
            return;
        lock (gate) {
            var state = states[index];
            var now = DateTimeOffset.UtcNow;
            var tickDataType = field is 66 or 67 ? 3 : field is 1 or 2 ? 1 : 0;
            if (tickDataType == 0)
                return;
            if (state.MarketDataType != tickDataType
                && (tickDataType == 3 || state.MarketDataType is 0 or 3)) {
                state.Bid = null;
                state.Ask = null;
                state.BidReceivedUtc = null;
                state.AskReceivedUtc = null;
                state.MarketDataType = tickDataType;
            }
            if (field is 1 or 66) {
                state.Bid = price;
                state.BidReceivedUtc = now;
            } else if (field is 2 or 67) {
                state.Ask = price;
                state.AskReceivedUtc = now;
            }
        }
    }

    public override void tickString(int tickerId, int field, string value)
    {
        // 45 is the exchange's last-trade time. Bid/ask tickPrice has no such timestamp.
        var index = tickerId - 20000;
        if (field != 45 || index < 0 || index >= states.Count
            || !long.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var seconds)
            || seconds <= 0)
            return;
        try {
            var tradeTime = DateTimeOffset.FromUnixTimeSeconds(seconds);
            lock (gate) {
                if (states[index].LastTradeUtc is null || tradeTime > states[index].LastTradeUtc.Value)
                    states[index].LastTradeUtc = tradeTime;
            }
        } catch (ArgumentOutOfRangeException) {
            // Ignore an invalid timestamp from TWS.
        }
    }

    public override void marketDataType(int reqId, int marketDataType)
    {
        var index = reqId - 20000;
        if (index < 0 || index >= states.Count)
            return;
        lock (gate) {
            var state = states[index];
            if (state.MarketDataType != marketDataType) {
                state.Bid = null;
                state.Ask = null;
                state.BidReceivedUtc = null;
                state.AskReceivedUtc = null;
            }
            state.MarketDataType = marketDataType;
        }
    }

    public override void error(int id, long errorTime, int errorCode,
                               string errorMsg, string advancedOrderRejectJson)
    {
        // TWS reports 300 when a cancellation races with an already removed
        // subscription. It is not a quote error and must not replace the
        // original request error (or a valid quote received after rotation).
        if (errorCode is 300 or 2104 or 2106 or 2107 or 2108 or 2158)
            return;
        var tickerIndex = id - 20000;
        var detailsIndex = id - 10000;
        lock (gate) {
            if (tickerIndex >= 0 && tickerIndex < states.Count) {
                var state = states[tickerIndex];
                if (errorCode == 10090) {
                    // IBKR can still deliver usable bid/ask ticks after this warning.
                    state.Warning = "IBKR 10090: Teilabo";
                    return;
                }
                if (errorCode is 354 or 10168 or 10186) {
                    // TWS rejected the subscription, so there is nothing to cancel.
                    state.Subscribed = false;
                    state.NeedsCancel = false;
                    state.Bid = null;
                    state.Ask = null;
                    state.BidReceivedUtc = null;
                    state.AskReceivedUtc = null;
                    state.MarketDataType = 0;
                    if (state.Request.Exchange.Equals("SMART", StringComparison.OrdinalIgnoreCase)
                        && !string.IsNullOrWhiteSpace(state.Request.FallbackExchange)) {
                        state.Request.Exchange = state.Request.FallbackExchange;
                        state.TradingHours = "";
                        state.TimeZoneId = "";
                        state.DetailsComplete = false;
                        state.PendingDetails = true;
                        state.Error = "";
                        state.Warning = "";
                        state.RetryAfterUtc = DateTimeOffset.UtcNow;
                    } else {
                        state.Error = $"IBKR {errorCode}: Kein Echtzeit-Abo ({state.Request.Exchange})";
                        state.RetryAfterUtc = DateTimeOffset.UtcNow.AddMinutes(5);
                        Console.Error.WriteLine($"{state.Request.Symbol} {state.Request.Exchange}: IBKR {errorCode}: {errorMsg}");
                    }
                    return;
                }
                state.Error = $"IBKR {errorCode}: {errorMsg}";
                state.Subscribed = false;
                state.NeedsCancel = true;
                state.RetryAfterUtc = DateTimeOffset.UtcNow.AddMinutes(1);
                state.Bid = null;
                state.Ask = null;
                state.BidReceivedUtc = null;
                state.AskReceivedUtc = null;
            } else if (detailsIndex >= 0 && detailsIndex < states.Count) {
                states[detailsIndex].Error = $"IBKR {errorCode}: {errorMsg}";
                states[detailsIndex].DetailsComplete = true;
            } else {
                Console.Error.WriteLine($"IBKR {errorCode}: {errorMsg}");
            }
        }
    }

    public IReadOnlyList<(int Index, Contract Contract)> TakePendingContractDetails()
    {
        lock (gate) {
            var pending = new List<(int, Contract)>();
            for (var index = 0; index < states.Count; ++index) {
                if (!states[index].PendingDetails)
                    continue;
                states[index].PendingDetails = false;
                pending.Add((index, states[index].Request.ToContract()));
            }
            return pending;
        }
    }

    public void UpdateSubscriptions(EClientSocket client, DateTimeOffset now, ref int rotationCursor)
    {
        // Market-data lines are shared with TWS. Keep room for its watchlists.
        const int maxConcurrent = 40;
        var openIndexes = new List<int>();
        lock (gate) {
            for (var index = 0; index < states.Count; ++index)
                if (states[index].DetailsComplete
                    && LiveQuoteStream.IsTradingOpen(states[index].TradingHours,
                        states[index].TimeZoneId, now))
                    openIndexes.Add(index);
        }
        var desired = new HashSet<int>();
        if (openIndexes.Count > 0) {
            for (var offset = 0; offset < Math.Min(maxConcurrent, openIndexes.Count); ++offset)
                desired.Add(openIndexes[(rotationCursor + offset) % openIndexes.Count]);
            rotationCursor = (rotationCursor + maxConcurrent) % openIndexes.Count;
        }
        // Cancel the old group before requesting the next one. TWS releases
        // lines asynchronously, so wait before sending the replacement group.
        var canceledAny = false;
        for (var index = 0; index < states.Count; ++index) {
            bool cancel;
            lock (gate) {
                var state = states[index];
                cancel = state.NeedsCancel || (state.Subscribed && !desired.Contains(index));
                state.NeedsCancel = false;
                if (cancel) {
                    state.Subscribed = false;
                    if (!openIndexes.Contains(index)) {
                        state.Bid = null;
                        state.Ask = null;
                        state.BidReceivedUtc = null;
                        state.AskReceivedUtc = null;
                    }
                }
            }
            if (cancel) {
                client.cancelMktData(20000 + index);
                canceledAny = true;
            }
        }
        if (canceledAny)
            Thread.Sleep(2000);
        foreach (var index in desired) {
            bool subscribe;
            lock (gate) {
                var state = states[index];
                subscribe = !state.Subscribed && now >= state.RetryAfterUtc;
                if (subscribe) {
                    state.Subscribed = true;
                    state.Error = "";
                    state.Warning = "";
                    state.MarketDataType = 0;
                }
            }
            if (subscribe) {
                client.reqMktData(20000 + index, states[index].Request.ToContract(),
                                  "", false, false, []);
                Thread.Sleep(30);
            }
        }
    }

    public object Snapshot(DateTimeOffset now)
    {
        lock (gate) {
            return new {
                type = "quotes",
                receivedAtUtc = now.ToString("O"),
                rows = states.Select(state => {
                    var open = state.DetailsComplete
                        && LiveQuoteStream.IsTradingOpen(state.TradingHours, state.TimeZoneId, now);
                    var tradingDate = open
                        ? LiveQuoteStream.TradingDate(state.TimeZoneId, now) : null;
                    var bid = state.Bid;
                    var ask = state.Ask;
                    var fresh = open && (state.MarketDataType is 0 or 1)
                        && bid is > 0 && ask.HasValue && ask.Value >= bid.GetValueOrDefault()
                        && state.BidReceivedUtc.HasValue && state.AskReceivedUtc.HasValue
                        && Math.Abs((now - state.BidReceivedUtc.Value).TotalSeconds) <= 60
                        && Math.Abs((now - state.AskReceivedUtc.Value).TotalSeconds) <= 60
                        && Math.Abs((state.BidReceivedUtc.Value - state.AskReceivedUtc.Value).TotalSeconds) <= 60;
                    return new {
                        symbol = state.Request.Symbol,
                        exchange = state.Request.Exchange,
                        currency = state.Request.Currency,
                        tradingDate,
                        bid = open ? state.Bid : null,
                        ask = open ? state.Ask : null,
                        mid = fresh
                            ? (bid.GetValueOrDefault() + ask.GetValueOrDefault()) / 2.0 : (double?)null,
                        bidReceivedUtc = state.BidReceivedUtc?.ToString("O"),
                        askReceivedUtc = state.AskReceivedUtc?.ToString("O"),
                        lastTradeAtUtc = state.LastTradeUtc?.ToString("O"),
                        marketDataType = state.MarketDataType,
                        status = !string.IsNullOrEmpty(state.Error) ? state.Error
                            : !state.DetailsComplete ? "Handelszeiten werden geladen"
                            : !open ? "Handelsplatz geschlossen"
                            : !state.Subscribed ? "Wechselpause"
                            : state.MarketDataType is 2 or 4 ? "Eingefroren"
                            : state.MarketDataType == 3 ? "Verzögert"
                            : state.Bid is null || state.Ask is null
                                ? (string.IsNullOrEmpty(state.Warning) ? "Warte auf Geld/Brief" : state.Warning)
                            : !string.IsNullOrEmpty(state.Warning) ? "Aktiv (IBKR 10090)"
                            : "Aktiv"
                    };
                }).ToArray()
            };
        }
    }
}

internal static class LiveQuoteStream
{
    public static string? TradingDate(string zoneId, DateTimeOffset utcNow)
    {
        try {
            var zone = TimeZoneInfo.FindSystemTimeZoneById(zoneId == "MET"
                ? "W. Europe Standard Time" : zoneId);
            return TimeZoneInfo.ConvertTime(utcNow, zone).ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
        } catch (TimeZoneNotFoundException) {
            return null;
        }
    }

    // ContractDetails.TradingHours uses the venue's time zone and includes holidays.
    public static bool IsTradingOpen(string hours, string zoneId, DateTimeOffset utcNow)
    {
        if (string.IsNullOrWhiteSpace(hours) || string.IsNullOrWhiteSpace(zoneId))
            return false;
        try {
            var zone = TimeZoneInfo.FindSystemTimeZoneById(zoneId == "MET"
                ? "W. Europe Standard Time" : zoneId);
            var localNow = TimeZoneInfo.ConvertTime(utcNow, zone).DateTime;
            foreach (var segment in hours.Split(';', StringSplitOptions.RemoveEmptyEntries)) {
                var parts = segment.Split('-');
                if (parts.Length != 2 || parts[1] == "CLOSED")
                    continue;
                if (DateTime.TryParseExact(parts[0], "yyyyMMdd:HHmm", CultureInfo.InvariantCulture,
                        DateTimeStyles.None, out var start)
                    && DateTime.TryParseExact(parts[1], "yyyyMMdd:HHmm", CultureInfo.InvariantCulture,
                        DateTimeStyles.None, out var end)
                    && localNow >= start && localNow < end)
                    return true;
            }
        } catch (TimeZoneNotFoundException) {
            return false;
        }
        return false;
    }

    public static async Task<int> RunAsync(string host, int port, int clientId, string contractsBase64)
    {
        List<LiveQuoteContract>? requests;
        try {
            var json = Encoding.UTF8.GetString(Convert.FromBase64String(contractsBase64));
            requests = JsonSerializer.Deserialize<List<LiveQuoteContract>>(json,
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
        } catch (Exception ex) {
            Console.Error.WriteLine($"Ungueltige Live-Kontrakte: {ex.Message}");
            return 2;
        }
        if (requests is null || requests.Count == 0)
            return 2;
        var states = requests.Select(request => new LiveQuoteState { Request = request }).ToList();
        var wrapper = new LiveQuoteWrapper(states);
        var signal = new EReaderMonitorSignal();
        var client = new EClientSocket(wrapper, signal);
        client.SetConnectOptions("+PACEAPI");
        client.eConnect(host, port, clientId);
        if (!client.IsConnected()) {
            Console.Error.WriteLine("IBKR konnte nicht verbunden werden.");
            return 3;
        }
        using var cancellation = new CancellationTokenSource();
        Console.CancelKeyPress += (_, eventArgs) => {
            eventArgs.Cancel = true;
            cancellation.Cancel();
        };
        var reader = new EReader(client, signal);
        reader.Start();
        var readerTask = Task.Run(() => {
            while (client.IsConnected() && !cancellation.IsCancellationRequested) {
                signal.waitForSignal();
                reader.processMsgs();
            }
        });
        try {
            await wrapper.Connected.Task.WaitAsync(TimeSpan.FromSeconds(10));
            for (var index = 0; index < states.Count; ++index) {
                client.reqContractDetails(10000 + index, states[index].Request.ToContract());
                await Task.Delay(30);
            }
            client.reqMarketDataType(1);
            var detailRefreshDate = DateTime.UtcNow.Date;
            var rotationCursor = 0;
            while (client.IsConnected() && !cancellation.IsCancellationRequested) {
                var now = DateTimeOffset.UtcNow;
                if (now.UtcDateTime.Date != detailRefreshDate) {
                    detailRefreshDate = now.UtcDateTime.Date;
                    for (var index = 0; index < states.Count; ++index) {
                        client.reqContractDetails(10000 + index, states[index].Request.ToContract());
                        await Task.Delay(30);
                    }
                }
                foreach (var (index, contract) in wrapper.TakePendingContractDetails()) {
                    client.reqContractDetails(10000 + index, contract);
                    await Task.Delay(30);
                }
                wrapper.UpdateSubscriptions(client, now, ref rotationCursor);
                Console.WriteLine(JsonSerializer.Serialize(wrapper.Snapshot(now)));
                await Task.Delay(TimeSpan.FromSeconds(10), cancellation.Token);
            }
        } catch (OperationCanceledException) {
            // The parent application has stopped the stream.
        } catch (Exception ex) {
            Console.Error.WriteLine($"IBKR-Livekurse: {ex.Message}");
            return 4;
        } finally {
            for (var index = 0; index < states.Count; ++index)
                if (states[index].Subscribed)
                    client.cancelMktData(20000 + index);
            client.eDisconnect();
            signal.issueSignal();
            await readerTask.WaitAsync(TimeSpan.FromSeconds(2));
        }
        return 0;
    }
}
