namespace ProjectDFSDGitManager;
internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        try
        {
            string? repo = null;
            for (int i = 0; i + 1 < args.Length; i++) if (args[i] == "--repo") repo = args[i + 1];
            if (args.Contains("--self-test")) return SelfTests.Run(repo);
            Application.Run(new ManagerForm(repo)); return 0;
        }
        catch (Exception error) { MessageBox.Show(error.Message, "Git 관리", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
    }
}
