internal static class HistoricalExchangeOrder
{
    internal static string[] Select(string preferred, string validExchanges)
    {
        var valid = validExchanges.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(value => value.ToUpperInvariant()).ToHashSet();
        // Do not guess a trading venue or repeat the same exchange twice.
        return new[] { preferred.Trim().ToUpperInvariant(), "FWB2" }
            .Where(value => value.Length > 0 && valid.Contains(value)).Distinct().ToArray();
    }
}
