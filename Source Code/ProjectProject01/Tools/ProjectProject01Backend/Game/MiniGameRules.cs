/// <summary>
/// Rules of the phone mini games that the server checks itself (Docs/MiniGames.md). The client
/// (Source/ProjectProject01/Private/ProjectMiniGameRules.cpp) follows exactly the same rules; both sides are tested on
/// the same stages and move lists.
/// </summary>
internal static class MiniGameRules
{
    // ---- 움직였다 (spot): the server makes each round; the client only draws it. ----
    internal sealed record SpotFigure(int X, int Y, int Head, int Arm);
    internal sealed record SpotRound(int Round, SpotFigure[] Before, SpotFigure[] After, int[] Changed);

    /// <summary>Round n of a run: more mannequins and smaller changes as it goes. The same seed and round always give the same picture.</summary>
    internal static SpotRound MakeSpotRound(int seed, int round)
    {
        var random = new Random(unchecked(seed * 7919 + round * 104729));
        int count = Math.Min(4 + round, 12), changes = 1 + (round >= 4 ? 1 : 0) + (round >= 8 ? 1 : 0), step = Math.Max(26, 110 - round * 9);
        var before = new List<SpotFigure>();
        // Spread over a 1000x1000 picture; never closer than 90 apart so every figure can be clicked on its own.
        for (int tries = 0; before.Count < count && tries < 4000; tries++)
        {
            var f = new SpotFigure(random.Next(80, 921), random.Next(260, 900), random.Next(3), random.Next(3));
            if (before.All(o => Math.Abs(o.X - f.X) >= 90 || Math.Abs(o.Y - f.Y) >= 120)) before.Add(f);
        }
        var after = before.ToArray();
        var changed = Enumerable.Range(0, before.Count).OrderBy(_ => random.Next()).Take(changes).OrderBy(i => i).ToArray();
        foreach (int i in changed)
        {
            var f = after[i];
            switch (random.Next(3))
            {
                case 0: // a step to the side
                    int dx = random.Next(2) == 0 ? -step : step, x = Math.Clamp(f.X + dx, 60, 940);
                    if (x == f.X) x = Math.Clamp(f.X - dx, 60, 940);
                    after[i] = f with { X = x }; break;
                case 1: after[i] = f with { Head = (f.Head + 1 + random.Next(2)) % 3 }; break;
                default: after[i] = f with { Arm = (f.Arm + 1 + random.Next(2)) % 3 }; break;
            }
        }
        return new SpotRound(round, [.. before], after, changed);
    }
    internal const int SpotLives = 3;
    internal static int SpotPoints(int round) => 100 + 25 * round;

    // ---- 한 칸씩 (steps) ----
    internal sealed record StepStage(string Id, string Name, string[] Map, char Facing, int Shutters, int Par);

    /// <summary>
    /// '#' wall, '.' floor, 'S' start, 'E' exit, 'M' mannequin, 'X' a doorway with a fire shutter that can be lowered.
    /// Par is the fewest actions the solver found (checked by MiniGameChecks).
    /// </summary>
    internal static readonly StepStage[] Stages =
    [
        new("1", "첫 걸음", ["########", "##E..M.#", "##.....#", "##.#...#", "##.#..##", "##S.#..#", "########"], 'R', 0, 8),
        new("2", "돌아서 가기", ["########", "#...E..#", "##.....#", "#.#.M..#", "##...#.#", "#..#.S.#", "########"], 'L', 0, 9),
        new("3", "두 마네킹", ["########", "#.E...M#", "#.#..#.#", "#...#.M#", "#...##.#", "#S.....#", "########"], 'L', 0, 9),
        new("4", "첫 셔터", ["#########", "#....ME##", "#.......#", "#..##...#", "#X#.....#", "#S.....##", "#########"], 'U', 1, 12),
        new("5", "엇갈린 길", ["#########", "##.EM...#", "#.##.M..#", "#.##.#..#", "#..#....#", "##......#", "#..##S.##", "#########"], 'D', 0, 11),
        new("6", "문 닫고 가기", ["#######", "#...ME#", "#.....#", "#X#...#", "#...#M#", "#.S.###", "#######"], 'L', 1, 12),
        new("7", "셋", ["########", "#E#.#..#", "#M.....#", "#.##..M#", "#.M..#.#", "#.#....#", "##.S..##", "########"], 'U', 0, 15),
        new("8", "뒤를 막아라", ["#######", "#ME...#", "#.....#", "#M....#", "#...#.#", "#..#..#", "#..X.S#", "#######"], 'U', 1, 13),
        new("9", "두 개의 셔터", ["########", "#.M#.#E#", "##...M.#", "#.#.#..#", "#.#.X.##", "#M#.#..#", "#.XS..##", "########"], 'D', 2, 13),
        new("10", "폐점", ["########", "#ME.M.M#", "#...#..#", "##.#...#", "##.#...#", "#XSX...#", "########"], 'R', 2, 15),
    ];

