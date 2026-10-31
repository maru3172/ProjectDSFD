namespace MeetingRecorder;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Contains("--selftest")) return SelfTests.Run();
        ApplicationConfiguration.Initialize();
        // --snapshot <file.png> [width height]: draws the window to a picture (layout check without a person looking).
        if (args.Length >= 2 && args[0] == "--snapshot")
        {
            using var form = new MainForm { StartPosition = FormStartPosition.Manual, Location = new Point(-4000, -4000), ShowInTaskbar = false };
            if (args.Length >= 4 && int.TryParse(args[2], out var w) && int.TryParse(args[3], out var h)) form.Size = new Size(w, h);
            form.Show();Application.DoEvents();
            using var bitmap = new Bitmap(form.Width, form.Height);
            form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
            bitmap.Save(args[1], System.Drawing.Imaging.ImageFormat.Png);
            form.Close();
            return 0;
        }
        Application.Run(new MainForm());
        return 0;
    }
}
