using IBApi;

static long Timestamp(string value) => DateTimeOffset.Parse(value).ToUnixTimeSeconds();
static HistoricalSession Session(string day) => new($"{day}-07:30:00", $"{day}-23:00:00", day);
static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
    Console.WriteLine("PASS: " + message);
}

var fridayTimestamp = Timestamp("2026-09-11T17:35:00+02:00");
static ContractDetails Listing(int id, string currency, string primary, string zone) => new() {
    Contract = new Contract { ConId = id, Currency = currency, PrimaryExch = primary, Exchange = primary, SecType = "STK" },
    ValidExchanges = "SMART," + primary, TimeZoneId = zone
};
var listings = new[] { Listing(1, "EUR", "VSE", "MET"), Listing(2, "JPY", "TSEJ", "Japan"), Listing(3, "USD", "NASDAQ", "US/Eastern") };
Check(HomeListingSelector.Select("JP3756600007", listings)?.Contract.ConId == 2, "Japan selects TSEJ/JPY, never USD");
Check(HomeListingSelector.Select("US35137L1052", listings)?.Contract.ConId == 3, "US selects NASDAQ/USD");
Check(HomeListingSelector.Select("JP3756600007", listings.Append(Listing(4, "JPY", "TSEJ", "Japan"))) == null,
    "Ambiguous home listings are rejected");
Check(HomeListingSelector.Select("KYG970081173", listings) == null, "Offshore domicile does not imply a US listing");
Check(HomeListingSelector.Select("CA0203987072", new[] { Listing(7, "CAD", "TSE", "US/Eastern") })?.Contract.Currency == "CAD",
    "Canada uses the IBKR-qualified Canadian currency");
Check(HomeListingSelector.TimeZone("Japan").GetUtcOffset(DateTime.UtcNow) == TimeSpan.FromHours(9), "Native Japan timezone resolved");
Check(HistoricalExchangeOrder.Select("GETTEX2", "SMART,FWB2,GETTEX2").SequenceEqual(new[] { "GETTEX2", "FWB2" }),
    "Preferred history exchange is attempted before FWB2");
Check(HistoricalExchangeOrder.Select("FWB2", "FWB2,GETTEX2").SequenceEqual(new[] { "FWB2" }),
    "Preferred FWB2 is not repeated");
Check(HistoricalExchangeOrder.Select("UNKNOWN", "SMART,FWB2").SequenceEqual(new[] { "FWB2" }),
    "Unqualified preferred exchange is not guessed");
Check(HistoricalExchangeOrder.Select("SMART", "SMART,FWB").SequenceEqual(new[] { "SMART" }),
    "SMART preference is retained and unsupported FWB2 skipped");
var sessions = new[] { Session("20260909"), Session("20260910"), Session("20260911"), Session("20260914") };
var friday = SnapshotReference.Resolve(fridayTimestamp, "MET", sessions);
Check(friday?.lastDate == "2026-09-11" && friday.closeDate == "2026-09-10",
    "Frozen Friday LAST refers to Thursday CLOSE, irrespective of weekend fetch date");
Check(friday!.Matches(fridayTimestamp), "Same trading day reuses confirmed calendar mapping");
Check(!friday.Matches(Timestamp("2026-09-14T12:00:00+02:00")), "New LAST day invalidates cached mapping");
var monday = SnapshotReference.Resolve(Timestamp("2026-09-14T12:00:00+02:00"), "MET", sessions);
Check(monday?.closeDate == "2026-09-11", "Monday skips weekend");
var holidaySessions = new[] { Session("20260402"), Session("20260407") };
Check(SnapshotReference.Resolve(Timestamp("2026-04-07T12:00:00+02:00"), "MET", holidaySessions)?.closeDate == "2026-04-02",
    "Calendar skips Good Friday and Easter Monday");
Check(SnapshotReference.Resolve(fridayTimestamp, "unknown-zone", sessions) == null, "Unknown timezone cannot cause historical write");
Check(SnapshotReference.Resolve(fridayTimestamp, "MET", new[] { Session("20260911") }) == null,
    "Missing previous session cannot cause historical write");
Check(SnapshotReference.Resolve(Timestamp("2026-09-12T12:00:00+02:00"), "MET", sessions) == null,
    "Timestamp outside trading sessions is not guessed");
Check(SnapshotReference.Resolve(long.MaxValue, "MET", sessions) == null, "Invalid timestamp is rejected");
var usSessions = new[] { new HistoricalSession("20260910-04:00:00", "20260910-20:00:00", "20260910"),
    new HistoricalSession("20260911-04:00:00", "20260911-20:00:00", "20260911") };
Check(SnapshotReference.Resolve(Timestamp("2026-09-12T00:00:00Z"), "America/New_York", usSessions)?.lastDate == "2026-09-11",
    "Exchange timezone, not UTC calendar date, determines LAST day");
Check(Math.Round((13.525m - 14.585m) / 14.585m * 100m, 2) == -7.27m, "Sample snapshot pair gives -7.27 percent");
