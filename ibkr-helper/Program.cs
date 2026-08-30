using System.Globalization;
using System.Text.Json;
using IBApi;

const int RequestId = 1001;

static string? Argument(IReadOnlyList<string> args, string name)
{
    for (var index = 0; index + 1 < args.Count; ++index) {
        if (args[index] == name)
            return args[index + 1];
    }
    return null;
}

static void WriteResult(object result)
{
    Console.WriteLine(JsonSerializer.Serialize(result));
}

var host = Argument(args, "--host") ?? "127.0.0.1";
var symbol = Argument(args, "--symbol")?.Trim();
var currency = Argument(args, "--currency")?.Trim().ToUpperInvariant() ?? string.Empty;
var isin = Argument(args, "--isin")?.Trim().ToUpperInvariant() ?? string.Empty;
var exchange = Argument(args, "--exchange")?.Trim().ToUpperInvariant() ?? string.Empty;
var primaryExchange = Argument(args, "--primary-exchange")?.Trim().ToUpperInvariant() ?? string.Empty;
var conIdText = Argument(args, "--con-id")?.Trim() ?? string.Empty;
var conId = int.TryParse(conIdText, out var parsedConId) ? parsedConId : 0;
var directExchange = args.Any(value => string.Equals(value, "--direct-exchange", StringComparison.OrdinalIgnoreCase));
var matchSymbols = args.Any(value => string.Equals(value, "--match-symbols", StringComparison.OrdinalIgnoreCase));
var isinOnly = args.Any(value => string.Equals(value, "--isin-only", StringComparison.OrdinalIgnoreCase));
var historicalQuotes = args.Any(value => string.Equals(value, "--historical-quotes", StringComparison.OrdinalIgnoreCase));
var marketSnapshot = args.Any(value => string.Equals(value, "--market-snapshot", StringComparison.OrdinalIgnoreCase));
var probeQuoteExchanges = args.Any(value => string.Equals(value, "--probe-quote-exchanges", StringComparison.OrdinalIgnoreCase));
var rawExchanges = (Argument(args, "--exchanges") ?? string.Empty)
    .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
    .Where(value => !string.Equals(value, "SMART", StringComparison.OrdinalIgnoreCase))
    .Distinct(StringComparer.OrdinalIgnoreCase)
    .ToArray();
var hasFwbExchange = rawExchanges.Any(value => string.Equals(value, "FWB", StringComparison.OrdinalIgnoreCase));
var exchanges = rawExchanges
    .Where(value => !(hasFwbExchange && string.Equals(value, "SBF", StringComparison.OrdinalIgnoreCase)))
    .ToArray();
var daysText = Argument(args, "--days")?.Trim() ?? string.Empty;
var days = int.TryParse(daysText, out var parsedDays) ? parsedDays : 90;
var timeoutSecondsText = Argument(args, "--timeout-seconds")?.Trim() ?? string.Empty;
var timeoutSeconds = int.TryParse(timeoutSecondsText, out var parsedTimeoutSeconds)
    ? Math.Max(1, parsedTimeoutSeconds)
    : 0;

if ((string.IsNullOrWhiteSpace(symbol) && conId <= 0)
    || !int.TryParse(Argument(args, "--port"), out var port)
    || !int.TryParse(Argument(args, "--client-id") ?? "23", out var clientId)) {
    WriteResult(new { success = false, message = "Ungültige Argumente für den IBKR-Helfer." });
    return 2;
}

var defaultTimeoutSeconds = probeQuoteExchanges
    ? Math.Max(75, exchanges.Length * 25)
    : ((historicalQuotes || marketSnapshot) ? 75 : 20);
using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(
    timeoutSeconds > 0 ? timeoutSeconds : defaultTimeoutSeconds));
var wrapper = new ContractDetailsWrapper(RequestId,
                                         symbol ?? string.Empty,
                                         currency,
                                         isin,
                                         exchange,
                                         primaryExchange,
                                         conId,
                                         directExchange,
                                         matchSymbols,
                                         isinOnly,
                                         historicalQuotes,
                                         marketSnapshot,
                                         probeQuoteExchanges,
                                         exchanges,
                                         Math.Max(1, days));
