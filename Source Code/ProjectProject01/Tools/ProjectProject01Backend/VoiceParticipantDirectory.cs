// Short-lived identities, not credentials. Never exposed without current survivor membership checks.
internal sealed class VoiceParticipantDirectory
{
    private sealed class MatchEntries
    {
        public DateTimeOffset LastUsed;
        public readonly Dictionary<ulong, VoiceParticipantName> Players = [];
    }
    private readonly object gate = new();
    private readonly Dictionary<Guid, MatchEntries> matches = [];
    private readonly TimeProvider clock;
    public VoiceParticipantDirectory(TimeProvider clock) => this.clock = clock;
    internal const int MaximumMatches = 4096;

    public bool TryBind(Guid matchId, ulong userId, string productUserId, string displayName)
    {
        lock (gate)
        {
            Prune();
            if (!matches.TryGetValue(matchId, out var match))
            {
                if (matches.Count >= MaximumMatches) return false;
                matches[matchId] = match = new();
            }
            string id = productUserId.Trim().ToLowerInvariant();
            if (match.Players.Any(pair => pair.Key != userId && pair.Value.ProductUserId == id)) return false;
            if (!match.Players.ContainsKey(userId) && match.Players.Count >= 3) return false;
            match.LastUsed = clock.GetUtcNow();
            match.Players[userId] = new(id, displayName, userId.ToString(System.Globalization.CultureInfo.InvariantCulture));
            return true;
        }
    }

    public VoiceParticipantRoster Read(Guid matchId, ulong callerId, IReadOnlyDictionary<ulong, string> currentSurvivors)
    {
        lock (gate)
        {
            Prune();
            if (!currentSurvivors.ContainsKey(callerId)) return new([], true);
            if (!matches.TryGetValue(matchId, out var match)) return new([], true);
            match.LastUsed = clock.GetUtcNow();
            return new(match.Players.Where(pair => currentSurvivors.ContainsKey(pair.Key))
                .Select(pair => pair.Value with { DisplayName = currentSurvivors[pair.Key] }).ToArray(),
                !match.Players.ContainsKey(callerId));
        }
    }

    private void Prune()
    {
        var cutoff = clock.GetUtcNow().AddMinutes(-5);
        foreach (var id in matches.Where(pair => pair.Value.LastUsed < cutoff).Select(pair => pair.Key).ToArray())
            matches.Remove(id);
    }
}
internal sealed record VoiceParticipantName(string ProductUserId, string DisplayName, string UserId);
internal sealed record VoiceParticipantRoster(VoiceParticipantName[] Participants, bool NeedsIdentityRefresh);
