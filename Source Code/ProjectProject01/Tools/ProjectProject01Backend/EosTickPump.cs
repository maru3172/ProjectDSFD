using System.Collections.Concurrent;

// Called only on the SDK-owning thread: callbacks and native commands keep their original affinity.
internal static class EosTickPump
{
    internal const int IntervalMilliseconds = 10;
    internal const int MaximumCommandsPerIteration = 8;
    internal static int RunIteration(ConcurrentQueue<Action> commands, Action tick, Action<Exception> commandFailed,
        CancellationToken cancellationToken, TimeProvider? clock = null)
    {
        if (cancellationToken.IsCancellationRequested) return 0;
        clock ??= TimeProvider.System;
        long started = clock.GetTimestamp();
        tick(); // Service the SDK before dispatching more work, not after an unbounded queue drain.
        int count = 0;
        while (!cancellationToken.IsCancellationRequested && count < MaximumCommandsPerIteration &&
            clock.GetElapsedTime(started).TotalMilliseconds < 2 && commands.TryDequeue(out var command))
        {
            count++;
            try { command(); }
            catch (Exception exception) { commandFailed(exception); }
        }
        if (!commands.IsEmpty || cancellationToken.IsCancellationRequested) return 0;
        // Native tick/command time counts against the interval instead of adding another fixed 10ms sleep.
        return Math.Clamp((int)Math.Ceiling(IntervalMilliseconds - clock.GetElapsedTime(started).TotalMilliseconds),
            0, IntervalMilliseconds);
    }
}
