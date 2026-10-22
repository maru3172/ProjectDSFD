// Short-lived identities, not credentials. Counters never expose cached identity content.
internal sealed class VoiceParticipantDirectory
{
    private sealed class MatchEntries
    {
        public DateTimeOffset LastUsed;
        public long PayloadBytes;
        public readonly Dictionary<ulong, VoiceParticipantName> Players = [];
    }
    private readonly object gate = new();
    private readonly Dictionary<Guid, MatchEntries> matches = [];
    private readonly TimeProvider clock;
    private long entryCount,payloadBytes;
    public VoiceParticipantDirectory(TimeProvider clock) => this.clock = clock;
    internal const int MaximumMatches = 4096;

    public bool TryBind(Guid matchId, ulong userId, string productUserId, string displayName)
    {
        lock (gate)
        {
            Prune();
            if (!matches.TryGetValue(matchId, out var match))
            {
                if (matches.Count >= MaximumMatches){BackendDiagnostics.Shared.VoiceRoster.CapacityRejected();return false;}
                matches[matchId] = match = new();
            }
            string id = productUserId.Trim().ToLowerInvariant();
            if (match.Players.Any(pair => pair.Key != userId && pair.Value.ProductUserId == id)) return false;
            if (!match.Players.ContainsKey(userId) && match.Players.Count >= 3) return false;
            match.LastUsed = clock.GetUtcNow();
            if(match.Players.TryGetValue(userId,out var old))
            {long bytes=Payload(old);match.PayloadBytes-=bytes;payloadBytes-=bytes;}
            else ++entryCount;
            match.Players[userId] = new(id, displayName, userId.ToString(System.Globalization.CultureInfo.InvariantCulture));
            long added=Payload(match.Players[userId]);match.PayloadBytes+=added;payloadBytes+=added;
            BackendDiagnostics.Shared.VoiceRoster.Size(entryCount,payloadBytes);
            return true;
        }
    }

    public VoiceParticipantRoster Read(Guid matchId, ulong callerId, IReadOnlyDictionary<ulong, string> currentSurvivors)
    {
        lock (gate)
        {
            Prune();
            if (!currentSurvivors.ContainsKey(callerId)) return new([], true);
            long started=System.Diagnostics.Stopwatch.GetTimestamp();
            bool hit=matches.TryGetValue(matchId, out var match);
            BackendDiagnostics.Shared.VoiceRoster.Lookup(hit,System.Diagnostics.Stopwatch.GetTimestamp()-started);
            if (!hit) return new([], true);
            if(match is null)return new([],true);
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
        {
            var expired=matches[id];entryCount-=expired.Players.Count;payloadBytes-=expired.PayloadBytes;matches.Remove(id);
            // TTL expiry is distinct from capacity eviction. This cache rejects overflow rather than evicting.
            BackendDiagnostics.Shared.VoiceRoster.Expired();
        }
        BackendDiagnostics.Shared.VoiceRoster.Size(entryCount,payloadBytes);
    }
    private static long Payload(VoiceParticipantName entry)=>2L*(entry.ProductUserId.Length+entry.DisplayName.Length+entry.UserId.Length);
}
internal sealed record VoiceParticipantName(string ProductUserId, string DisplayName, string UserId);
internal sealed record VoiceParticipantRoster(VoiceParticipantName[] Participants, bool NeedsIdentityRefresh);