var signal = new EReaderMonitorSignal();
var client = new EClientSocket(wrapper, signal);
wrapper.Client = client;
client.SetConnectOptions("+PACEAPI");

try {
    Console.Error.WriteLine($"Connecting to {host}:{port} ...");
    client.eConnect(host, port, clientId);
    Console.Error.WriteLine($"eConnect returned. Connected={client.IsConnected()}");
    if (!client.IsConnected()) {
        WriteResult(new { success = false, message = $"IBKR-Verbindung zu {host}:{port} fehlgeschlagen." });
        return 3;
    }

    var reader = new EReader(client, signal);
    reader.Start();
    Console.Error.WriteLine("Reader started.");
    var readerTask = Task.Run(() => {
        while (client.IsConnected() && !timeout.IsCancellationRequested) {
            signal.waitForSignal();
            if (timeout.IsCancellationRequested)
                break;
            reader.processMsgs();
        }
    });

    await wrapper.ConnectionReady.Task.WaitAsync(timeout.Token);
    Console.Error.WriteLine("API connection ready.");

    if (probeQuoteExchanges) {
        wrapper.StartQuoteExchangeProbe();
        Console.Error.WriteLine($"Quote exchange probe requested for {symbol}/{conId}: {string.Join(",", exchanges)}.");
    } else if (historicalQuotes) {
        var requestedDays = Math.Max(1, days);
        var duration = requestedDays > 365
            ? $"{Math.Max(1, (int)Math.Ceiling(requestedDays / 365.0))} Y"
            : $"{requestedDays} D";
        client.reqHistoricalData(RequestId,
                                 wrapper.CreateCurrentContract(),
                                 string.Empty,
                                 duration,
                                 "1 day",
                                 "TRADES",
                                 1,
                                 1,
                                 false,
                                 []);
        Console.Error.WriteLine($"Historical quotes requested for {symbol}/{conId}, days={days}, duration={duration}.");
    } else if (marketSnapshot) {
        client.reqMarketDataType(3);
        client.reqMktData(RequestId,
                          wrapper.CreateCurrentContract(),
                          string.Empty,
                          true,
                          false,
                          []);
        Console.Error.WriteLine($"Market snapshot requested for {symbol}/{conId}.");
    } else if (matchSymbols) {
        client.reqMatchingSymbols(RequestId, symbol);
        Console.Error.WriteLine($"Matching symbols requested for {symbol}.");
    } else {
        client.reqContractDetails(RequestId, wrapper.CreateCurrentContract());
        Console.Error.WriteLine($"Contract details requested for {symbol}/{currency}/{isin}.");
    }

    var result = await wrapper.Result.Task.WaitAsync(timeout.Token);
    WriteResult(result);
    client.eDisconnect();
    signal.issueSignal();
    await readerTask.WaitAsync(TimeSpan.FromSeconds(2));
    return result.success ? 0 : 4;
}
catch (OperationCanceledException) {
    WriteResult(new { success = false, message = "Zeitüberschreitung beim Abruf der IBKR-Daten." });
    return 5;
}
catch (Exception exception) {
    WriteResult(new { success = false, message = $"IBKR-Abruf fehlgeschlagen: {exception.Message}" });
    return 6;
}
finally {
    if (client.IsConnected())
        client.eDisconnect();
}

