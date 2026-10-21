using System.Diagnostics;
using System.Globalization;
using System.Text;

namespace ProjectProject01BuildServerHelper;

internal static class ProcessLogEncoding
{
    internal static void Configure(ProcessStartInfo info)
    {
        bool dotnet = Path.GetFileNameWithoutExtension(info.FileName).Equals("dotnet", StringComparison.OrdinalIgnoreCase);
        // Do not change legacy PowerShell/native output unless the command promises UTF-8.
        if (!dotnet && !info.Arguments.Contains("-utf8output", StringComparison.OrdinalIgnoreCase)) return;
        info.StandardOutputEncoding = new UTF8Encoding(false);
        info.StandardErrorEncoding = new UTF8Encoding(false);
        info.Environment["DOTNET_CLI_FORCE_UTF8_ENCODING"] = "true";
        if (!info.Environment.TryGetValue("DOTNET_CLI_UI_LANGUAGE", out string? language) || string.IsNullOrWhiteSpace(language))
            info.Environment["DOTNET_CLI_UI_LANGUAGE"] = CultureInfo.CurrentUICulture.Name;
    }
}
