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
            Console.OutputEncoding=new System.Text.UTF8Encoding(false);
            if(args.Length==2&&args[0]=="--case-ui-checks")
            {
                string output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
                HttpRequestMessage MockCaseRequest(HttpMethod method,string endpoint,object? body)=>new(method,"http://127.0.0.1:1"+endpoint);
                using(var settings=new CaseInvestigationForm(MockCaseRequest))
                {
                    PrepareRender(settings);var amount=Children(settings).OfType<NumericUpDown>().Single();Check(amount.Minimum==15&&amount.Maximum==117,"사건 조사 수량 도우미 범위 15~117");
                    using var bitmap=new Bitmap(settings.Width,settings.Height);settings.DrawToBitmap(bitmap,new Rectangle(Point.Empty,settings.Size));bitmap.Save(Path.Combine(output,"case-investigation-settings.png"));
                    foreach(var button in Children(settings).OfType<Button>())Check(settings.ClientRectangle.Contains(settings.RectangleToClient(button.RectangleToScreen(button.ClientRectangle))),"조사 수량·구성 버튼 가시 영역 "+button.Text);
                }
                using(var catalogue=new CaseInvestigationCatalogueForm(MockCaseRequest))
                {
                    PrepareRender(catalogue);Children(catalogue).OfType<Button>().Single(b=>b.Text=="빈 117칸 생성").PerformClick();
                    var grid=Children(catalogue).OfType<DataGridView>().Single();Check(grid.Rows.Count==117&&grid.Columns.Count==8,"117조각 조사 구성 편집 가능");
                    Check(Convert.ToInt32(grid.Rows[116].Cells[1].Value)==13&&Convert.ToInt32(grid.Rows[116].Cells[2].Value)==9,"마지막 조각이 13파트 9조각");
                    using var bitmap=new Bitmap(catalogue.Width,catalogue.Height);catalogue.DrawToBitmap(bitmap,new Rectangle(Point.Empty,catalogue.Size));bitmap.Save(Path.Combine(output,"case-investigation-catalogue.png"));
                    Check(grid.Height>400,"조사 구성 목록 가시 영역과 스크롤 높이");
                    Check(grid.Columns[4].HeaderText=="단어 단서","조각 입력은 스토리 문단이 아닌 단어 단서");
                }
                using(var stories=new CasePartStoriesForm(MockCaseRequest))
                {
                    PrepareRender(stories);var parts=Children(stories).OfType<ListBox>().Single();var storyInput=Children(stories).OfType<TextBox>().Single(t=>t.Name=="PartStoryText");
                    Check(parts.Items.Count==13,"전체 스토리는 13파트별 별도 작성");
                    storyInput.Text="첫 문단입니다.\r\n\r\n두 번째 문단입니다.";parts.SelectedIndex=1;parts.SelectedIndex=0;
                    Check(storyInput.Text=="첫 문단입니다.\r\n\r\n두 번째 문단입니다.","파트 전환 중 스토리 초안과 줄바꿈 보존");
                    using var bitmap=new Bitmap(stories.Width,stories.Height);stories.DrawToBitmap(bitmap,new Rectangle(Point.Empty,stories.Size));bitmap.Save(Path.Combine(output,"case-part-stories.png"));
                }
                int publications=0;string sent="";
                string sentTag="";int sentCount=0;
                DateTimeOffset? sentStart=null,sentEnd=null;
                using(var notice=new AnnouncementEditorForm("",(content,tag,count,start,end)=>{publications++;sent=content;sentTag=tag;sentCount=count;sentStart=start;sentEnd=end;return Task.CompletedTask;}))
                {
                    PrepareRender(notice);var noticeInput=Children(notice).OfType<TextBox>().Single(t=>t.Name=="AnnouncementContent");noticeInput.Text="첫 줄\r\n두  칸 유지";
                    Children(notice).OfType<TextBox>().Single(t=>t.Name=="AnnouncementTag").Text="-긴급 공지-";Children(notice).OfType<NumericUpDown>().Single().Value=3;
                    Children(notice).OfType<CheckBox>().Single(t=>t.Name=="UseAnnouncementWindow").Checked=true;
                    Children(notice).OfType<DateTimePicker>().Single(t=>t.Name=="AnnouncementStart").Value=DateTimeOffset.UtcNow.ToOffset(TimeSpan.FromHours(9)).DateTime.AddMinutes(5);
                    Children(notice).OfType<DateTimePicker>().Single(t=>t.Name=="AnnouncementEnd").Value=DateTimeOffset.UtcNow.ToOffset(TimeSpan.FromHours(9)).DateTime.AddMinutes(15);
                    Check(publications==0,"공지 작성 창 열기와 입력만으로는 전송하지 않음");
                    Check(noticeInput.Multiline&&noticeInput.Height>=90,"공지 작성란은 여러 줄과 충분한 높이를 제공");
                    using var bitmap=new Bitmap(notice.Width,notice.Height);notice.DrawToBitmap(bitmap,new Rectangle(Point.Empty,notice.Size));bitmap.Save(Path.Combine(output,"announcement-editor.png"));
                    Children(notice).OfType<Button>().Single(b=>b.Text=="띄우기").PerformClick();
                    Check(publications==1&&sent=="첫 줄\r\n두  칸 유지","띄우기를 눌러야 한 번 전송하고 원문 보존");
                    Check(sentTag=="-긴급 공지-"&&sentCount==3,"작성한 태그와 선택 횟수를 함께 전송");
                    Check(sentStart>DateTimeOffset.UtcNow&&sentEnd>sentStart,"한국 시간 입력을 UTC 시작·종료 시각으로 전송");
                }
                using(var notice=new AnnouncementEditorForm("닫을 때는 미전송",(_,_,_,_,_)=>{publications++;return Task.CompletedTask;}))
                {PrepareRender(notice);Children(notice).OfType<Button>().Single(b=>b.Text=="닫기").PerformClick();Check(publications==1,"공지 작성 취소는 서버 상태를 변경하지 않음");}
                int removals=0;
                using(var notice=new AnnouncementEditorForm("제거 대상",(_,_,_,_,_)=>throw new Exception("제거로 전송하면 안 됨"),remove:()=>{removals++;return Task.CompletedTask;}))
                {PrepareRender(notice);Children(notice).OfType<Button>().Single(b=>b.Text=="제거").PerformClick();Check(removals==1,"작성 창의 제거 버튼은 공지 삭제만 호출");}
                Console.WriteLine($"PASS: {checks} case helper UI checks (offline, no publishing)");return 0;
            }
            if(args.Length==1&&args[0]=="--diagnostics-checks")
            {
                DiagnosticsChecks.RunAsync(Check).GetAwaiter().GetResult();
                Console.WriteLine($"PASS: {checks} lightweight diagnostics checks (isolated HTTP fixture; no production database/EOS)");return 0;
            }
            if(args.Length==1 && args[0]=="--encoding-fixture")
            {
                Console.OutputEncoding=new System.Text.UTF8Encoding(false);
                Console.WriteLine(LogReliabilityChecks.StandardText);Console.Error.WriteLine(LogReliabilityChecks.ErrorText);return 0;
            }
            if(args.Length==1 && args[0]=="--log-reliability-checks")
            {
                Console.OutputEncoding=new System.Text.UTF8Encoding(false);
                LogReliabilityChecks.RunAsync(Check).GetAwaiter().GetResult();
                Console.WriteLine($"PASS: {checks} log reliability checks (no database/native EOS execution)");return 0;
            }
            if(args.Length==1 && args[0]=="--packaging-checks")
            {
                PackagingReadinessChecks.Run(Check);
                Console.WriteLine($"PASS: {checks} packaging checks (no packaging/native SDK execution)");
                return 0;
            }
            if(args.Length==2 && args[0]=="--prepare-voice-runtime")
            {
                string backend=Path.Combine(Environment.CurrentDirectory,"Tools","ProjectProject01Backend");
                if(!File.Exists(Path.Combine(Environment.CurrentDirectory,"ProjectDFSD.uproject")) ||
                    !File.Exists(Path.Combine(backend,"ProjectProject01Backend.csproj"))) return 25;
                var bundle=global::EosRuntimeBundle.Prepare(backend,args[1],
                    Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_SDK_PATH"),
                    Environment.GetEnvironmentVariable("PROJECTPROJECT01_EOS_XAUDIO_PATH"));
                Console.WriteLine("Prepared server-only EOS runtime: "+Path.GetDirectoryName(bundle.Sdk));
                return 0;
            }
            if(args.Length==2&&args[0]=="--check-project-lock")
            {
                // Invoked only by an isolated review fixture, never by the helper UI.
                string root=Path.GetFullPath(args[1]);
                string reviewRoot=Path.GetFullPath(Path.Combine("Saved","ReviewRemediation","Launcher"))+Path.DirectorySeparatorChar;
                if(!root.StartsWith(reviewRoot,StringComparison.OrdinalIgnoreCase))return 24;
                try{using var lease=ProjectOperationLock.Acquire(root);return 0;}catch(IOException){return 23;}
            }
            if (args.Length > 0 && args[0] == "--integration")
            {
                IntegrationChecks.RunAsync(args[1],int.Parse(args[2]),args.Length>3?args[3]:null).GetAwaiter().GetResult();
                return 0;
            }
            var compatibility=VersionCompatibilityOptions.FromConfiguration(
                new Microsoft.Extensions.Configuration.ConfigurationBuilder().Build());
            Check(compatibility.NetworkProtocolVersion=="7","커스텀 경기 규칙·결과 통신 프로토콜 기본값 7");
            var versionHeaders=new Microsoft.AspNetCore.Http.HeaderDictionary
            {
                [VersionCompatibilityOptions.ClientBuildHeader]=compatibility.ClientBuildVersion,
                [VersionCompatibilityOptions.DedicatedServerBuildHeader]=compatibility.DedicatedServerBuildVersion,
                [VersionCompatibilityOptions.ApiVersionHeader]=compatibility.ApiVersion,
                [VersionCompatibilityOptions.GameDataVersionHeader]=compatibility.GameDataVersion,
                [VersionCompatibilityOptions.NetworkProtocolHeader]="7"
            };
            Check(compatibility.GetMismatches(versionHeaders,false).Count==0,"프로토콜 7 클라이언트 허용");
            Check(compatibility.GetMismatches(versionHeaders,true).Count==0,"프로토콜 7 데디 서버 허용");
            versionHeaders[VersionCompatibilityOptions.NetworkProtocolHeader]="6";
            Check(compatibility.GetMismatches(versionHeaders,false).SequenceEqual(new[]{"NetworkProtocol"}),"구형 클라이언트 프로토콜 혼용 차단");
            Check(compatibility.GetMismatches(versionHeaders,true).SequenceEqual(new[]{"NetworkProtocol"}),"구형 데디 서버 프로토콜 혼용 차단");
            string id=Guid.NewGuid().ToString("D");
            VoiceDirectoryChecks.Run(Check);
            NetworkBudgetChecks.RunAsync(Check).GetAwaiter().GetResult();
            VoiceSecurityChecks.Run(Check);
            LogReliabilityChecks.RunAsync(Check).GetAwaiter().GetResult();
            CrashRetentionChecks.RunAsync(Check).GetAwaiter().GetResult();
            CrashPolicyHttpChecks.RunAsync(Check).GetAwaiter().GetResult();
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
            var extreme=SurveyEndpoints.FillDailyGaps([new("0001-01-01",1,1,0),new("9999-12-31",1,1,0)]);
            Check(extreme.Count==3660&&extreme[^1].Date=="9999-12-31","극단 설문 날짜도 날짜 overflow 없이 기간 상한 적용");
            ReviewLauncherChecks.RunAsync(Check).GetAwaiter().GetResult();
            PackagingReadinessChecks.Run(Check);
            ResumeDownloadChecks.RunAsync(Path.GetFullPath("Saved/FeatureChecks/ResumeDownloads")).GetAwaiter().GetResult();
            UpdateInstallChecks.RunAsync(Path.GetFullPath("Saved/FeatureChecks/UpdateInstall")).GetAwaiter().GetResult();
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
    private sealed class ArticleImageFixture(byte[] bytes):HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)
        {
            Check(request.RequestUri!.AbsolutePath=="/api/launcher/images/11111111-1111-1111-1111-111111111111","사진은 지정된 서버의 공지 이미지 경로에서만 요청");
            return Task.FromResult(new HttpResponseMessage(System.Net.HttpStatusCode.OK){Content=new ByteArrayContent(bytes)});
        }
    }
    private static IEnumerable<Control> Children(Control control)
    {
        foreach(Control child in control.Controls){yield return child;foreach(var descendant in Children(child))yield return descendant;}
    }
    private static void CheckLayouts(string output)
    {
        Directory.CreateDirectory(output);
        HttpRequestMessage MockRequest(HttpMethod method,string endpoint,object? body)=>new(method,"http://127.0.0.1:1"+endpoint);
        using(var reports=new PlayerReportsForm(MockRequest))
        {
            PrepareRender(reports);
            Check(Children(reports).OfType<Button>().Count(b=>b.Text is "검토 완료" or "보류 종료" or "미확인으로 변경")==3,"운영 신고 목록에 검토 상태 변경 기능 표시");
            Check(Children(reports).OfType<TextBox>().Any(t=>!t.ReadOnly&&t.MaxLength==2000),"신고 검토 메모 입력 제공");
            foreach(var b in Children(reports).OfType<Button>())Check(reports.ClientRectangle.Contains(reports.RectangleToClient(b.RectangleToScreen(b.ClientRectangle))),"신고 창 버튼이 화면 범위에 표시: "+b.Text);
            Check(PlayerReportsForm.Reason("Griefing")=="게임 진행 방해","운영자에게 신고 사유 한글 표시");
            using var bitmap=new Bitmap(reports.Width,reports.Height);reports.DrawToBitmap(bitmap,new Rectangle(Point.Empty,reports.Size));bitmap.Save(Path.Combine(output,"player-reports.png"));
        }
        using(var helper=new MainForm())
        {
            PrepareRender(helper);
            foreach(string caption in new[]{"동시 경기 수 적용","방 한도 설정·적용","경기 종료 후 운영 중지","종료 대기 취소","게임 서버 운영 즉시 중지"})
            {
                var button=Children(helper).OfType<Button>().Single(b=>b.Text==caption);
                // Actions are intentionally grouped into tabs instead of exposing
                // every shutdown/deployment button at once. Select the owning tab
                // without clicking the operational action under test.
                for(Control? parent=button.Parent;parent is not null;parent=parent.Parent)
                    if(parent is TabPage page&&page.Parent is TabControl tabs){tabs.SelectedTab=page;break;}
                helper.PerformLayout();Application.DoEvents();
                Check(helper.ClientRectangle.Contains(helper.RectangleToClient(button.RectangleToScreen(button.ClientRectangle))),caption+" 버튼이 메인 창에 표시됨");
            }
            using var bitmap=new Bitmap(helper.Width,helper.Height);helper.DrawToBitmap(bitmap,new Rectangle(Point.Empty,helper.Size));bitmap.Save(Path.Combine(output,"helper-capacity-buttons.png"));
        }
        using(var launcher=new ProjectProject01ClientLauncher.LauncherForm(Path.Combine(output,"LauncherUiFixture")))
        {
            PrepareRender(launcher);
            launcher.FeatureSetHomePosts([
                new("notice","News","백화점 화재사건 안내","새로운 사건 기록을 확인하세요.",null,true),
                new("event","Event","조사 기록 수집 이벤트","지정된 장소에서 사건의 단서를 모아보세요.",null,true),
                new("hidden","News","대표 칸에 선택하지 않은 글","목록에서만 표시됩니다.",null,false),
                new("patch","Patch","최신 업데이트","전체 패치 내용 본문",null,false,"관전 카메라 조정\nUI 가독성 개선\n안정성 수정\n음성 상태 표시 개선\n업데이트 안정화\n서버 연결 안내 개선\n튜토리얼 목표 정리")
            ]);
            launcher.FeatureSetAnnouncement("첫 줄\n둘째 줄\n마지막 줄","-점검 공지-");Application.DoEvents();
            Check(launcher.FeatureAnnouncementVisible&&launcher.FeatureAnnouncementText.Replace("\r\n","\n")=="첫 줄\n둘째 줄\n마지막 줄","런처 상시 공지의 줄바꿈 보존");
            Check(launcher.FeatureAnnouncementTag=="-점검 공지-","런처 공지 본문 위에 작성한 태그 표시");
            var notice=Children(launcher).OfType<RichTextBox>().Single(t=>t.Text=="첫 줄\n둘째 줄\n마지막 줄"||t.Text=="첫 줄\r\n둘째 줄\r\n마지막 줄").Parent!;
            var main=Children(launcher).OfType<Button>().Single(b=>b.Text=="업데이트 확인").Parent!.Parent!;
            Check(notice.Width<=204&&notice.Height<=150&&notice.PointToScreen(Point.Empty).X>=main.PointToScreen(new Point(main.Width,0)).X,"상시 공지를 본문 오른쪽의 작은 칸으로 배치");
            Check(Children(launcher).OfType<RichTextBox>().All(t=>t.Parent==notice),"런처에 누적 상세 로그 창을 표시하지 않음");
            launcher.FeatureSetStatus("업데이트 정보를 확인하는 중...");
            launcher.FeatureSetStatus("다운로드 중\n45%");
            Check(Children(launcher).OfType<Label>().Any(t=>t.Text=="다운로드 중 45%"&&t.AutoEllipsis)&&!Children(launcher).OfType<Label>().Any(t=>t.Text=="업데이트 정보를 확인하는 중..."),"진행 상태는 최신 한 줄로 교체하고 줄바꿈을 정리");
            launcher.FeatureSetStatus("다운로드 중 (45%)");
            Children(launcher).OfType<ProgressBar>().Single().Value=45;
            var carousel=Children(launcher).OfType<ProjectProject01ClientLauncher.LauncherFeaturedCarousel>().Single();
            Check(carousel.PostCount==2&&carousel.CurrentId=="notice","서버에서 대표 칸으로 선택한 글만 순환 목록에 포함");
            Check(Children(launcher).OfType<Label>().Any(l=>l.Text.Contains("최신 업데이트")&&l.Text.Contains("관전 카메라 조정")),"최신 패치노트 요약을 별도로 표시");
            var patchText=Children(launcher).OfType<Label>().Single(l=>l.Text.Contains("최신 업데이트"));
            var patchBox=patchText.Parent!;int compactPatchHeight=patchBox.Height;
            Check(!patchText.Text.Contains("전체 패치 내용 본문")&&!patchText.Text.Contains("튜토리얼 목표 정리"),"직접 작성한 요약을 본문 대신 사용하고 공지 표시 중에는 짧게 표시");
            using(var noticeImage=new Bitmap(launcher.Width,launcher.Height)){launcher.DrawToBitmap(noticeImage,new Rectangle(Point.Empty,launcher.Size));noticeImage.Save(Path.Combine(output,"launcher-persistent-announcement.png"));}
            carousel.FeatureSetTransition(.5f);
            Check(carousel.SlideOffsets.Current<0&&carousel.SlideOffsets.Next>0,"기존 글은 왼쪽으로 나가고 다음 글은 오른쪽에서 들어오는 방향");
            using(var slide=new Bitmap(launcher.Width,launcher.Height)){launcher.DrawToBitmap(slide,new Rectangle(Point.Empty,launcher.Size));slide.Save(Path.Combine(output,"launcher-news-slide.png"));}
            carousel.FeatureSetTransition(0);
            carousel.FeatureAdvance(9999);Check(carousel.CurrentId=="notice","대표 게시글은 10초가 되기 전 전환하지 않음");
            carousel.FeatureAdvance(10550);Check(carousel.CurrentId=="event","10초 유지와 슬라이드 전환 후 다음 게시글 표시");
            carousel.FeatureAdvance(9999);Check(carousel.CurrentId=="event","전환이 끝난 새 게시글에도 온전한 10초 유지 시간 부여");
            carousel.FeatureAdvance(10550);Check(carousel.CurrentId=="notice","마지막 대표 게시글 다음에는 첫 게시글로 순환");
            launcher.FeatureSetAnnouncement("태그 없는 본문");Check(launcher.FeatureAnnouncementTag==""&&launcher.FeatureAnnouncementVisible,"태그 없이 전송하면 런처도 본문만 표시");
            launcher.FeatureSetAnnouncement(string.Empty);Check(!launcher.FeatureAnnouncementVisible,"빈 공지는 런처에서 영역까지 숨김");
            launcher.PerformLayout();Application.DoEvents();
            Check(patchBox.Height>compactPatchHeight&&patchText.Text.Contains("튜토리얼 목표 정리"),"안내 공지를 내리면 패치노트 칸이 길어지고 더 많은 요약 표시");
            using(var noNotice=new Bitmap(launcher.Width,launcher.Height)){launcher.DrawToBitmap(noNotice,new Rectangle(Point.Empty,launcher.Size));noNotice.Save(Path.Combine(output,"launcher-without-announcement.png"));}
            var pause=Children(launcher).OfType<Button>().Single(b=>b.Text=="다운로드 중단");
            Check(!pause.Enabled&&!pause.Visible,"런처 다운로드 중단은 작업 중에만 표시·유휴 중 숨김");
            using var bitmap=new Bitmap(launcher.Width,launcher.Height);launcher.DrawToBitmap(bitmap,new Rectangle(Point.Empty,launcher.Size));bitmap.Save(Path.Combine(output,"resumable-launcher.png"));
            var settings=Children(launcher).OfType<Button>().Single(b=>b.Text=="⚙ 환경설정");
            Check(!Children(launcher).OfType<TextBox>().Any(),"런처 메인에는 서버 입력·설치 관리 대신 환경설정만 표시");
            Exception? settingsError=null;
            using var closeSettings=new System.Windows.Forms.Timer {Interval=150};
            closeSettings.Tick+=(_,_)=>
            {
                var dialog=Application.OpenForms.Cast<Form>().FirstOrDefault(f=>f.Text=="런처 환경설정");if(dialog is null)return;closeSettings.Stop();
                try
                {
                    var tabs=Children(dialog).OfType<TabControl>().Single();
                    Check(tabs.TabPages.Count==2&&!tabs.TabPages.Cast<TabPage>().Any(p=>p.Text.Contains("공지")),"런처 환경설정은 서버·설치 / 고객센터만 표시");
                    foreach(TabPage page in tabs.TabPages)
                    {
                        tabs.SelectedTab=page;dialog.PerformLayout();
                        using var image=new Bitmap(dialog.Width,dialog.Height);dialog.DrawToBitmap(image,new Rectangle(Point.Empty,dialog.Size));image.Save(Path.Combine(output,"launcher-settings-"+tabs.SelectedIndex+".png"));
                    }
                    Check(Children(dialog).OfType<Button>().Any(b=>b.Text=="무결성 검사")&&Children(dialog).OfType<Button>().Any(b=>b.Text=="캐시 삭제"),"런처 검사·캐시 기능 환경설정에 포함");
                }catch(Exception ex){settingsError=ex;}finally{dialog.Close();}
            };
            closeSettings.Start();settings.PerformClick();if(settingsError is not null)throw settingsError;
            Exception? newsError=null;
            using var closeNews=new System.Windows.Forms.Timer {Interval=150};
            closeNews.Tick+=(_,_)=>
            {
                var dialog=Application.OpenForms.Cast<Form>().FirstOrDefault(f=>f.Text=="공지사항·패치노트");if(dialog is null)return;closeNews.Stop();
                try
                {
                    Check(Children(dialog).OfType<ListBox>().Any()&&Children(dialog).OfType<ProjectProject01ClientLauncher.LauncherPostView>().Any(),"공지 확인 버튼으로 읽기 전용 공지사항·패치노트 전용 창 열기");
                    Check(!Children(dialog).OfType<TextBox>().Any(),"공지 창에 서버·설치·문의 입력 없이 게시글 열람만 제공");
                    using var image=new Bitmap(dialog.Width,dialog.Height);dialog.DrawToBitmap(image,new Rectangle(Point.Empty,dialog.Size));image.Save(Path.Combine(output,"launcher-news-dialog.png"));
                }catch(Exception ex){newsError=ex;}finally{dialog.Close();}
            };
            closeNews.Start();Children(launcher).OfType<Button>().Single(b=>b.Text=="공지 확인").PerformClick();if(newsError is not null)throw newsError;
        }
        using(var community=new CommunityAdministrationForm(MockRequest))
        {
            PrepareRender(community);
            Check(Children(community).OfType<TextBox>().Any(t=>t.Name=="LauncherPostSummary"&&t.Multiline&&t.MaxLength==500),"운영자가 런처 요약을 별도 입력 가능");
            Check(Children(community).OfType<CheckBox>().Any(c=>c.Text=="런처 대표 칸에 표시"&&!c.Checked)&&Children(community).OfType<ComboBox>().Any(c=>c.Items.Contains("이벤트")),"운영자가 공지·이벤트와 대표 칸 표시 여부를 선택");
            Check(Children(community).OfType<TabControl>().Single().TabPages.Count==2,"운영 뉴스·패치노트 / 문의 답변 분리");
            using var image=new Bitmap(community.Width,community.Height);community.DrawToBitmap(image,new Rectangle(Point.Empty,community.Size));image.Save(Path.Combine(output,"community-administration.png"));
            Exception? tableError=null;
            using var tableTimer=new System.Windows.Forms.Timer {Interval=150};
            tableTimer.Tick+=(_,_)=>
            {
                var editor=Application.OpenForms.Cast<Form>().FirstOrDefault(f=>f.Text=="본문에 표 삽입");if(editor is null)return;tableTimer.Stop();
                try
                {
                    var table=Children(editor).OfType<DataGridView>().Single();
                    table.Rows[0].Cells[0].Value="변경 항목";table.Rows[0].Cells[1].Value="설명";table.Rows[1].Cells[0].Value="게임";table.Rows[1].Cells[1].Value="수정 내용";
                    Children(editor).OfType<Button>().Single(b=>b.Text=="본문에 삽입").PerformClick();
                }catch(Exception ex){tableError=ex;editor.Close();}
            };
            tableTimer.Start();Children(community).OfType<Button>().Single(b=>b.Text=="표 삽입").PerformClick();if(tableError is not null)throw tableError;
            var text=(TextBox)typeof(CommunityAdministrationForm).GetField("body",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(community)!;
            Check(text.Text.Contains("| 변경 항목 | 설명 |")&&text.Text.Contains("| 게임 | 수정 내용 |"),"운영자가 표 칸에 입력한 내용이 게시글 본문에 삽입됨");
            text.AppendText("\n![첫 사진](image:11111111-1111-1111-1111-111111111111)\n![두 번째](image:22222222-2222-2222-2222-222222222222)\n");
            var previewSelector=(ComboBox)typeof(CommunityAdministrationForm).GetField("previewImage",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(community)!;
            Check(previewSelector.Items.Count==3&&previewSelector.SelectedIndex==0&&previewSelector.SelectedItem!.GetType().GetProperty("Id")!.GetValue(previewSelector.SelectedItem) is null,"사진을 첨부해도 대표 미리보기 자동 선택하지 않음·하나만 선택 가능");
            text.Clear();Check(previewSelector.Items.Count==1&&previewSelector.SelectedIndex==0,"사진을 지우면 미리보기 후보에서도 제거");
        }
        using(var article=new ProjectProject01ClientLauncher.LauncherPostView {Width=600,Height=400})
        {
            article.ShowPostAsync("한국어 뉴스\n| 항목 | 내용 |\n| --- | --- |\n| 버전 | 1.0 |","http://127.0.0.1:1").GetAwaiter().GetResult();
            var table=Children(article).OfType<DataGridView>().Single();
            Check(table.ReadOnly&&!table.AllowUserToAddRows&&table.Rows.Count==1&&table.Columns.Count==2,"클라이언트 공지 표 읽기 전용 표시·편집 불가");
        }
        byte[] fixtureImage;
        using(var photo=new Bitmap(320,180))
        using(var drawing=Graphics.FromImage(photo))
        using(var bytes=new MemoryStream())
        {
            drawing.Clear(Color.SteelBlue);drawing.DrawString("Photo / 사진 미리보기",SystemFonts.DefaultFont,Brushes.White,new PointF(12,12));
            photo.Save(bytes,System.Drawing.Imaging.ImageFormat.Png);fixtureImage=bytes.ToArray();
        }
        using(var carousel=new ProjectProject01ClientLauncher.LauncherFeaturedCarousel(new ArticleImageFixture(fixtureImage)){Width=510,Height=250})
        {
            carousel.SetPosts([new("photo","News","사진이 있는 공지","선택한 대표 사진과 안내 내용", "11111111-1111-1111-1111-111111111111",true)],"http://127.0.0.1:1");
            Application.DoEvents();
            Check(carousel.LoadedImageCount==1,"대표 칸에서 서버 사진을 내려받아 비율을 유지하며 표시");
            using var image=new Bitmap(carousel.Width,carousel.Height);carousel.DrawToBitmap(image,new Rectangle(Point.Empty,carousel.Size));image.Save(Path.Combine(output,"launcher-featured-photo.png"));
            carousel.SetPosts([],"http://127.0.0.1:1");
            Check(carousel.PostCount==0&&carousel.LoadedImageCount==0,"대표 칸 해제·서버 변경 시 이전 사진과 게시글 정리");
        }
        using(var article=new ProjectProject01ClientLauncher.LauncherPostView(new ArticleImageFixture(fixtureImage)){Dock=DockStyle.Fill})
        using(var preview=new Form {Text="읽기 전용 뉴스 표시 검사",Width=760,Height=650})
        {
            preview.Controls.Add(article);
            article.ShowPostAsync("공지 사진과 표\n![공지 사진](image:11111111-1111-1111-1111-111111111111)\n| 항목 | 내용 |\n| --- | --- |\n| 버전 | 1.0 |","http://127.0.0.1:1").GetAwaiter().GetResult();
            PrepareRender(preview);
            Check(Children(article).OfType<PictureBox>().Single().Image?.Width==320,"클라이언트가 공지 사진을 실제 PNG로 해석·표시");
            using var image=new Bitmap(preview.Width,preview.Height);preview.DrawToBitmap(image,new Rectangle(Point.Empty,preview.Size));image.Save(Path.Combine(output,"launcher-news-photo-table.png"));
        }
        using(var administration=new ServerAdministrationForm(Path.GetFullPath(Path.Combine(output,"empty-fixture")),_=>{}))
        {
            PrepareRender(administration);
            using var bitmap=new Bitmap(administration.Width,administration.Height);administration.DrawToBitmap(bitmap,new Rectangle(Point.Empty,administration.Size));bitmap.Save(Path.Combine(output,"server-administration.png"));
            Check(Children(administration).OfType<Button>().Any(b=>b.Text=="최대 방 수")&&Children(administration).OfType<Button>().Any(b=>b.Text=="계정 이용 제한"),"서버 운영에서 방 한도·계정 제한 접근");
            Check(Children(administration).OfType<Button>().Any(b=>b.Text=="플레이어 신고 확인"),"서버 운영에서 플레이어 신고 확인 접근");
            foreach(var button in Children(administration).OfType<Button>().Where(b=>b.Text is "최대 방 수" or "계정 이용 제한" or "플레이어 신고 확인"))
                Check(administration.ClientRectangle.Contains(administration.RectangleToClient(button.RectangleToScreen(button.ClientRectangle))),button.Text+" 버튼이 창 밖으로 잘리지 않음");
        }
        using(var capacity=new RoomCapacityForm(MockRequest,_=>{}))
        {
            PrepareRender(capacity);
            using var bitmap=new Bitmap(capacity.Width,capacity.Height);capacity.DrawToBitmap(bitmap,new Rectangle(Point.Empty,capacity.Size));bitmap.Save(Path.Combine(output,"room-capacity.png"));
            Check(Children(capacity).OfType<NumericUpDown>().Single().Maximum==100,"방 한도 설정 UI 8개 제한 제거");
        }
        using(var policy=new CrashArtifactPolicyForm(MockRequest))
        {
            PrepareRender(policy);
            using var bitmap=new Bitmap(policy.Width,policy.Height);policy.DrawToBitmap(bitmap,new Rectangle(Point.Empty,policy.Size));bitmap.Save(Path.Combine(output,"crash-retention-policy.png"));
            Check(Children(policy).OfType<CheckBox>().Count()==2,"크래시 자동 정리·덤프 수집 별도 체크박스");
            var apply=Children(policy).OfType<Button>().Single();
            Check(policy.ClientRectangle.Contains(policy.RectangleToClient(apply.RectangleToScreen(apply.ClientRectangle))),"크래시 정책 적용 버튼 가시 영역");
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