internal sealed class ContractDetailsWrapper : DefaultEWrapper
{
    private readonly int requestId;
    private readonly string requestedSymbol;
    private readonly string requestedCurrency;
    private readonly string requestedIsin;
    private readonly string requestedExchange;
    private readonly string requestedPrimaryExchange;
    private readonly int requestedConId;
    private readonly bool directExchange;
    private readonly bool matchSymbols;
    private readonly bool isinOnly;
    private readonly bool historicalQuotes;
    private readonly bool marketSnapshot;
    private readonly bool probeQuoteExchanges;
    private readonly string[] quoteExchangeCandidates;
    private readonly int historicalDays;
    private readonly List<ContractDetails> matches = [];
    private readonly List<HistoricalBar> historicalBars = [];
    private readonly List<HistoricalBar> currentProbeBars = [];
    private readonly List<QuoteExchangeProbeResult> quoteExchangeProbeResults = [];
    private readonly List<int> snapshotMarketDataTypes = [];
    private readonly Dictionary<string, double> snapshotPrices = [];
    private readonly Dictionary<string, long> snapshotSizes = [];
    private readonly object snapshotLock = new();
    private bool snapshotCompletionQueued;
    private bool usingIsinRequest;
    private bool retriedSymbolRequest;
    private int currentProbeIndex;
    private string currentProbeExchange = string.Empty;

    public ContractDetailsWrapper(int requestId,
                                  string requestedSymbol,
                                  string requestedCurrency,
                                  string requestedIsin,
                                  string requestedExchange,
                                  string requestedPrimaryExchange,
                                  int requestedConId,
                                  bool directExchange,
                                  bool matchSymbols,
                                  bool isinOnly,
                                  bool historicalQuotes,
                                  bool marketSnapshot,
                                  bool probeQuoteExchanges,
                                  string[] quoteExchangeCandidates,
                                  int historicalDays)
    {
        this.requestId = requestId;
        this.requestedSymbol = requestedSymbol;
        this.requestedCurrency = requestedCurrency;
        this.requestedIsin = requestedIsin;
        this.requestedExchange = requestedExchange;
        this.requestedPrimaryExchange = requestedPrimaryExchange;
        this.requestedConId = requestedConId;
        this.directExchange = directExchange;
        this.matchSymbols = matchSymbols;
        this.isinOnly = isinOnly;
        this.historicalQuotes = historicalQuotes;
        this.marketSnapshot = marketSnapshot;
        this.probeQuoteExchanges = probeQuoteExchanges;
        this.quoteExchangeCandidates = quoteExchangeCandidates;
        this.historicalDays = historicalDays;
        this.usingIsinRequest = !string.IsNullOrWhiteSpace(requestedIsin);
    }

    public EClientSocket? Client { get; set; }
    public TaskCompletionSource ConnectionReady { get; } =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    public TaskCompletionSource<dynamic> Result { get; } =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public override void nextValidId(int orderId)
    {
        ConnectionReady.TrySetResult();
    }

    public override void connectAck()
    {
        if (Client?.AsyncEConnect == true)
            Client.startApi();
    }

    public override void symbolSamples(int reqId, ContractDescription[] contractDescriptions)
    {
        if (reqId != requestId || !matchSymbols)
            return;

        Result.TrySetResult(new {
            success = true,
            message = $"IBKR-Symbolsuche fuer {requestedSymbol} abgeschlossen.",
            data = contractDescriptions.Select(description => new {
                conId = description.Contract.ConId,
                symbol = description.Contract.Symbol ?? string.Empty,
                securityType = description.Contract.SecType ?? string.Empty,
                primaryExchange = description.Contract.PrimaryExch ?? string.Empty,
                currency = description.Contract.Currency ?? string.Empty,
                description = description.Contract.Description ?? string.Empty,
                issuerId = description.Contract.IssuerId ?? string.Empty,
                derivativeSecTypes = description.DerivativeSecTypes ?? []
            }).ToArray()
        });
    }

    public override void contractDetails(int reqId, ContractDetails contractDetails)
    {
        if (reqId == requestId)
            matches.Add(contractDetails);
    }

    public override void historicalData(int reqId, Bar bar)
    {
        if (reqId != requestId || (!historicalQuotes && !probeQuoteExchanges))
            return;

        var historicalBar = new HistoricalBar(
            NormalizeIbkrDate(bar.Time),
            bar.Open,
            bar.High,
            bar.Low,
            bar.Close,
            (double)bar.Volume);
        if (probeQuoteExchanges)
            currentProbeBars.Add(historicalBar);
        else
            historicalBars.Add(historicalBar);
    }

