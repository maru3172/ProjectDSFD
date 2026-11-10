namespace ProjectProject01BuildServerHelper;

internal sealed class QuestFlowForm : Form
{
    internal sealed record Node(string Title, string Story, string[] Goals, MainQuestTalkCsv.Talk? Talk);

    public string BaseStory { get; private set; }
    public Node[] Nodes { get; }

    public QuestFlowForm(string name, string story, Node[] nodes)
    {
        BaseStory = story;
        Nodes = nodes;
        Text = name + " · 퀘스트·대화 흐름";
        Size = new Size(980, 660);
        MinimumSize = new Size(720, 500);
        StartPosition = FormStartPosition.CenterParent;

        var tree = new TreeView { Dock = DockStyle.Fill, HideSelection = false };
        var editor = new TextBox { Dock = DockStyle.Fill, Multiline = true, ScrollBars = ScrollBars.Vertical };
        var summary = new Label { Dock = DockStyle.Top, Height = 48, AutoEllipsis = true };
        var actions = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 42 };
        var preview = new Button { Text = "이 대화부터 미리보기", AutoSize = true };
        var accept = new Button { Text = "편집 반영", AutoSize = true, DialogResult = DialogResult.OK };
        var cancel = new Button { Text = "돌아가기", AutoSize = true, DialogResult = DialogResult.Cancel };
        actions.Controls.AddRange([preview, accept, cancel]);
        CancelButton = cancel;
        var panes = new SplitContainer { Dock = DockStyle.Fill, Width = 920, SplitterDistance = 320 };
        panes.Panel1.Controls.Add(tree);
        panes.Panel2.Controls.Add(editor);
        panes.Panel2.Controls.Add(summary);
        panes.Panel2.Controls.Add(actions);
        Controls.Add(panes);

        tree.Nodes.Add(new TreeNode("기본 이야기") { Tag = new Selection(-1) });
        for (int step = 0; step < Nodes.Length; ++step)
        {
            var node = Nodes[step];
            var stage = new TreeNode($"{step + 1} · {node.Title}") { Tag = new Selection(step) };
            tree.Nodes.Add(stage);
            if (node.Talk is not { } talk) continue;
            for (int line = 0; line < talk.Lines.Length; ++line)
            {
                var item = talk.Lines[line];
                var entry = new TreeNode(item.Options is null ? $"{line + 1} · {talk.Npc}" : $"{line + 1} · 선택지")
                    { Tag = new Selection(step, line) };
                stage.Nodes.Add(entry);
                if (item.Options is { } options)
                    for (int choice = 0; choice < options.Length; ++choice)
                        entry.Nodes.Add(new TreeNode($"선택 {choice + 1}") { Tag = new Selection(step, line, choice) });
            }
        }

        Selection? selection = null;
        string original = "";
        void Commit()
        {
            if (selection is not { } current || editor.ReadOnly || editor.Text == original) return;
            if (current.Step < 0) { BaseStory = editor.Text; return; }
            var node = Nodes[current.Step];
            if (current.Line < 0) { Nodes[current.Step] = node with { Story = editor.Text }; return; }
            var talk = node.Talk!;
            var lines = talk.Lines.ToArray();
            var line = lines[current.Line];
            if (current.Choice < 0)
            {
                if (string.IsNullOrWhiteSpace(editor.Text)) throw new InvalidDataException("대사를 입력하세요.");
                lines[current.Line] = line with { Text = editor.Text.Trim() };
            }
            else
            {
                var parts = editor.Lines;
                var replies = parts.Skip(1).Select(reply => reply.Trim()).Where(reply => reply.Length != 0).ToArray();
                if (parts.Length == 0 || string.IsNullOrWhiteSpace(parts[0]) || parts[0].Trim().Length > 120 ||
                    replies.Length > 6 || replies.Any(reply => reply.Length > 400))
                    throw new InvalidDataException("첫 줄에 선택지 120자 이내, 다음 줄부터 답장 6개까지 입력하세요. 답장은 줄마다 400자 이내입니다.");
                var options = line.Options!.ToArray();
                options[current.Choice] = new(parts[0].Trim(), replies);
                lines[current.Line] = line with { Options = options };
            }
            Nodes[current.Step] = node with { Talk = talk with { Lines = lines } };
        }

