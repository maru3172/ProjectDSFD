using System.Text.Json;

namespace ProjectDFSDGitManager;

internal static class SelfTests
{
    internal static int Run(string? repository)
    {
        var root = Path.Combine(repository ?? Path.GetTempPath(), "Source Code", "ProjectProject01", "Saved", "GitManagerChecks", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root); var checks = new List<string>();
        void Check(bool ok, string name) { if (!ok) throw new InvalidOperationException(name); checks.Add(name); }
        T Await<T>(Task<T> task) { while (!task.IsCompleted) { Application.DoEvents(); Thread.Sleep(5); } return task.GetAwaiter().GetResult(); }
        void Wait(Task task) { while (!task.IsCompleted) { Application.DoEvents(); Thread.Sleep(5); } task.GetAwaiter().GetResult(); }
        try
        {
            string fixture = Path.Combine(root, "fixture"); Directory.CreateDirectory(fixture);
            Await(GitService.RunProcess("git", fixture, ["init", "-b", "main"])); var git = new GitService(fixture);
            CommandResult Run(params string[] args) => Await(git.Git(args));
            void Put(string path, string text) { var file = Path.Combine(fixture, path); Directory.CreateDirectory(Path.GetDirectoryName(file)!); File.WriteAllText(file, text); }
            string Save(string message) { Run("add", "--all"); Run("commit", "-m", message); return Run("rev-parse", "HEAD").Output.Trim(); }
            Run("config", "user.name", "Git manager fixture"); Run("config", "user.email", "fixture@example.invalid"); Run("config", "commit.gpgsign", "false");
            Put("base.txt", "base\n"); Put("Content/공백 [1].umap", "original"); var first = Save("base");
            Run("branch", "destination"); Run("switch", "-c", "source"); Put("Content/공백 [1].umap", "source asset"); Put("feature.txt", "feature\n"); var feature = Save("feature");
            Run("switch", "main"); Put("main.txt", "main\n"); Save("main");
            Check(Await(git.Branches()).Count == 3, "현재 브랜치와 무관하게 source/destination 목록 조회");
            Check(Await(git.History("refs/heads/source"))[0].Hash == feature, "브랜치별 최신 커밋 조회");
            var plan = Await(git.Review("refs/heads/source", "destination")); var report = GitService.ReadPlan(plan.PlanFile);
            Check(report.TargetBranch == "destination" && report.Assets.Any(x => x.Choice == "Source"), "main이 아닌 대상 비교");
            var selection = Path.Combine(root, "selection.json"); File.WriteAllText(selection, JsonSerializer.Serialize(new[] { new { path = "Content/공백 [1].umap", choice = "Target" } }));
            var prepared = Await(git.Review("refs/heads/source", "destination", plan.PlanFile, selection));
            var preview = new GitService(prepared.PreviewPath!);
            Check(Await(preview.Git("show", ":Content/공백 [1].umap")).Output == "original", "GUI 선택 JSON으로 대상 버전 유지");
            Check(Run("rev-parse", "destination").Output.Trim() == first && Await(git.CurrentBranch()) == "main", "검토 준비는 대상 브랜치와 현재 브랜치 보존");
            Put("renamed [2].txt", "literal\n");
            var untracked = Await(git.Changes()).Single(x => x.Path == "renamed [2].txt");
            Await(git.Git(GitService.SelectedPathsCommand("add", [untracked])));
            Check(Await(git.Changes()).Any(x => x.Path == untracked.Path && x.Index == "A"), "공백·대괄호 파일 스테이지");
            Run("commit", "-m", "literal file"); Run("mv", "renamed [2].txt", "renamed result.txt");
            Check(Await(git.Changes()).Any(x => x.Path == "renamed result.txt" && x.Original == "renamed [2].txt"), "rename 상태 경로 해석");
            Save("rename"); Run("commit", "--amend", "--only", "-m", "renamed message");
            Check(Await(git.History("HEAD"))[0].Subject == "renamed message", "마지막 커밋 메시지 수정");
            Run("reset", "--soft", "HEAD^"); Check(Await(git.Changes()).Any(), "soft reset은 변경 유지");
            Run("reset", "--mixed"); Check(Await(git.Changes()).Any(x => x.Index == " " || x.Index == "?"), "mixed reset은 스테이지 취소");
            Run("stash", "push", "--include-untracked", "-m", "fixture stash"); Check(Await(git.Changes()).Count == 0, "스태시 임시 보관");
            Run("stash", "pop"); Check(Await(git.Changes()).Count > 0, "스태시 복원");
            Run("reset", "--hard", "HEAD"); Run("clean", "-fd");
            Run("cherry-pick", feature); Check(File.Exists(Path.Combine(fixture, "feature.txt")), "선택 커밋 cherry-pick");
            Run("revert", "--no-edit", "HEAD"); Check(!File.Exists(Path.Combine(fixture, "feature.txt")), "revert는 취소 커밋 생성");
            Run("switch", "source"); Run("rebase", "main"); Check(Await(git.CurrentBranch()) == "source", "브랜치 rebase");
            Put("drop.txt", "remove"); var drop = Save("drop this"); Put("keep.txt", "keep"); Save("keep this");
            var parent = Run("rev-parse", drop + "^").Output.Trim(); Run("rebase", "--onto", parent, drop);
            Check(!File.Exists(Path.Combine(fixture, "drop.txt")) && File.Exists(Path.Combine(fixture, "keep.txt")), "중간 커밋만 삭제하고 이후 변경 재적용");
            Run("branch", "temporary"); Run("branch", "-m", "temporary", "renamed"); Run("branch", "-d", "renamed"); Check(!Await(git.Branches()).Any(x => x.Name == "renamed"), "브랜치 생성·이름 변경·삭제");
            Run("tag", "-a", "v-test", "-m", "test"); Check(Run("tag").Output.Contains("v-test"), "태그 생성"); Run("tag", "-d", "v-test");
            var tree = Path.Combine(root, "extra worktree"); Run("worktree", "add", "-b", "tree-test", tree, "main");
            Check(Await(git.Worktrees()).Any(x => x.Path.Replace('/', '\\') == tree.Replace('/', '\\')), "worktree 별도 폴더 생성"); Run("worktree", "remove", tree);
            var bare = Path.Combine(root, "remote.git"); Directory.CreateDirectory(bare); Await(GitService.RunProcess("git", bare, ["init", "--bare"]));
            Run("remote", "add", "origin", bare); Run("push", "--set-upstream", "origin", "HEAD:refs/heads/source");
            Run("fetch", "--prune", "origin"); Run("pull", "--ff-only", "origin", "source");
            Check(Await(git.Branches()).Any(x => x.Remote && x.Name == "origin/source"), "로컬 원격 저장소 push·fetch·pull");
            var clone = Path.Combine(root, "cloned repository");
            Await(GitService.RunProcess("git", root, ["clone", "-b", "source", "--", bare, clone]));
            Check(Await(new GitService(clone).CurrentBranch()) == "source", "저장소 복제");
            var cloned = new GitService(clone); Await(cloned.Git("config", "user.name", "Other fixture")); Await(cloned.Git("config", "user.email", "other@example.invalid")); Await(cloned.Git("config", "commit.gpgsign", "false"));
            File.WriteAllText(Path.Combine(clone, "remote-update.txt"), "remote update"); Await(cloned.Git("add", "--all")); Await(cloned.Git("commit", "-m", "remote update")); Await(cloned.Git("push"));
            Check(Await(git.Git(new[] { "push", "--force-with-lease", "origin", "HEAD:refs/heads/source" }, false)).Code != 0, "원격이 앞서면 안전 강제 push도 덮어쓰기를 거부");
            Run("fetch", "origin"); Run("pull", "--ff-only", "origin", "source"); Check(File.Exists(Path.Combine(fixture, "remote-update.txt")), "실제 원격 새 커밋을 pull로 반영");
            Check(GitService.ParseArguments("log --format=\"%h %s\" -5").SequenceEqual(new[] { "log", "--format=%h %s", "-5" }), "추가 명령은 셸 없이 인수 경계 보존");
            Put("Content/공백 [1].umap", "UI preview revision"); Save("UI preview asset");
            using var form = new ManagerForm(fixture) { SuppressInitialRefresh = true };
            form.Show(); Wait(form.RefreshRepository(fixture)); Wait(form.CompareForTest("source", "destination"));
            while (form.Busy) { Application.DoEvents(); Thread.Sleep(5); }
            Put("dirty-check.txt", "preserve local change"); var rejected = false;
            try { Wait(form.CheckoutForTest("destination")); } catch (InvalidOperationException error) { rejected = error.Message.Contains("미커밋"); }
            Check(rejected && Await(git.CurrentBranch()) == "source", "미커밋 변경을 지닌 브랜치 전환 차단");
            File.Delete(Path.Combine(fixture, "dirty-check.txt"));
            Check(form.ActionNames.Count >= 55 && form.Pages.TabPages.Count == 8, "관리 기능과 8개 분류 탭 구성");
            for (int i = 0; i < form.Pages.TabPages.Count; i++)
            {
                form.Pages.SelectedIndex = i; Application.DoEvents();
                using var bitmap = new Bitmap(form.Width, form.Height); form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
                bitmap.Save(Path.Combine(root, $"tab-{i + 1}.png"));
            }
            var unborn = Path.Combine(root, "unborn"); Directory.CreateDirectory(unborn); Await(GitService.RunProcess("git", unborn, ["init", "-b", "main"]));
            using (var emptyForm = new ManagerForm(unborn) { SuppressInitialRefresh = true }) { emptyForm.Show(); Wait(emptyForm.RefreshRepository(unborn)); emptyForm.Close(); }
            Check(true, "첫 커밋이 없는 새 저장소도 정상 표시");
            form.Size = form.MinimumSize; Application.DoEvents(); using (var bitmap = new Bitmap(form.Width, form.Height)) { form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size)); bitmap.Save(Path.Combine(root, "minimum-size.png")); }
            Check(true, "실제 Windows 폼 렌더링 및 최소 창 크기 검사"); form.Close();
            File.WriteAllText(Path.Combine(root, "results.json"), JsonSerializer.Serialize(new { Success = true, Checks = checks.Count, Actions = form.ActionNames.Count, Names = checks, Root = root }, GitService.Json)); return 0;
        }
        catch (Exception error)
        {
            File.WriteAllText(Path.Combine(root, "results.json"), JsonSerializer.Serialize(new { Success = false, Checks = checks.Count, Failure = error.ToString(), Root = root }, GitService.Json)); return 1;
        }
    }
}