    public override void historicalDataEnd(int reqId, string start, string end)
    {
        if (reqId != requestId || (!historicalQuotes && !probeQuoteExchanges))
            return;

        if (probeQuoteExchanges) {
            var turnover = currentProbeBars.Sum(bar => bar.close * bar.volume);
            quoteExchangeProbeResults.Add(new QuoteExchangeProbeResult(
                currentProbeExchange,
                turnover,
                currentProbeBars.Count,
                currentProbeBars.Sum(bar => bar.volume)));
            currentProbeBars.Clear();
            RequestNextQuoteExchangeProbe();
            return;
        }

        Result.TrySetResult(new {
            success = historicalBars.Count > 0,
            message = historicalBars.Count > 0
                ? $"IBKR-Quotes fuer {requestedSymbol} wurden empfangen."
                : $"IBKR lieferte keine Quotes fuer {requestedSymbol}.",
            data = historicalBars
                .OrderBy(bar => bar.date, StringComparer.Ordinal)
                .ToArray()
        });
    }

    public override void tickPrice(int tickerId, int field, double price, TickAttrib attribs)
    {
        if (tickerId != requestId || !marketSnapshot || price <= 0)
            return;

        lock (snapshotLock) {
            snapshotPrices[TickFieldName(field)] = price;
        }
        QueueSnapshotCompletionIfUsable();
    }

    public override void marketDataType(int reqId, int marketDataType)
    {
        if (reqId != requestId || !marketSnapshot)
            return;

        lock (snapshotLock) {
            snapshotMarketDataTypes.Add(marketDataType);
        }
        Console.Error.WriteLine($"Market data type for {requestedSymbol}/{requestedConId}: {marketDataType}.");
    }

    public override void tickSize(int tickerId, int field, decimal size)
    {
        if (tickerId != requestId || !marketSnapshot)
            return;

        lock (snapshotLock) {
            snapshotSizes[TickFieldName(field)] = decimal.ToInt64(size);
        }
    }

    public override void tickSnapshotEnd(int reqId)
    {
        if (reqId != requestId || !marketSnapshot)
            return;

        CompleteSnapshot();
    }

    public void StartQuoteExchangeProbe()
    {
        currentProbeIndex = 0;
        quoteExchangeProbeResults.Clear();
        currentProbeBars.Clear();
        RequestNextQuoteExchangeProbe();
    }

