using System.Text.Json;
using System.Xml.Linq;

namespace ProjectProject01DiagnosticsDashboard;

internal static class AutomationVerificationTests
{
    internal static void Run()
    {
        int checks=0;
        void Check(bool value,string name){++checks;if(!value)throw new InvalidOperationException("Automation verification: "+name);}
        string root=Path.GetFullPath("Saved/ReviewRemediation/AutomationRunner-20261022/Fixtures/"+Guid.NewGuid().ToString("N"));
        var first=AutomationVerification.CreateAttempt(root);var second=AutomationVerification.CreateAttempt(root);
        Check(first.Directory!=second.Directory&&!Directory.Exists(first.ReportDirectory),"new attempt never reuses old report directory");
        string[] arguments=AutomationVerification.Arguments("ProjectDFSD.uproject",first);
        Check(!arguments.Contains("-nullrhi")&&arguments.Contains("-RenderOffscreen"),"default suite uses real rendering, not null RHI");
        Check(arguments.Any(a=>a.StartsWith("-ReportExportPath="))&&!arguments.Any(a=>a.StartsWith("-ReportOutputPath=")),"current engine report export option");
        Check(arguments.Single(a=>a.StartsWith("-ExecCmds=")).EndsWith("Group:ProjectDFSDGeneral")&&!arguments.Any(a=>a.Contains(";Quit")),"queue completes via TestExit, never premature Quit");
        Check(arguments.Contains("-abslog="+first.EngineLog),"each attempt keeps its full Unreal log");
        Check(arguments.Single(a=>a.StartsWith("-ExecCmds=")).Contains("Automation RunTests Group:ProjectDFSDGeneral"),"general regression uses the project group with explicit simulation exclusions");
        const string unit = "ProjectProject01.Multiplayer.MannequinNavigationSafety";
        const string simulation = "ProjectProject01.Simulation.BotMatch.Ladder.P5S3.Run30";
        const string simPrefix = "ProjectProject01.Simulation.BotMatch";
        var selected = AutomationRunPlan.Selected([unit, simulation, unit], simPrefix);
        Check(selected.Filter == $"^{unit}$+^{simulation}$" && selected.Simulation && selected.Timeout == TimeSpan.FromMinutes(15),
            "catalog selection is deduplicated, exact, and retains simulation runtime");
        try
        {
            AutomationRunPlan.Selected(["ProjectProject01.Frontend;Quit"], simPrefix);
            throw new InvalidOperationException("unsafe filter accepted");
        }
        catch (ArgumentException) { Check(true, "catalog selections cannot append engine commands"); }
        const string display = "LogAutomationCommandLine: Display: ";
        string[] catalogLines = [display + "Found 3 Automation Tests", display + $"\t'{unit}'", display + "'System.EngineFixture'",
            display + $"\t'{simulation}'", display + "...Automation Test Queue Empty 0 tests performed."];
        var names = AutomationTestCatalog.Parse(catalogLines, "ProjectProject01");
        Check(names.SequenceEqual([unit, simulation]), "native discovery lists project leaves including complex simulation scenarios");
        try
        {
            AutomationTestCatalog.Parse(catalogLines[..^1], "ProjectProject01");
            throw new InvalidOperationException("incomplete catalog accepted");
        }
        catch (InvalidDataException) { Check(true, "incomplete engine output does not replace the catalog"); }
        var bulk = AutomationRunPlan.Selected(Enumerable.Range(1, 700).Select(i => $"ProjectProject01.Simulation.BotMatch.Bulk.Run{i:D3}"), simPrefix);
        string[] batches = AutomationVerification.SplitFilters(bulk.Filter);
        Check(batches.Length > 1 && batches.All(batch => batch.Length <= 8_000) && string.Join('+', batches) == bulk.Filter,
            "bulk exact selections stay below Unreal's command-line ceiling without losing names");
        var batchVerdict = AutomationVerification.CombineBatches([new("Succeeded", "", 0, 5, 2), new("ReportMissing", "")]);
        Check(!batchVerdict.Passed && batchVerdict.Succeeded == 5 && batchVerdict.Warnings == 2, "missing batch evidence blocks overall success");
        using (var catalog = new AutomationCatalogForm(UnrealProject.Load(Path.GetFullPath("ProjectDFSD.uproject")), default))
        {
            catalog.ShowCatalog(new("", DateTimeOffset.UtcNow, names));
            var layout = (TableLayoutPanel)catalog.Controls[0];
            var filters = (TableLayoutPanel)layout.GetControlFromPosition(0, 0)!;
            var search = (TextBox)filters.GetControlFromPosition(1, 0)!;
            var list = (ListView)layout.GetControlFromPosition(0, 2)!;
            _ = list.Handle;
            Check(list.Items.Count == 2, "catalog UI initially exposes all registered items");
            list.Items[0].Checked = true;
            search.Text = "마네킹 우회";
            Check(list.Items.Count == 1 && list.Items[0].Checked, "Korean search preserves checked items");
            search.Text = "시뮬레이션";
            Check(list.Items.Count == 1 && !list.Items[0].Checked, "search does not transfer a check to a different test");
            search.Clear();
            Check(list.Items[0].Checked && !list.Items[1].Checked, "hidden checks reappear without selecting extra tests");
        }
        var entries = ProjectVerification.ReadContainerEntries(
            ["Filename, Offset, Size, Hash, Deleted, Compressed, CompressionMethod",
                "../../../ProjectDFSD/Content/MyProject/Level/TitleLevel.umap, 0, 42, hash, false, true, 1",
                "../../../Engine/Content/MyProject/Level/TitleLevel.umap, 42, 7, hash, false, true, 1",
                "../../../ProjectDFSD/Content/MyProject/Level/Removed.umap, 49, 0, hash, true, false, 0"], "ProjectDFSD");
        Check(entries.SequenceEqual(["MyProject/Level/TitleLevel.umap"]), "package audit excludes same-name Engine files and removed entries");
        const string assetStart = "LogContentValidation: Display: Starting to validate 2 assets (0 associated objects such as actors)";
        const string validAsset = "AssetCheck: /Game/Example/First contains valid data.";
        Check(ProjectVerification.SummarizeAssetValidation(
            [assetStart, validAsset, "AssetCheck: /Game/Example/Second contains valid data, but has warnings."], 0).Passed,
            "Data Validation uses native per-asset evidence when the commandlet omits its aggregate");
        Check(!ProjectVerification.SummarizeAssetValidation(
            [assetStart, validAsset, "AssetCheck: /Game/Example/Second contains invalid data."], 0).Passed,
            "invalid asset cannot pass on commandlet exit code zero");
        Check(!ProjectVerification.SummarizeAssetValidation([assetStart, validAsset], 0).Passed,
            "partial asset validation cannot pass");
        WorkReviewChecks.Run(root, Check);
        int selectable=0;
        foreach(string scenario in AutomationRunPlan.Scenarios)
        {
            int count=scenario.StartsWith("Level",StringComparison.Ordinal)?20:30;
            var plan=AutomationRunPlan.Bot(scenario,1,count);selectable+=count;
            Check(plan.Filter.Split('+').Length==count&&plan.Filter.Split('+').All(f=>f.StartsWith("^ProjectProject01.Simulation.BotMatch.")&&f.EndsWith('$')),"all repetitions have exact isolated simulation selectors");
            Check(plan.Timeout>TimeSpan.FromMinutes(30),"long batches have a finite repetition-based deadline, not the general 30-minute cap");
        }
        Check(selectable==400,"all original 400 balance runs remain selectable");
        using(var options=new BotSimulationOptionsForm())
        {
            var layout=(TableLayoutPanel)options.Controls[0];
            var scenario=(ComboBox)layout.GetControlFromPosition(1,0)!;
            var begin=(NumericUpDown)layout.GetControlFromPosition(1,1)!;
            var repeats=(NumericUpDown)layout.GetControlFromPosition(1,2)!;
            Check(scenario.Items.Count==15&&repeats.Value==5,"UI exposes all combinations with a small default batch");
            scenario.SelectedIndex=14;begin.Value=29;
            Check(repeats.Maximum==2,"UI cannot select past the last ladder repetition");
            begin.Value=30;Check(repeats.Maximum==1,"last repetition remains selectable alone");
            scenario.SelectedIndex=0;
            Check(begin.Value==20&&repeats.Maximum==1,"switching to a shorter scenario safely clamps the range");
            begin.Value=1;repeats.Value=5;
            options.StartPosition=FormStartPosition.Manual;options.Location=new Point(-3000,-3000);
            options.Show();layout.PerformLayout();options.Refresh();
            foreach(float scale in new[]{1f,1.25f,1.6f})
            {
                if(scale!=1f)options.Scale(new SizeF(scale,scale));
                options.PerformLayout();layout.PerformLayout();
                var action=(Button)options.AcceptButton!;
                Rectangle bounds=options.RectangleToClient(action.RectangleToScreen(action.ClientRectangle));
                Check(action.Visible&&options.ClientRectangle.Contains(bounds),"start button remains fully visible at 100/125/200 percent scaling");
            }
            using var image=new Bitmap(options.Width,options.Height);
            options.DrawToBitmap(image,new Rectangle(Point.Empty,image.Size));
            image.Save(Path.Combine(root,"SimulationOptions.png"));
            options.Hide();
        }
        var bot=AutomationRunPlan.Bot("Ladder.H3S5",29,2);
        var botArgs=AutomationVerification.Arguments("ProjectDFSD.uproject",first,bot.Filter);
        Check(botArgs.Single(a=>a.StartsWith("-ExecCmds=")).Contains(bot.Filter)&&!bot.Filter.Contains("Run28"),"simulation uses only requested exact repetitions");
        try{AutomationRunPlan.Bot("Level1",20,2);throw new InvalidOperationException("range accepted");}catch(ArgumentOutOfRangeException){Check(true,"invalid repetition range rejected");}
        var simulationAttempt=AutomationVerification.CreateAttempt(Path.Combine(root,"BotSimulation"));
        AutomationVerification.Save(simulationAttempt,"fixture","Succeeded",0,new("Succeeded","simulation only"),true);
        Check(!File.Exists(Path.Combine(root,"Verification.xml"))&&File.Exists(Path.Combine(root,"BotSimulation","Verification.xml")),"simulation cannot overwrite general verification verdict");
        object Report(int pass=1,int warning=0,int fail=0,int notRun=0,int pending=0,string? date=null)=>new
        {
            reportCreatedOn=date??DateTimeOffset.UtcNow.ToString("yyyy.MM.dd-HH.mm.ss"),succeeded=pass,succeededWithWarnings=warning,failed=fail,notRun,inProcess=pending,
            tests=Enumerable.Range(0,pass+warning+fail+notRun+pending).Select(i=>new{fullTestPath="ProjectProject01.Fixture."+i,state=i<pass+warning?"Success":i<pass+warning+fail?"Fail":i<pass+warning+fail+notRun?"NotRun":"InProcess"}).ToArray()
        };
        void Write(object report){Directory.CreateDirectory(first.ReportDirectory);File.WriteAllText(first.ReportFile,JsonSerializer.Serialize(report));}
        string old=Path.Combine(root,"Verification","AutomationReport");Directory.CreateDirectory(old);File.WriteAllText(Path.Combine(old,"index.json"),JsonSerializer.Serialize(Report(pass:12)));
        var missing=AutomationVerification.Evaluate(first,3);
        Check(missing.Status=="ProcessFailed"&&missing.Succeeded is null&&missing.Failed is null,"crash cannot reuse old twelve-success report");
        Check(AutomationVerification.Evaluate(first,0).Status=="ReportMissing","zero exit without report is not success");
        Write(Report());Check(AutomationVerification.Evaluate(first,0).Passed,"current completed report succeeds");
        Write(Report(pass:0,warning:1));var warning=AutomationVerification.Evaluate(first,0);
        Check(warning.Passed&&warning.Warnings==1,"warning-only successes count as completed, warnings remain visible");
        Write(Report(pass:3,fail:1));Check(AutomationVerification.Evaluate(first,0).Status=="TestFailed","real assertion failure remains failure");
        Write(Report(pass:3));Check(AutomationVerification.Evaluate(first,3).Status=="ProcessFailed","crash after partial success is never passed");
        Write(Report(notRun:1));Check(AutomationVerification.Evaluate(first,0).Status=="Incomplete","not-run tests block full success");
        Write(Report(pending:1));Check(AutomationVerification.Evaluate(first,0).Status=="Incomplete","in-progress tests block full success");
        Write(Report(pass:0));Check(AutomationVerification.Evaluate(first,0).Status=="NoTests","empty suite is not passed");
        Write(Report(date:"2026.10.09-15.50.12"));Check(AutomationVerification.Evaluate(first,0).Status=="StaleReport","copied old report timestamp rejected");
        Write(Report());File.SetLastWriteTimeUtc(first.ReportFile,first.StartedUtc.UtcDateTime.AddDays(-1));
        Check(AutomationVerification.Evaluate(first,0).Status=="StaleReport","old file modification rejected");
        Write(new{succeeded=1,tests=new[]{new{state="Success"}}});Check(AutomationVerification.Evaluate(first,0).Status=="InvalidReport","missing counters never filled with zeros");
        Write(new{succeeded=1,succeededWithWarnings=0,failed=0,notRun=0,inProcess=0,tests=Array.Empty<object>()});
        Check(!AutomationVerification.Evaluate(first,0).Passed,"inconsistent totals cannot pass");
        File.WriteAllText(first.ReportFile,"{truncated");Check(AutomationVerification.Evaluate(first,0).Status=="InvalidReport","truncated report handled without UI exception");
        AutomationVerification.Save(first,"fixture","Running",null,new("Running","검사 중"),true);
        var saved=XDocument.Load(Path.Combine(root,"Verification.xml"));
        Check(saved.Root?.Attribute("FinalOutcome")?.Value=="Inconclusive"&&saved.Root?.Attribute("AttemptId")?.Value==first.Id,"running attempt replaces old verdict, not passed");
        AutomationVerification.Save(first,"fixture","Succeeded",0,missing,true);
        saved=XDocument.Load(Path.Combine(root,"Verification.xml"));var automation=saved.Root!.Element("Automation")!;
        Check(automation.Attribute("SucceededCount") is null&&automation.Attribute("FailedCount") is null,"unknown counts omitted, no fake zero failures");
        Check(automation.Attribute("EngineLog")?.Value.Contains(first.Id)==true&&automation.Attribute("Report")?.Value.Contains(first.Id)==true,"record points only at this attempt's evidence");
        Write(Report());var success=AutomationVerification.Evaluate(first,0);
        AutomationVerification.Save(first,"fixture","Succeeded",0,success,false);
        Check(XDocument.Load(Path.Combine(root,"Verification.xml")).Root?.Attribute("FinalOutcome")?.Value=="ReviewRequired","passing automation does not erase old runtime risks");
        AutomationVerification.Save(second,"fixture","Failed",6,new("NotRun","빌드 실패"),true);
        Check(XDocument.Load(Path.Combine(root,"Verification.xml")).Root?.Attribute("FinalOutcome")?.Value=="ReviewRequired","failed build and unexecuted tests stay distinguishable");
        Check(File.Exists(Path.Combine(first.Directory,"Verification.xml"))&&File.Exists(Path.Combine(old,"index.json")),"previous attempt and original reports preserved");
        Console.WriteLine($"Automation runner self-tests passed ({checks} assertions).");
    }
}