    internal enum StepResult { Continue, Won, Caught, Invalid }

    /// <summary>
    /// One position of a stage. After every step or shutter the mannequins move one cell toward the player, in reading
    /// order; the one the player is looking at stays put. Turning on the spot is free: it is not an action and nothing moves.
    /// </summary>
    internal sealed class StepState
    {
        public readonly char[][] Grid; public int Px, Py; public char Facing; public int ShuttersLeft, Actions;
        public readonly List<(int X, int Y)> Mannequins = [];
        public StepState(StepStage stage)
        {
            Grid = stage.Map.Select(r => r.ToCharArray()).ToArray(); Facing = stage.Facing; ShuttersLeft = stage.Shutters;
            for (int y = 0; y < Grid.Length; y++) for (int x = 0; x < Grid[y].Length; x++)
            {
                if (Grid[y][x] == 'S') { Px = x; Py = y; Grid[y][x] = '.'; }
                else if (Grid[y][x] == 'M') { Mannequins.Add((x, y)); Grid[y][x] = '.'; }
            }
        }
        static (int, int) Delta(char d) => d switch { 'U' => (0, -1), 'D' => (0, 1), 'L' => (-1, 0), _ => (1, 0) };
        char At(int x, int y) => y < 0 || y >= Grid.Length || x < 0 || x >= Grid[y].Length ? '#' : Grid[y][x];
        bool Blocked(int x, int y) => At(x, y) is '#' or 'C'; // 'C' a lowered shutter
        int MannequinAt(int x, int y) => Mannequins.FindIndex(m => m.X == x && m.Y == y);
        /// <summary>The first mannequin in a straight line ahead of the player (walls and lowered shutters stop the view).</summary>
        public int Watched()
        {
            var (dx, dy) = Delta(Facing);
            for (int x = Px + dx, y = Py + dy; !Blocked(x, y); x += dx, y += dy) { int m = MannequinAt(x, y); if (m >= 0) return m; }
            return -1;
        }
        /// <summary>"U/D/L/R" step (and face that way), "tU/tD/tL/tR" turn on the spot, "S" lower the shutter just ahead.</summary>
        public StepResult Apply(string action)
        {
            if (action.Length == 1 && "UDLR".Contains(action[0]))
            {
                var (dx, dy) = Delta(action[0]); int nx = Px + dx, ny = Py + dy;
                if (Blocked(nx, ny) || MannequinAt(nx, ny) >= 0) return StepResult.Invalid;
                Px = nx; Py = ny; Facing = action[0]; Actions++;
                if (At(Px, Py) == 'E') return StepResult.Won;
            }
            else if (action.Length == 2 && action[0] == 't' && "UDLR".Contains(action[1]))
            {
                if (Facing == action[1]) return StepResult.Invalid;
                Facing = action[1]; return StepResult.Continue;
            }
            else if (action == "S")
            {
                var (dx, dy) = Delta(Facing); int sx = Px + dx, sy = Py + dy;
                if (ShuttersLeft <= 0 || At(sx, sy) != 'X' || MannequinAt(sx, sy) >= 0) return StepResult.Invalid;
                Grid[sy][sx] = 'C'; ShuttersLeft--; Actions++;
            }
            else return StepResult.Invalid;
            return MoveMannequins();
        }
        StepResult MoveMannequins()
        {
            int watched = Watched();
            for (int i = 0; i < Mannequins.Count; i++)
            {
                if (i == watched) continue;
                var (mx, my) = Mannequins[i]; int dx = Px - mx, dy = Py - my;
                var tries = new List<(int, int)>();
                (int, int) h = (Math.Sign(dx), 0), v = (0, Math.Sign(dy));
                if (Math.Abs(dx) >= Math.Abs(dy)) { if (dx != 0) tries.Add(h); if (dy != 0) tries.Add(v); }
                else { if (dy != 0) tries.Add(v); if (dx != 0) tries.Add(h); }
                foreach (var (sx, sy) in tries)
                {
                    int nx = mx + sx, ny = my + sy;
                    if (Blocked(nx, ny) || MannequinAt(nx, ny) >= 0) continue;
                    Mannequins[i] = (nx, ny);
                    if (nx == Px && ny == Py) return StepResult.Caught;
                    break;
                }
            }
            return StepResult.Continue;
        }
        public string Key() => $"{Px},{Py},{Facing},{ShuttersLeft},{string.Join(';', Mannequins.Select(m => m.X + "." + m.Y))},{string.Concat(Grid.Select(r => new string(r)))}";
        public StepState Clone()
        {
            var grid = Grid.Select(r => (char[])r.Clone()).ToArray();
            var copy = new StepState(grid, Px, Py, Facing, ShuttersLeft, Actions, [.. Mannequins]);
            return copy;
        }
        StepState(char[][] grid, int px, int py, char facing, int shutters, int actions, List<(int, int)> mannequins)
        { Grid = grid; Px = px; Py = py; Facing = facing; ShuttersLeft = shutters; Actions = actions; Mannequins = mannequins; }
    }