    public override void contractDetailsEnd(int reqId)
    {
        if (reqId != requestId)
            return;

        if (matches.Count == 0) {
            if (TryRetryWithSymbol("keine ISIN-Treffer"))
                return;

            Result.TrySetResult(new {
                success = false,
                message = $"IBKR hat keinen eindeutigen Aktienkontrakt für {requestedSymbol} gefunden."
            });
            return;
        }

        var ranked = matches
            .Select(details => new { details, score = Score(details) })
            .OrderByDescending(candidate => candidate.score)
            .ThenBy(candidate => candidate.details.Contract.ConId)
            .ToList();
        var best = ranked[0];
        var equallyRelevant = ranked
            .Where(candidate => candidate.score == best.score)
            .ToList();
        var equallyRelevantIsins = equallyRelevant
            .Select(candidate => Isin(candidate.details))
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

        if (equallyRelevantIsins.Count > 1) {
            var candidates = string.Join(", ", equallyRelevant.Take(5).Select(candidate =>
                $"{candidate.details.Contract.LocalSymbol}/{candidate.details.Contract.Currency}/"
                + $"{candidate.details.Contract.PrimaryExch}/ISIN:{Isin(candidate.details)}"));
            Result.TrySetResult(new {
                success = false,
                reason = "ambiguous_isin",
                message = $"IBKR-Kontrakt fuer {requestedSymbol} lieferte mehrere relevante ISINs: {candidates}",
                data = new {
                    isins = equallyRelevantIsins
                }
            });
            return;
        }

        if (ranked.Count > 1 && ranked[1].score == best.score) {
            var candidates = string.Join(", ", equallyRelevant.Take(5).Select(candidate =>
                $"{candidate.details.Contract.LocalSymbol}/{candidate.details.Contract.Currency}/"
                + $"{candidate.details.Contract.PrimaryExch}"));
            Result.TrySetResult(new {
                success = false,
                message = $"IBKR-Kontrakt für {requestedSymbol} ist mehrdeutig: {candidates}"
            });
            return;
        }

        var selected = best.details;
        var identifiers = selected.SecIdList ?? [];
        var returnedIsin = identifiers.FirstOrDefault(value =>
            string.Equals(value.Tag, "ISIN", StringComparison.OrdinalIgnoreCase))?.Value ?? string.Empty;
        var returnedCusip = identifiers.FirstOrDefault(value =>
            string.Equals(value.Tag, "CUSIP", StringComparison.OrdinalIgnoreCase))?.Value
            ?? selected.Cusip
            ?? string.Empty;

        if (!string.IsNullOrEmpty(requestedIsin)
            && !string.Equals(returnedIsin, requestedIsin, StringComparison.OrdinalIgnoreCase)) {
            Result.TrySetResult(new {
                success = false,
                message = $"IBKR-Kontrakt fuer {requestedSymbol} passt nicht zur angefragten ISIN "
                          + $"{requestedIsin}: geliefert wurde {selected.Contract.Symbol}/"
                          + $"{selected.Contract.Currency}/{selected.Contract.PrimaryExch}"
                          + (string.IsNullOrEmpty(returnedIsin) ? " ohne ISIN." : $" mit ISIN {returnedIsin}.")
            });
            return;
        }

        Result.TrySetResult(new {
            success = true,
            message = $"IBKR-Stammdaten für {requestedSymbol} wurden empfangen.",
            data = new {
                ibkrConId = selected.Contract.ConId,
                symbol = selected.Contract.Symbol ?? string.Empty,
                longName = selected.LongName ?? string.Empty,
                currency = selected.Contract.Currency ?? string.Empty,
                primaryExchange = selected.Contract.PrimaryExch ?? string.Empty,
                localSymbol = selected.Contract.LocalSymbol ?? string.Empty,
                securityType = selected.Contract.SecType ?? string.Empty,
                tradingClass = selected.Contract.TradingClass ?? string.Empty,
                stockType = selected.StockType ?? string.Empty,
                industry = selected.Industry ?? string.Empty,
                category = selected.Category ?? string.Empty,
                subcategory = selected.Subcategory ?? string.Empty,
                timeZoneId = selected.TimeZoneId ?? string.Empty,
                tradingHours = selected.TradingHours ?? string.Empty,
                liquidHours = selected.LiquidHours ?? string.Empty,
                minTick = selected.MinTick,
                marketRuleIds = selected.MarketRuleIds ?? string.Empty,
                validExchanges = selected.ValidExchanges ?? string.Empty,
                orderTypes = selected.OrderTypes ?? string.Empty,
                marketName = selected.MarketName ?? string.Empty,
                isin = returnedIsin,
                cusip = returnedCusip
            }
        });
    }

    public override void error(int id,
                               long errorTime,
                               int errorCode,
                               string errorMsg,
                               string advancedOrderRejectJson)
    {
        if (errorCode is 2104 or 2106 or 2107 or 2108 or 2158)
            return;

        if (id == requestId || errorCode is 502 or 504 or 507) {
            if ((historicalQuotes || probeQuoteExchanges) && errorCode is 162 or 165 or 166 or 200) {
                if (probeQuoteExchanges) {
                    quoteExchangeProbeResults.Add(new QuoteExchangeProbeResult(
                        currentProbeExchange,
                        0,
                        0,
                        0));
                    currentProbeBars.Clear();
                    RequestNextQuoteExchangeProbe();
                    return;
                }
                Result.TrySetResult(new {
                    success = false,
                    message = $"IBKR-Fehler {errorCode}: {errorMsg}"
                });
                return;
            }

            if (marketSnapshot && errorCode is 10090 or 10167 or 10186 or 354) {
                // IBKR may still deliver delayed snapshot ticks after these permission warnings.
                // Keep the request alive so tickSnapshotEnd can select live or DELAYED_* values.
                return;
            }

            if (errorCode == 200 && TryRetryWithSymbol(errorMsg))
                return;

            Result.TrySetResult(new {
                success = false,
                message = $"IBKR-Fehler {errorCode}: {errorMsg}"
            });
            ConnectionReady.TrySetException(new InvalidOperationException(
                $"IBKR-Fehler {errorCode}: {errorMsg}"));
        }
    }