        bool TryCommit()
        {
            try { Commit(); return true; }
            catch (InvalidDataException ex) { MessageBox.Show(this, ex.Message, Text); return false; }
        }

        tree.BeforeSelect += (_, e) => e.Cancel = !TryCommit();
        tree.AfterSelect += (_, e) =>
        {
            selection = (Selection)e.Node!.Tag!;
            var current = selection.Value;
            preview.Enabled = current.Line >= 0;
            editor.ReadOnly = false;
            editor.MaxLength = 16000;
            summary.Text = "";
            if (current.Step < 0) editor.Text = BaseStory;
            else if (current.Line < 0)
            {
                summary.Text = string.Join(" · ", Nodes[current.Step].Goals);
                editor.Text = Nodes[current.Step].Story;
            }
            else
            {
                var line = Nodes[current.Step].Talk!.Lines[current.Line];
                if (current.Choice >= 0)
                {
                    summary.Text = "첫 줄은 선택지, 다음 줄부터는 답장";
                    var option = line.Options![current.Choice];
                    editor.Text = string.Join(Environment.NewLine, new[] { option.Text }.Concat(option.Replies));
                }
                else
                {
                    editor.ReadOnly = line.Options is not null;
                    editor.MaxLength = 400;
                    editor.Text = line.Text ?? "";
                }
            }
            original = editor.Text;
        };
        preview.Click += (_, _) =>
        {
            if (selection is { Line: >= 0 } current && TryCommit())
                ShowDialogue(Nodes[current.Step].Talk!, current.Line, current.Choice);
        };
        FormClosing += (_, e) =>
        {
            if (DialogResult == DialogResult.OK && !TryCommit()) e.Cancel = true;
        };
        tree.SelectedNode = tree.Nodes[0];
    }

    private void ShowDialogue(MainQuestTalkCsv.Talk talk, int from, int choice)
    {
        using var dialog = new Form { Text = talk.Npc + " · 대화 미리보기", Size = new Size(620, 480), StartPosition = FormStartPosition.CenterParent };
        var transcript = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true };
        dialog.Controls.Add(transcript);
        dialog.Controls.Add(buttons);
        void Append(string who, string message) => transcript.AppendText($"{who}  {message}{Environment.NewLine}{Environment.NewLine}");
        void ShowLine(int index, int selected = -1)
        {
            while (buttons.Controls.Count > 0) buttons.Controls[0].Dispose();
            if (index == talk.Lines.Length)
            {
                buttons.Controls.Add(new Button { Text = "닫기", DialogResult = DialogResult.OK });
                return;
            }
            var item = talk.Lines[index];
            void Choose(MainQuestTalkCsv.Option option)
            {
                Append("나", option.Text);
                foreach (var reply in option.Replies) Append(talk.Npc, reply);
                ShowLine(index + 1);
            }
            if (item.Options is { } options)
            {
                if (selected >= 0) { Choose(options[selected]); return; }
                foreach (var option in options)
                {
                    var button = new Button { Text = option.Text, AutoSize = true, MaximumSize = new Size(570, 0) };
                    button.Click += (_, _) => Choose(option);
                    buttons.Controls.Add(button);
                }
                return;
            }
            Append(talk.Npc, item.Text ?? "");
            var next = new Button { Text = "다음", AutoSize = true };
            next.Click += (_, _) => ShowLine(index + 1);
            buttons.Controls.Add(next);
        }
        ShowLine(from, choice);
        dialog.ShowDialog(this);
    }

    private readonly record struct Selection(int Step, int Line = -1, int Choice = -1);
}
