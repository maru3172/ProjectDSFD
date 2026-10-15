using System.Reflection;
using System.Text.Json;
using ProjectProject01BuildServerHelper;

internal static class FeatureChecks
{
    private static int checks;
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length > 0 && args[0] == "--integration")
            {
                IntegrationChecks.RunAsync(args[1],int.Parse(args[2])).GetAwaiter().GetResult();
                return 0;
            }
            string id=Guid.NewGuid().ToString("D");
            var single=new SurveyQuestionDefinition(id,"Choice","질문",["A","B","C"],true);
            Check(SurveyEndpoints.ValidateDefinition(new("호환성","",[single]),out _),"기존 문항 JSON 호환");
            var old=JsonSerializer.Deserialize<SurveyQuestionDefinition>(JsonSerializer.Serialize(new {Id=id,Type="Choice",Prompt="질문",Options=new[]{"A","B"},Required=true}))!;
            Check(!old.AllowMultiple && !old.IsTrap && SurveyEndpoints.SelectionLimit(old)==1,"새 필드 없는 문항 기본값");
            Check(SurveyEndpoints.ValidateAnswers([single],[new(id,"A")],out _),"기존 단일 응답 허용");
            Check(!SurveyEndpoints.ValidateAnswers([single],[new(id,"A, B",["A","B"])],out _),"단일 선택에서 복수 응답 차단");
            var multiple=single with {AllowMultiple=true,MaxSelections=2};
            Check(SurveyEndpoints.ValidateAnswers([multiple],[new(id,"A, B",["A","B"])],out _),"복수 응답 허용");
            Check(!SurveyEndpoints.ValidateAnswers([multiple],[new(id,"A, B, C",["A","B","C"])],out _),"최대 응답 수 서버 검증");
            Check(!SurveyEndpoints.ValidateAnswers([multiple],[new(id,"A, A",["A","A"])],out _),"중복 선택 차단");
            Check(!SurveyEndpoints.ValidateAnswers([multiple],[new(id,"외부",["외부"])],out _),"등록되지 않은 선택지 차단");
            Check(SurveyEndpoints.SelectionLimit(multiple with {MaxSelections=0})==3,"최대 개수 미지정시 전체 선택지 허용");
            var trap=single with {IsTrap=true,ExpectedAnswers=["B"]};
            Check(SurveyEndpoints.TrapMatches(trap,new(id,"B")),"함정문항 정답 반영");
            Check(!SurveyEndpoints.TrapMatches(trap,new(id,"A")),"함정문항 오답 보류");
            Check(!SurveyEndpoints.ValidateDefinition(new("잘못된 정답","",[trap with {ExpectedAnswers=["없음"]}]),out _),"잘못된 함정 정답 차단");
            var text=new SurveyQuestionDefinition(id,"Text","주관식",[],true,IsTrap:true,ExpectedAnswers:["확인"]);
            Check(SurveyEndpoints.TrapMatches(text,new(id," 확인 ")),"주관식 정답 비교");
            var publicMethod=typeof(SurveyEndpoints).GetMethod("ToPublicResponse",BindingFlags.Static|BindingFlags.NonPublic)!;
            var surveyType=typeof(SurveyEndpoints).GetNestedType("SurveyRow",BindingFlags.NonPublic)!;
            var row=Activator.CreateInstance(surveyType,id,"함정","",JsonSerializer.Serialize(new[]{trap}),true)!;
            string publicJson=JsonSerializer.Serialize(publicMethod.Invoke(null,[row]));
            Check(!publicJson.Contains("ExpectedAnswers") && !publicJson.Contains("expectedAnswers") && !publicJson.Contains("isTrap"),"클라이언트에 함정 정답 비공개");
            Check(publicJson.Contains("\"allowMultiple\"") && publicJson.Contains("\"id\""),"수정된 문항도 클라이언트 camelCase 유지");
            string scrubbed=CrashArtifactEndpoints.ScrubText("access_token=secret123 password=pass456 Bearer token789\n<CommandLine>-unknown=hidden</CommandLine>");
            Check(!scrubbed.Contains("secret123") && !scrubbed.Contains("pass456") && !scrubbed.Contains("token789") && !scrubbed.Contains("hidden"),"로그와 컨텍스트 비밀값 제거");
            string structured=CrashArtifactEndpoints.ScrubText("{\"password\":\"spaced secret\"}<AccessToken>xml-secret</AccessToken>");
            Check(!structured.Contains("spaced secret")&&!structured.Contains("xml-secret"),"JSON 및 XML 비밀값 제거");
            Check(!CrashArtifactEndpoints.Names.Contains("../secret.txt") && !CrashArtifactEndpoints.Names.Contains("password.json"),"덤프 첨부 경로 및 확장자 제한");
            var daily=SurveyEndpoints.FillDailyGaps([new("2026-10-01",2,1,1),new("2026-10-03",3,3,0)]);
            Check(daily.Count==3 && daily[1].Date=="2026-10-02" && daily[1].ResponseCount==0,"꺾은선 무응답 날짜 0건 처리");
            if(args.Length>0) CheckLayouts(args[0]);
            Console.WriteLine($"PASS: {checks} checks");return 0;
        }
        catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
    }
    private static void Check(bool value,string name)
    {
        if(!value)throw new InvalidOperationException(name);
        checks++; Console.WriteLine($"PASS {name}");
    }
    private static IEnumerable<Control> Children(Control control)
    {
        foreach(Control child in control.Controls){yield return child;foreach(var descendant in Children(child))yield return descendant;}
    }
    private static void CheckLayouts(string output)
    {
        Directory.CreateDirectory(output);
        HttpRequestMessage MockRequest(HttpMethod method,string endpoint,object? body)=>new(method,"http://127.0.0.1:1"+endpoint);
        using(var helper=new MainForm())
        {
            PrepareRender(helper);
            foreach(string caption in new[]{"동시 경기 수 적용","방 한도 설정·적용"})
            {
                var button=Children(helper).OfType<Button>().Single(b=>b.Text==caption);
                Check(helper.ClientRectangle.Contains(helper.RectangleToClient(button.RectangleToScreen(button.ClientRectangle))),caption+" 버튼이 메인 창에 표시됨");
            }
            using var bitmap=new Bitmap(helper.Width,helper.Height);helper.DrawToBitmap(bitmap,new Rectangle(Point.Empty,helper.Size));bitmap.Save(Path.Combine(output,"helper-capacity-buttons.png"));
        }
        using(var administration=new ServerAdministrationForm(Path.GetFullPath(Path.Combine(output,"empty-fixture")),_=>{}))
        {
            PrepareRender(administration);
            using var bitmap=new Bitmap(administration.Width,administration.Height);administration.DrawToBitmap(bitmap,new Rectangle(Point.Empty,administration.Size));bitmap.Save(Path.Combine(output,"server-administration.png"));
            Check(Children(administration).OfType<Button>().Any(b=>b.Text=="최대 방 수")&&Children(administration).OfType<Button>().Any(b=>b.Text=="계정 이용 제한"),"서버 운영에서 방 한도·계정 제한 접근");
            foreach(var button in Children(administration).OfType<Button>().Where(b=>b.Text is "최대 방 수" or "계정 이용 제한"))
                Check(administration.ClientRectangle.Contains(administration.RectangleToClient(button.RectangleToScreen(button.ClientRectangle))),button.Text+" 버튼이 창 밖으로 잘리지 않음");
        }
        using(var capacity=new RoomCapacityForm(MockRequest,_=>{}))
        {
            PrepareRender(capacity);
            using var bitmap=new Bitmap(capacity.Width,capacity.Height);capacity.DrawToBitmap(bitmap,new Rectangle(Point.Empty,capacity.Size));bitmap.Save(Path.Combine(output,"room-capacity.png"));
            Check(Children(capacity).OfType<NumericUpDown>().Single().Maximum==100,"방 한도 설정 UI 8개 제한 제거");
        }
        using(var restrictions=new AccountRestrictionForm(MockRequest))
        {
            PrepareRender(restrictions);
            var history=Children(restrictions).OfType<DataGridView>().Single();
            history.Rows.Add("test-account","운영 검증","오늘","영구","","제한 중");
            using var bitmap=new Bitmap(restrictions.Width,restrictions.Height);restrictions.DrawToBitmap(bitmap,new Rectangle(Point.Empty,restrictions.Size));bitmap.Save(Path.Combine(output,"account-restrictions.png"));
            Check(history.Height>200&&history.Columns.Count==6,"이용 제한 이력 UI 가시 영역");
        }
        using(var crashes=new CrashReportDownloadForm([("test","ZIP 다운로드 위치 안내")],_=>Task.FromResult(Array.Empty<byte>())))
        {
            PrepareRender(crashes);
            using var bitmap=new Bitmap(crashes.Width,crashes.Height);crashes.DrawToBitmap(bitmap,new Rectangle(Point.Empty,crashes.Size));bitmap.Save(Path.Combine(output,"crash-download.png"));
            Check(Children(crashes).OfType<Label>().Any(l=>l.Text.Contains("선택한 PC 경로")),"ZIP 저장 위치 UI 안내");
        }
        using(var recovery=new DatabaseRecoveryForm(Path.GetFullPath(Path.Combine(output,"empty-fixture")),()=>false,(_,_)=>Task.CompletedTask))
        {
            PrepareRender(recovery);
            using var bitmap=new Bitmap(recovery.Width,recovery.Height);recovery.DrawToBitmap(bitmap,new Rectangle(Point.Empty,recovery.Size));bitmap.Save(Path.Combine(output,"database-recovery.png"));
            Check(Children(recovery).OfType<ListBox>().Single().Height>200,"DB 백업 목록 UI 가시 영역");
        }
        using var form=new SurveyAdministrationForm("http://127.0.0.1:1","test",_=>{});
        var loadDraft=typeof(SurveyAdministrationForm).GetMethod("LoadSelectedDraftAsync",BindingFlags.Instance|BindingFlags.NonPublic)!;
        ((Task)loadDraft.Invoke(form,null)!).GetAwaiter().GetResult();
        Check(Children(form).OfType<Label>().Any(l=>l.Text.Contains("불러올 설문")),"선택 없는 설문 불러오기는 예외 대신 안내");
        var savedList=(ListBox)typeof(SurveyAdministrationForm).GetField("surveyList",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(form)!;
        typeof(Control).GetMethod("OnDoubleClick",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(savedList,[EventArgs.Empty]);
        Check(true,"빈 설문 목록 더블클릭 이벤트 예외 방지");
        PrepareRender(form);
        var field=typeof(SurveyAdministrationForm).GetField("questions",BindingFlags.Instance|BindingFlags.NonPublic)!;
        ((List<SurveyQuestionDraft>)field.GetValue(form)!).Add(new SurveyQuestionDraft {Prompt="추가한 문항이 목록에 표시됩니다",Options=["선택지 1","선택지 2"]});
        typeof(SurveyAdministrationForm).GetMethod("RefreshQuestionList",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(form,[0]);
        using(var bitmap=new Bitmap(form.Width,form.Height)){form.DrawToBitmap(bitmap,new Rectangle(Point.Empty,form.Size));bitmap.Save(Path.Combine(output,"survey-editor.png"));}
        var lists=Children(form).OfType<ListBox>().ToArray();
        Check(lists.Length==2 && lists.All(l=>l.Height>=60),"문항 및 저장된 설문 목록 가시 높이");
        Check(lists.Any(l=>l.Items.Count==1),"추가 문항 표시");
        using var question=new SurveyQuestionEditorForm(new SurveyQuestionDraft {Prompt="복수 응답과 함정문항",Options=["A","B","C"],AllowMultiple=true,MaxSelections=2,IsTrap=true,ExpectedAnswers=["B"]});
        PrepareRender(question);
        using(var bitmap=new Bitmap(question.Width,question.Height)){question.DrawToBitmap(bitmap,new Rectangle(Point.Empty,question.Size));bitmap.Save(Path.Combine(output,"question-editor.png"));}
        string resultPath=Path.Combine(output,"survey-results.json");
        if(File.Exists(resultPath))
        {
            using var results=new SurveyResultsForm("검증 설문",File.ReadAllText(resultPath),(_,_)=>Task.FromResult(File.ReadAllText(resultPath)));
            PrepareRender(results);
            var tabs=Children(results).OfType<TabControl>().Single();
            Check(tabs.TabPages.Count==5,"통계·그래프·서술·개별·보류 화면 구성");
            Check(!Children(results).OfType<SurveyPieChart>().Any()&&!Children(results).OfType<SurveyLineChart>().Any(),"개별 설문 그래프 구성 유지");
            tabs.SelectedIndex=1;
            using(var bitmap=new Bitmap(results.Width,results.Height)){results.DrawToBitmap(bitmap,new Rectangle(Point.Empty,results.Size));bitmap.Save(Path.Combine(output,"survey-chart.png"));}
            tabs.SelectedIndex=4;
            using(var bitmap=new Bitmap(results.Width,results.Height)){results.DrawToBitmap(bitmap,new Rectangle(Point.Empty,results.Size));bitmap.Save(Path.Combine(output,"survey-pending.png"));}
            using var overview=new SurveyOverviewForm(File.ReadAllText(Path.Combine(output,"survey-overview.json")),_=>Task.CompletedTask);
            PrepareRender(overview);
            using(var bitmap=new Bitmap(overview.Width,overview.Height)){overview.DrawToBitmap(bitmap,new Rectangle(Point.Empty,overview.Size));bitmap.Save(Path.Combine(output,"survey-overview.png"));}
            var overviewTabs=Children(overview).OfType<TabControl>().Single();
            Check(overviewTabs.TabPages.Count==4 && Children(overview).OfType<SurveyPieChart>().Count()==2 && Children(overview).OfType<SurveyLineChart>().Count()==1,"전체 통계에만 막대·파이·꺾은선 표시");
            for(int index=1;index<=2;index++)
            {
                overviewTabs.SelectedIndex=index;
                using var bitmap=new Bitmap(overview.Width,overview.Height);
                overview.DrawToBitmap(bitmap,new Rectangle(Point.Empty,overview.Size));
                bitmap.Save(Path.Combine(output,index==1?"survey-overview-pie.png":"survey-overview-line.png"));
            }
            var series=new[]{new SurveyTrendValue(new DateTime(2026,10,1),4,3,1),new SurveyTrendValue(new DateTime(2026,10,2),0,0,0),new SurveyTrendValue(new DateTime(2026,10,3),8,5,3)};
            using var line=new SurveyLineChart(series);using var lineBitmap=new Bitmap(line.Width,line.Height);
            line.DrawToBitmap(lineBitmap,new Rectangle(Point.Empty,line.Size));lineBitmap.Save(Path.Combine(output,"survey-line-gap-fixture.png"));
        }
    }
    private static void PrepareRender(Form form)
    {
        form.ShowInTaskbar=false; form.Opacity=0; form.StartPosition=FormStartPosition.Manual;
        form.Location=new Point(-32000,-32000); form.Show(); form.PerformLayout();
    }
}