    public override void connectionClosed()
    {
        var exception = new IOException("Die IBKR-Verbindung wurde geschlossen.");
        ConnectionReady.TrySetException(exception);
        Result.TrySetException(exception);
    }

    private int Score(ContractDetails details)
    {
        var score = 0;
        if (string.Equals(details.Contract.LocalSymbol, requestedSymbol, StringComparison.OrdinalIgnoreCase))
            score += 8;
        if (string.Equals(details.Contract.Symbol, requestedSymbol, StringComparison.OrdinalIgnoreCase))
            score += 6;
        if (!string.IsNullOrEmpty(requestedCurrency)
            && string.Equals(details.Contract.Currency, requestedCurrency, StringComparison.OrdinalIgnoreCase))
            score += 4;
        if (!string.IsNullOrEmpty(requestedIsin)
            && details.SecIdList?.Any(value =>
                string.Equals(value.Tag, "ISIN", StringComparison.OrdinalIgnoreCase)
                && string.Equals(value.Value, requestedIsin, StringComparison.OrdinalIgnoreCase)) == true)
            score += 40;
        if (!string.IsNullOrEmpty(requestedExchange)
            && (string.Equals(details.Contract.PrimaryExch, requestedExchange, StringComparison.OrdinalIgnoreCase)
                || (details.ValidExchanges ?? string.Empty)
                    .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                    .Any(value => string.Equals(value, requestedExchange, StringComparison.OrdinalIgnoreCase))))
            score += 10;
        if (string.Equals(details.Contract.PrimaryExch, "PINK", StringComparison.OrdinalIgnoreCase)
            || string.Equals(details.Contract.PrimaryExch, "OTC", StringComparison.OrdinalIgnoreCase))
            score -= 20;
        if (string.Equals(details.Contract.PrimaryExch, "VENTURE", StringComparison.OrdinalIgnoreCase)
            || string.Equals(details.Contract.PrimaryExch, "TSE", StringComparison.OrdinalIgnoreCase)
            || string.Equals(details.Contract.PrimaryExch, "TSEJ", StringComparison.OrdinalIgnoreCase)
            || string.Equals(details.Contract.PrimaryExch, "LSE", StringComparison.OrdinalIgnoreCase)
            || string.Equals(details.Contract.PrimaryExch, "SFB", StringComparison.OrdinalIgnoreCase)
            || string.Equals(details.Contract.PrimaryExch, "SBF", StringComparison.OrdinalIgnoreCase))
            score += 6;
        return score;
    }

    private static string Isin(ContractDetails details)
    {
        return details.SecIdList?.FirstOrDefault(value =>
            string.Equals(value.Tag, "ISIN", StringComparison.OrdinalIgnoreCase))?.Value ?? string.Empty;
    }

    private static string NormalizeIbkrDate(string value)
    {
        var trimmed = (value ?? string.Empty).Trim();
        if (DateTime.TryParseExact(trimmed,
                                   "yyyyMMdd",
                                   CultureInfo.InvariantCulture,
                                   DateTimeStyles.None,
                                   out var compactDate)) {
            return compactDate.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
        }
        if (DateTime.TryParse(trimmed, CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out var parsedDate))
            return parsedDate.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
        return trimmed;
    }

    private double? SnapshotValue(string key)
    {
        return snapshotPrices.TryGetValue(key, out var value) && value > 0 ? value : null;
    }

    private void QueueSnapshotCompletionIfUsable()
    {
        lock (snapshotLock) {
            if (snapshotCompletionQueued || !HasUsableSnapshotLocked())
                return;
            snapshotCompletionQueued = true;
        }

        _ = Task.Run(async () => {
            await Task.Delay(250);
            CompleteSnapshot();
        });
    }

