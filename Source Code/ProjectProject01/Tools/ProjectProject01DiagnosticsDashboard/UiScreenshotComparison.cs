using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace ProjectProject01DiagnosticsDashboard;

internal static class UiScreenshotComparison
{
    public static Task<ProjectCheckResult> RunAsync(string baseline, string candidate, string output, CancellationToken ct) =>
        Task.Run(() =>
        {
            using var left = new Bitmap(baseline);
            using var right = new Bitmap(candidate);
            if (left.Size != right.Size) return new ProjectCheckResult(false,
                $"해상도가 다릅니다. 기준 {left.Width}×{left.Height} · 비교 {right.Width}×{right.Height}");
            if ((long)left.Width * left.Height > 20_000_000)
                return new ProjectCheckResult(false, "비교 이미지는 2천만 픽셀 이하여야 합니다.");

            using var image = new Bitmap(left.Width, left.Height, PixelFormat.Format32bppArgb);
            var region = new Rectangle(Point.Empty, left.Size);
            var first = left.LockBits(region, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var second = right.LockBits(region, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var diff = image.LockBits(region, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            int changed = 0;
            try
            {
                byte[] a = new byte[left.Width * 4], b = new byte[a.Length], row = new byte[a.Length];
                for (int y = 0; y < left.Height; y++)
                {
                    ct.ThrowIfCancellationRequested();
                    Marshal.Copy(first.Scan0 + y * first.Stride, a, 0, a.Length);
                    Marshal.Copy(second.Scan0 + y * second.Stride, b, 0, b.Length);
                    for (int offset = 0; offset < a.Length; offset += 4)
                    {
                        int maximum = 0;
                        for (int channel = 0; channel < 4; channel++)
                            maximum = Math.Max(maximum, Math.Abs(a[offset + channel] - b[offset + channel]));
                        if (maximum > 8) changed++;
                        row[offset] = maximum > 8 ? (byte)0 : (byte)(b[offset] / 4);
                        row[offset + 1] = maximum > 8 ? (byte)20 : (byte)(b[offset + 1] / 4);
                        row[offset + 2] = maximum > 8 ? (byte)255 : (byte)(b[offset + 2] / 4);
                        row[offset + 3] = 255;
                    }
                    Marshal.Copy(row, 0, diff.Scan0 + y * diff.Stride, row.Length);
                }
            }
            finally
            {
                left.UnlockBits(first);
                right.UnlockBits(second);
                image.UnlockBits(diff);
            }
            image.Save(Path.Combine(output, "UI-Diff.png"), ImageFormat.Png);
            double ratio = 100.0 * changed / (left.Width * left.Height);
            string summary = $"픽셀 차이 {changed:N0}개 ({ratio:F3}%). 채널당 8 이하 차이는 제외했습니다. 결과 폴더에서 UI-Diff.png를 확인하세요.";
            File.WriteAllText(Path.Combine(output, "Details.txt"), summary);
            return new ProjectCheckResult(changed == 0, summary);
        }, ct);
}
