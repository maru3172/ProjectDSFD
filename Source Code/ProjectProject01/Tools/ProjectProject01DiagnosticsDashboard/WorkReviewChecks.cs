namespace ProjectProject01DiagnosticsDashboard;

internal static class WorkReviewChecks
{
    internal static void Run(string root, Action<bool, string> check)
    {
        string fixture = Path.Combine(root, "WorkReview");
        string output = Path.Combine(root, "WorkReviewResults");
        Directory.CreateDirectory(fixture);
        Directory.CreateDirectory(output);
        string projectFile = Path.Combine(fixture, "WorkReview.uproject");
        File.WriteAllText(projectFile, "{}");
        void Write(string path, string source)
        {
            string file = Path.Combine(fixture, path);
            Directory.CreateDirectory(Path.GetDirectoryName(file)!);
            File.WriteAllText(file, source);
        }
        void Git(params string[] args)
        {
            var result = DashboardForm.RunProcessAsync("git", args, workingDirectory: fixture).GetAwaiter().GetResult();
            if (result.ExitCode != 0) throw new InvalidOperationException(result.Output);
        }
        Git("init", "-q");
        const string native = "Source Code/RoomWidget.cpp";
        Write(native, """
            void URoomWidget::JoinRoom()
            {
                RenderPreviousLabel();
                Super::NativeConstruct();
            }
            """);
        Write("Source Code/LobbyTests.cpp", "const char* Screen = \"URoomWidget\";\n// UWitnessWidget\n");
        Write("Source Code/DeletedClass.h", "class UDeletedClass {};\n");
        Git("add", ".");
        Git("-c", "user.email=fixture@example.invalid", "-c", "user.name=Fixture", "-c", "commit.gpgsign=false",
            "-c", "core.hooksPath=.", "commit", "-qm", "Work review fixture");
        Write(native, """
            void URoomWidget::JoinRoom()
            {
                SetText(TEXT("방 참가 안내"));
                Super::NativeConstruct();
            }
            """);
        Write("Source Code/NewFeature.cs", """
            public class RoomEndpoint
            {
                public void Register()
                {
                    app.MapPost("/api/rooms/start", Start);
                }
            }
            """);
        Write("Content/Widget/WBP_Witness.uasset", "fixture; no Unreal asset load");
        Write("Content/Data/DT_Rooms.uasset", "fixture; no Unreal asset load");
        Write("Source Code/WitnessWidget.h", "class UWitnessWidget : public UUserWidget {};\n");
        File.Delete(Path.Combine(fixture, "Source Code/DeletedClass.h"));

        string[] plan = ["방 입장 | JoinRoom", "방 참가 안내", "새 시작 엔드포인트 | /api/rooms/start",
            "관련 없는 요구사항 | NonexistentFeature", native];
        var review = Task.Run(() => WorkReview.RunAsync(UnrealProject.Load(projectFile), plan, output, default, new Progress<string>()))
            .GetAwaiter().GetResult().WorkReview ?? throw new InvalidOperationException("Missing work-review result");
        var items = review.Items;
        check(items.Any(item => item.Area == WorkArea.Functions && item.Name == "URoomWidget::JoinRoom" && item.Line == 1),
            "native method body changes with spaces in Git paths resolve to the owning symbol");
        check(!items.Any(item => item.Name == "Super::NativeConstruct"), "a qualified native call is not a method definition");
        check(items.Any(item => item.Area == WorkArea.Ui && item.Name == "URoomWidget"), "native implementation discovers its widget owner");
        check(items.Any(item => item.Area == WorkArea.Ui && item.Name == "WBP_Witness"), "Unreal widget asset discovered from its changed path");
        check(items.Any(item => item.Area == WorkArea.Assets && item.Name == "DT_Rooms"), "data table assets appear in the changed asset area");
        check(items.Any(item => item.Area == WorkArea.Classes && item.Name == "UDeletedClass"), "deleted native type appears in review");
        check(items.Any(item => item.Area == WorkArea.Functions && item.Name == "MapPost /api/rooms/start"), "new backend endpoint is discovered");
        check(WorkReview.ScanSymbols(["virtual void JoinRoom();", "return JoinRoom();"], ".h")
            .Select(symbol => symbol.Name).SequenceEqual(["JoinRoom"]), "native header declarations can be identified without classifying return calls");
        check(items.Any(item => item.Area == WorkArea.Tests && item.Name.StartsWith("URoomWidget") && item.Status == WorkStatus.Observed),
            "existing test references can be found without a change in the test");
        check(items.Any(item => item.Area == WorkArea.Tests && item.Name.StartsWith("UWitnessWidget") && item.Status == WorkStatus.Review),
            "comment-only mention does not satisfy widget test evidence");
        check(items.Count(item => item.Area == WorkArea.Plan && item.Status == WorkStatus.Missing) == 1,
            "plan matches path, identifier, route and changed Korean text, while unmatched requirement is surfaced");
        check(File.Exists(Path.Combine(output, "Details.txt")), "work review keeps a readable report beside the HUD");

        using var form = new WorkReviewForm(review, output);
        var layout = (TableLayoutPanel)form.Controls[0];
        var areaList = (ListBox)layout.GetControlFromPosition(0, 1)!;
        var table = (DataGridView)layout.GetControlFromPosition(1, 1)!;
        var detail = (TextBox)layout.GetControlFromPosition(1, 2)!;
        areaList.SelectedIndex = 1;
        check(table.Rows.Count == plan.Length && detail.Text.Contains("관련 없는 요구사항"),
            "HUD shows the selected area and displays the first item's review reason");
        var header = (TableLayoutPanel)layout.GetControlFromPosition(0, 0)!;
        ((CheckBox)header.GetControlFromPosition(1, 0)!).Checked = true;
        check(table.Rows.Count == 1 && detail.Text.Contains("근거 미발견"), "HUD can focus on unresolved items");
        ((CheckBox)header.GetControlFromPosition(1, 0)!).Checked = false;
        areaList.SelectedIndex = 3;
        form.ShowInTaskbar = false;
        form.StartPosition = FormStartPosition.Manual;
        form.Location = new Point(SystemInformation.VirtualScreen.Left - form.Width - 20, SystemInformation.VirtualScreen.Top);
        form.Show();
        form.PerformLayout();
        using var preview = new Bitmap(form.Width, form.Height);
        form.DrawToBitmap(preview, new Rectangle(Point.Empty, preview.Size));
        preview.Save(Path.Combine(output, "WorkReviewHud.png"), System.Drawing.Imaging.ImageFormat.Png);
    }
}