    private bool HasUsableSnapshotLocked()
    {
        return SnapshotValue("LAST").HasValue
            || SnapshotValue("DELAYED_LAST").HasValue
            || SnapshotValue("CLOSE").HasValue
            || SnapshotValue("DELAYED_CLOSE").HasValue
            || SnapshotValue("BID").HasValue
            || SnapshotValue("DELAYED_BID").HasValue
            || SnapshotValue("ASK").HasValue
            || SnapshotValue("DELAYED_ASK").HasValue;
    }

    private void CompleteSnapshot()
    {
        double? last;
        double? bid;
        double? ask;
        double? close;
        int[] marketDataTypes;
        Dictionary<string, double> prices;
        Dictionary<string, long> sizes;

        lock (snapshotLock) {
            last = SnapshotValue("LAST") ?? SnapshotValue("DELAYED_LAST");
            bid = SnapshotValue("BID") ?? SnapshotValue("DELAYED_BID");
            ask = SnapshotValue("ASK") ?? SnapshotValue("DELAYED_ASK");
            close = SnapshotValue("CLOSE") ?? SnapshotValue("DELAYED_CLOSE");
            marketDataTypes = snapshotMarketDataTypes.Distinct().ToArray();
            prices = new Dictionary<string, double>(snapshotPrices);
            sizes = new Dictionary<string, long>(snapshotSizes);
        }

        double? mid = bid.HasValue && ask.HasValue ? (bid.Value + ask.Value) / 2.0 : null;
        var selected = last ?? mid ?? close ?? bid ?? ask;

        if (selected.HasValue)
            Client?.cancelMktData(requestId);

        Result.TrySetResult(new {
            success = selected.HasValue,
            message = selected.HasValue
                ? $"IBKR-Snapshot fuer {requestedSymbol} wurde empfangen."
                : $"IBKR lieferte keinen verwertbaren Snapshot fuer {requestedSymbol}.",
            data = new {
                selected,
                last,
                bid,
                ask,
                mid,
                close,
                marketDataTypes,
                prices,
                sizes
            }
        });
    }

    private static string TickFieldName(int field)
    {
        return field switch {
            1 => "BID",
            2 => "ASK",
            4 => "LAST",
            6 => "HIGH",
            7 => "LOW",
            8 => "VOLUME",
            9 => "CLOSE",
            14 => "OPEN",
            66 => "DELAYED_BID",
            67 => "DELAYED_ASK",
            68 => "DELAYED_LAST",
            72 => "DELAYED_HIGH",
            73 => "DELAYED_LOW",
            74 => "DELAYED_CLOSE",
            75 => "DELAYED_OPEN",
            _ => $"FIELD_{field}"
        };
    }

    public Contract CreateCurrentContract()
    {
        return CreateCurrentContract(string.Empty);
    }

    private Contract CreateCurrentContract(string exchangeOverride)
    {
        var effectiveExchange = string.IsNullOrWhiteSpace(exchangeOverride)
            ? requestedExchange
            : exchangeOverride;
        var contract = new Contract {
            ConId = requestedConId,
            Symbol = usingIsinRequest ? string.Empty : requestedSymbol,
            SecType = "STK",
            Exchange = (directExchange || !string.IsNullOrWhiteSpace(exchangeOverride))
                       && !string.IsNullOrWhiteSpace(effectiveExchange)
                ? effectiveExchange
                : "SMART",
            Currency = requestedCurrency
        };
        if (!string.IsNullOrWhiteSpace(requestedPrimaryExchange))
            contract.PrimaryExch = requestedPrimaryExchange;

        if (usingIsinRequest) {
            contract.SecIdType = "ISIN";
            contract.SecId = requestedIsin;
        }

        return contract;
    }

