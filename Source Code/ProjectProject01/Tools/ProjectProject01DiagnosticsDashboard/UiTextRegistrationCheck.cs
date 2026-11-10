using System.Text;
using System.Text.RegularExpressions;
using TextFieldParser = Microsoft.VisualBasic.FileIO.TextFieldParser;

namespace ProjectProject01DiagnosticsDashboard;

internal static partial class UiTextRegistrationCheck
{
    public static Task<ProjectCheckResult> RunAsync(string root, string output, CancellationToken token) =>
        Task.Run(() =>
        {
            string catalogue = Path.Combine(root, "Content", "Localization", "ProjectProject01UI.csv");
            if (!File.Exists(catalogue)) return new ProjectCheckResult(false, "UI 문자열 테이블을 찾지 못했습니다.");
            var entries = new HashSet<string>(StringComparer.Ordinal);
            var findings = new List<string>();
            using (var csv = new TextFieldParser(catalogue, new UTF8Encoding(false, true))
                { HasFieldsEnclosedInQuotes = true, TrimWhiteSpace = false })
            {
                csv.SetDelimiters(",");
                if (!(csv.ReadFields() ?? []).SequenceEqual(new[] { "Key", "SourceString", "Comment" }))
                    return new ProjectCheckResult(false, "문자열 테이블 머리글을 확인하세요.");
                while (!csv.EndOfData)
                {
                    token.ThrowIfCancellationRequested();
                    long line = csv.LineNumber;
                    string[]? row = csv.ReadFields();
                    if (row is not { Length: 3 } || string.IsNullOrWhiteSpace(row[0]) || string.IsNullOrWhiteSpace(row[1]) ||
                        row[1].Contains('\uFFFD') || !entries.Add(row[0]))
                        findings.Add($"CSV {line}행 · 빈 키, 누락, 중복 또는 깨진 글자");
                }
            }
            int literals = 0;
            string code = Path.Combine(root, "Source", "ProjectProject01");
            foreach (string path in Directory.EnumerateFiles(code, "*.cpp", SearchOption.AllDirectories)
                .Where(path => !Path.GetFileNameWithoutExtension(path).EndsWith("Test", StringComparison.OrdinalIgnoreCase) &&
                               !Path.GetFileNameWithoutExtension(path).EndsWith("Tests", StringComparison.OrdinalIgnoreCase)))
            {
                token.ThrowIfCancellationRequested();
                string source = File.ReadAllText(path);
                foreach (Match match in LiteralKey().Matches(source))
                {
                    literals++;
                    string key = match.Groups[1].Value.Replace(@"\n", "\n").Replace(@"\r", "\r").Replace("\\\"", "\"");
                    if (!entries.Contains(key))
                        findings.Add($"{Path.GetRelativePath(root, path)} · 미등록: {key.Replace("\n", " ")}");
                }
            }
            var issues = findings.Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
            string summary = $"등록 키 {entries.Count}개 · 코드의 고정 문자열 참조 {literals}개 · 검토 항목 {issues.Length}개";
            File.WriteAllLines(Path.Combine(output, "Details.txt"), new[] { summary }.Concat(issues));
            return new ProjectCheckResult(issues.Length == 0, summary);
        }, token);

    [GeneratedRegex("""\bFProjectProject01Localization::Text\s*\(\s*TEXT\s*\(\s*"((?:\\.|[^"\\])*)"\s*\)\s*\)""")]
    private static partial Regex LiteralKey();
}
