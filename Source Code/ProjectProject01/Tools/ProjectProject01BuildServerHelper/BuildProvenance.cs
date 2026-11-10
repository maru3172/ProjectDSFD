using System.Diagnostics;
using System.Reflection.PortableExecutable;
using System.Text.Json;

namespace ProjectProject01BuildServerHelper;

internal static class BuildProvenance
{
    internal sealed record SourceState(string Commit, string[] WorkingFiles, string[] CommitFiles);
    internal sealed record BuildRecord(DateTimeOffset RecordedUtc, string Version, string Module, Guid CodeViewId,
        int CodeViewAge, string PdbName, string Commit, string SourceStatus, string[] Files);
    internal static string DirectoryFor(string projectRoot) => Path.Combine(projectRoot, "Saved", "BuildProvenance");

    internal static async Task<SourceState?> ReadSourceAsync(string root)
    {
        try
        {
            async Task<string> Git(params string[] args)
            {
                var start = new ProcessStartInfo("git")
                {
                    WorkingDirectory = root, UseShellExecute = false, RedirectStandardOutput = true,
                    RedirectStandardError = true, CreateNoWindow = true, StandardOutputEncoding = System.Text.Encoding.UTF8
                };
                foreach (string arg in args) start.ArgumentList.Add(arg);
                using var process = Process.Start(start)!;
                var output = process.StandardOutput.ReadToEndAsync();
                var error = process.StandardError.ReadToEndAsync();
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
                try { await process.WaitForExitAsync(timeout.Token); }
                catch (OperationCanceledException) { if (!process.HasExited) process.Kill(); throw; }
                return process.ExitCode == 0 ? await output : throw new InvalidOperationException(await error);
            }
            string commit = (await Git("rev-parse", "HEAD")).Trim();
            string diff = await Git("diff", "--name-only", "-z", "HEAD", "--", ".");
            string untracked = await Git("ls-files", "--others", "--exclude-standard", "-z", "--", ".");
            string changed = await Git("diff-tree", "--root", "--no-commit-id", "--name-only", "-r", "-z", commit, "--", ".");
            static string[] Names(string text) => text.Split('\0', StringSplitOptions.RemoveEmptyEntries)
                .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
            return new(commit, Names(diff + untracked), Names(changed));
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException or OperationCanceledException)
        {
            return null;
        }
    }

    internal static async Task RecordAsync(string root, string archive, string executable, string version,
        SourceState? before, SourceState? after)
    {
        if (!Directory.Exists(archive)) return;
        string targetName = Path.GetFileNameWithoutExtension(executable);
        var binaries = Directory.EnumerateFiles(archive, targetName + "*.exe", new EnumerationOptions
        {
            RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = true
        }).Where(file => Path.GetDirectoryName(file)!.EndsWith(Path.Combine("Binaries", "Win64"), StringComparison.OrdinalIgnoreCase) &&
            (Path.GetFileName(file).Equals(executable, StringComparison.OrdinalIgnoreCase) ||
             Path.GetFileName(file).StartsWith(targetName + "-Win64-", StringComparison.OrdinalIgnoreCase)));
        foreach (string binary in binaries)
        {
            using var input = File.OpenRead(binary);
            using var pe = new PEReader(input);
            foreach (var entry in pe.ReadDebugDirectory().Where(item => item.Type == DebugDirectoryEntryType.CodeView))
            {
                var codeView = pe.ReadCodeViewDebugDirectoryData(entry);
                string status = before is null || after is null ? "Unknown" :
                    before.Commit != after.Commit || !before.WorkingFiles.SequenceEqual(after.WorkingFiles) ? "ChangedDuringBuild" :
                    before.WorkingFiles.Length > 0 ? "WorkingTree" : "Commit";
                var record = new BuildRecord(DateTimeOffset.UtcNow, version, Path.GetFileName(binary), codeView.Guid,
                    codeView.Age, Path.GetFileName(codeView.Path), before?.Commit ?? "", status,
                    (before?.CommitFiles ?? []).Concat(before?.WorkingFiles ?? []).Distinct(StringComparer.Ordinal).ToArray());
                string directory = DirectoryFor(root);
                Directory.CreateDirectory(directory);
                string name = $"{record.CodeViewId:N}-{record.CodeViewAge}.json";
                string target = Path.Combine(directory, name);
                string temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
                try
                {
                    await File.WriteAllTextAsync(temporary, JsonSerializer.Serialize(record, new JsonSerializerOptions { WriteIndented = true }));
                    File.Move(temporary, target, overwrite: true);
                }
                finally { File.Delete(temporary); }
            }
        }
    }
}