    private void RequestNextQuoteExchangeProbe()
    {
        if (Client is null) {
            Result.TrySetResult(new {
                success = false,
                message = "IBKR-Client ist nicht verfuegbar."
            });
            return;
        }

        while (currentProbeIndex < quoteExchangeCandidates.Length) {
            currentProbeExchange = quoteExchangeCandidates[currentProbeIndex++].Trim().ToUpperInvariant();
            if (string.IsNullOrWhiteSpace(currentProbeExchange))
                continue;

            Client.reqHistoricalData(requestId,
                                     CreateCurrentContract(currentProbeExchange),
                                     string.Empty,
                                     $"{Math.Max(1, historicalDays)} D",
                                     "1 day",
                                     "TRADES",
                                     1,
                                     1,
                                     false,
                                     []);
            Console.Error.WriteLine($"Historical quote probe requested for {requestedSymbol}/{currentProbeExchange}.");
            return;
        }

        var successfulProbeResults = quoteExchangeProbeResults
            .Where(result => result.turnover > 0)
            .ToArray();
        var best = successfulProbeResults
            .Where(result => IsEuroQuoteExchange(result.exchange))
            .OrderBy(result => EuroQuoteExchangePreferenceRank(result.exchange))
            .ThenByDescending(result => result.turnover)
            .ThenByDescending(result => result.volume)
            .FirstOrDefault()
            ?? successfulProbeResults
                .OrderByDescending(result => result.turnover)
                .ThenByDescending(result => result.volume)
                .FirstOrDefault();
        if (best is null) {
            Result.TrySetResult(new {
                success = false,
                message = $"IBKR konnte fuer {requestedSymbol} keine Umsatzboerse ermitteln.",
                data = quoteExchangeProbeResults.ToArray()
            });
            return;
        }

        Result.TrySetResult(new {
            success = true,
            message = $"IBKR-Umsatzboerse fuer {requestedSymbol}: {best.exchange}.",
            data = new {
                exchange = best.exchange,
                turnover = best.turnover,
                volume = best.volume,
                bars = best.bars,
                candidates = quoteExchangeProbeResults.ToArray()
            }
        });
    }

    private static bool IsEuroQuoteExchange(string exchange)
    {
        var normalized = (exchange ?? string.Empty).Trim().ToUpperInvariant();
        return normalized is "AEB" or "BATEEN"
            or "BM" or "BME"
            or "BRU"
            or "BVME"
            or "ENEXT.BE" or "ENEXT.FR" or "ENEXT.NL" or "ENEXT.PT"
            or "FRA" or "FWB" or "FWB2"
            or "GETTEX" or "GETTEX2"
            or "IBIS" or "IBIS2"
            or "LISB"
            or "MCE"
            or "MIL"
            or "PAR"
            or "SBF"
            or "SWB"
            or "TGATE"
            or "VIE" or "VSE"
            or "XAMS" or "XATH" or "XBRU"
            or "XDUB" or "XETR" or "XFRA"
            or "XHEL" or "XLIS" or "XMAD"
            or "XMIL" or "XPAR" or "XVIE";
    }

    private static int EuroQuoteExchangePreferenceRank(string exchange)
    {
        var normalized = (exchange ?? string.Empty).Trim().ToUpperInvariant();
        return normalized switch {
            "FWB" => 0,
            "FWB2" => 1,
            "GETTEX" => 2,
            "GETTEX2" => 3,
            "TGATE" => 4,
            "IBIS" or "IBIS2" => 5,
            "SBF" => 90,
            _ => 50
        };
    }

    private bool TryRetryWithSymbol(string reason)
    {
        if (!usingIsinRequest || isinOnly || retriedSymbolRequest || Client is null)
            return false;

        retriedSymbolRequest = true;
        usingIsinRequest = false;
        matches.Clear();
        Console.Error.WriteLine($"Retrying {requestedSymbol} by symbol after ISIN lookup failed: {reason}");
        Client.reqContractDetails(requestId, CreateCurrentContract());
        return true;
    }
}

internal sealed record HistoricalBar(string date,
                                     double open,
                                     double high,
                                     double low,
                                     double close,
                                     double volume);

internal sealed record QuoteExchangeProbeResult(string exchange,
                                                double turnover,
                                                int bars,
                                                double volume);
