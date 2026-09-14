using System.Globalization;
using IBApi;

// Dates belong to the instrument's trading calendar, never to the fetch date.
internal sealed record SnapshotReference(string lastDate, string closeDate, string timeZone)
{
    internal static SnapshotReference? Resolve(long timestamp, string zone, HistoricalSession[] sessions)
    {
        try {
            var local = LocalTime(timestamp, zone);
            var current = sessions.FirstOrDefault(session =>
                TrySessionTime(session.StartDateTime, out var start)
                && TrySessionTime(session.EndDateTime, out var end)
                && local >= start && local <= end);
            if (current == null || !TryDate(current.RefDate, out var last))
                return null;
            var previous = sessions.Select(session => TryDate(session.RefDate, out var date) ? date : default)
                .Where(date => date != default && date < last).DefaultIfEmpty().Max();
            return previous == default ? null : new(last.ToString("yyyy-MM-dd"), previous.ToString("yyyy-MM-dd"), zone);
        } catch (ArgumentException) { return null; }
          catch (TimeZoneNotFoundException) { return null; }
          catch (InvalidTimeZoneException) { return null; }
    }

    internal bool Matches(long timestamp)
    {
        try {
            return LocalTime(timestamp, timeZone).ToString("yyyy-MM-dd") == lastDate
                && DateOnly.TryParseExact(closeDate, "yyyy-MM-dd", out var previous)
                && DateOnly.TryParseExact(lastDate, "yyyy-MM-dd", out var last) && previous < last;
        } catch (ArgumentException) { return false; }
          catch (TimeZoneNotFoundException) { return false; }
          catch (InvalidTimeZoneException) { return false; }
    }

    private static DateTime LocalTime(long timestamp, string zone) =>
        TimeZoneInfo.ConvertTime(DateTimeOffset.FromUnixTimeSeconds(timestamp),
            TimeZoneInfo.FindSystemTimeZoneById(zone == "MET" ? "Europe/Berlin" : zone)).DateTime;

    private static bool TryDate(string value, out DateOnly date) =>
        DateOnly.TryParseExact(value, "yyyyMMdd", CultureInfo.InvariantCulture, DateTimeStyles.None, out date);

    private static bool TrySessionTime(string value, out DateTime date) =>
        DateTime.TryParseExact(value, new[] { "yyyyMMdd-HH:mm:ss", "yyyyMMdd HH:mm:ss" },
            CultureInfo.InvariantCulture, DateTimeStyles.None, out date);
}
