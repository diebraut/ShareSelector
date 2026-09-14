using IBApi;

internal static class HomeListingSelector
{
    // ISIN domicile filters native venues; currency always comes from IBKR.
    // Unknown/offshore domiciles are not silently assigned a US listing.
    private static readonly Dictionary<string, string[]> Venues = new() {
        ["US"] = ["NASDAQ", "NYSE", "AMEX", "ARCA", "ISLAND"],
        ["JP"] = ["TSEJ", "OSE.JPN"], ["CA"] = ["TSE", "VENTURE", "PURE"],
        ["GB"] = ["LSE", "LSEETF"], ["CH"] = ["EBS", "SIX"],
        ["DE"] = ["IBIS", "IBIS2", "FWB", "FWB2"], ["FR"] = ["SBF"],
        ["NL"] = ["AEB"], ["BE"] = ["ENEXT.BE"], ["IT"] = ["BVME"],
        ["ES"] = ["BM"], ["PT"] = ["BVL"], ["AT"] = ["VSE"],
        ["SE"] = ["SFB"], ["NO"] = ["OSE"], ["DK"] = ["CPH"], ["FI"] = ["HEX"],
        ["AU"] = ["ASX"], ["NZ"] = ["NZE"], ["HK"] = ["SEHK"],
        ["SG"] = ["SGX"], ["CN"] = ["SEHKNTL", "SEHKSZSE", "SSE", "SZSE"],
        ["IL"] = ["TASE"], ["ZA"] = ["JSE"], ["MX"] = ["MEXI"],
        ["PL"] = ["WSE"], ["HU"] = ["BUX"], ["CZ"] = ["PRA"]
    };

    internal static ContractDetails? Select(string isin, IEnumerable<ContractDetails> details)
    {
        if (isin.Length != 12 || !Venues.TryGetValue(isin[..2].ToUpperInvariant(), out var venues)) return null;
        var candidates = details.Where(d => d.Contract.SecType == "STK"
            && d.Contract.ConId > 0 && d.Contract.Currency.Length == 3
            && venues.Contains(d.Contract.PrimaryExch) && !string.IsNullOrWhiteSpace(d.TimeZoneId)
            && (d.ValidExchanges ?? "").Split(',').Contains(d.Contract.PrimaryExch))
            .GroupBy(d => d.Contract.ConId).ToArray();
        if (candidates.Length != 1) return null;
        return candidates[0].FirstOrDefault(d => d.Contract.Exchange == d.Contract.PrimaryExch) ?? candidates[0].First();
    }

    internal static TimeZoneInfo TimeZone(string value) => TimeZoneInfo.FindSystemTimeZoneById(value switch {
        "Japan" => "Asia/Tokyo", "MET" => "Europe/Berlin", "GB-Eire" => "Europe/London",
        "Hongkong" => "Asia/Hong_Kong", "US/Eastern" => "America/New_York",
        "US/Central" => "America/Chicago", _ => value
    });
}