    internal static readonly string[] StepActions = ["U", "D", "L", "R", "tU", "tD", "tL", "tR", "S"];

    /// <summary>Plays a move list from the start. Returns the result and the number of actions, or Invalid at the first illegal one.</summary>
    internal static (StepResult Result, int Actions) Replay(StepStage stage, IReadOnlyList<string> moves)
    {
        var state = new StepState(stage);
        foreach (var move in moves)
        {
            var r = state.Apply(move);
            if (r != StepResult.Continue) return (r, state.Actions);
        }
        return (StepResult.Continue, state.Actions);
    }

    /// <summary>
    /// The fewest-action win (turns are free, so they cost nothing), used by the checks to prove every stage can be won
    /// and to set par. Returns the move list including the turns it needs.
    /// </summary>
    internal static List<string>? Solve(StepStage stage, int maxActions = 40)
    {
        var start = new StepState(stage);
        var queue = new LinkedList<(StepState State, List<string> Path)>(); queue.AddLast((start, []));
        var best = new Dictionary<string, int> { [start.Key()] = 0 };
        while (queue.Count > 0)
        {
            var (state, path) = queue.First!.Value; queue.RemoveFirst();
            if (best.TryGetValue(state.Key(), out int known) && known < state.Actions) continue;
            if (state.Actions >= maxActions) continue;
            foreach (var action in StepActions)
            {
                var next = state.Clone(); var r = next.Apply(action);
                if (r is StepResult.Invalid or StepResult.Caught) continue;
                var p = new List<string>(path) { action };
                if (r == StepResult.Won) return p;
                string key = next.Key();
                if (best.TryGetValue(key, out int seen) && seen <= next.Actions) continue;
                best[key] = next.Actions;
                if (action[0] == 't') queue.AddFirst((next, p)); else queue.AddLast((next, p));
            }
        }
        return null;
    }

    /// <summary>Stars for a cleared stage: 1 cleared, 2 within par+3 actions, 3 at par.</summary>
    internal static int Stars(StepStage stage, int actions) => actions <= stage.Par ? 3 : actions <= stage.Par + 3 ? 2 : 1;
}
