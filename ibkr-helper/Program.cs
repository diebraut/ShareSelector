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

if (string.IsNullOrWhiteSpace(symbol)
    || !int.TryParse(Argument(args, "--port"), out var port)
    || !int.TryParse(Argument(args, "--client-id") ?? "23", out var clientId)) {
    WriteResult(new { success = false, message = "Ungültige Argumente für den IBKR-Helfer." });
    return 2;
}

using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
var wrapper = new ContractDetailsWrapper(RequestId, symbol, currency, isin);
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

    var contract = new Contract {
        Symbol = symbol,
        SecType = "STK",
        Exchange = "SMART",
        Currency = currency
    };
    client.reqContractDetails(RequestId, contract);
    Console.Error.WriteLine($"Contract details requested for {symbol}/{currency}.");

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
    private readonly List<ContractDetails> matches = [];

    public ContractDetailsWrapper(int requestId,
                                  string requestedSymbol,
                                  string requestedCurrency,
                                  string requestedIsin)
    {
        this.requestId = requestId;
        this.requestedSymbol = requestedSymbol;
        this.requestedCurrency = requestedCurrency;
        this.requestedIsin = requestedIsin;
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

    public override void contractDetails(int reqId, ContractDetails contractDetails)
    {
        if (reqId == requestId)
            matches.Add(contractDetails);
    }

    public override void contractDetailsEnd(int reqId)
    {
        if (reqId != requestId)
            return;

        if (matches.Count == 0) {
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

        if (ranked.Count > 1 && ranked[1].score == best.score) {
            var candidates = string.Join(", ", ranked.Take(5).Select(candidate =>
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

        Result.TrySetResult(new {
            success = true,
            message = $"IBKR-Stammdaten für {requestedSymbol} wurden empfangen.",
            data = new {
                ibkrConId = selected.Contract.ConId,
                symbol = selected.Contract.Symbol ?? string.Empty,
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
            score += 20;
        return score;
    }
}
