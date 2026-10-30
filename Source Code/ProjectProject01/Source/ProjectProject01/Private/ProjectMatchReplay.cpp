#include "ProjectMatchReplay.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectLoadingScreenSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ReplaySubsystem.h"
#include "NetworkReplayStreaming.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
    constexpr TCHAR ReplayFactory[] = TEXT("LocalFileNetworkReplayStreaming");
    FString ReplayPrefix() { return TEXT("DFSD_P") + FProjectProject01VersionContract::GetNetworkProtocolVersion() + TEXT("_"); }
    TSharedRef<STextBlock> ReplayText(const FString& Text)
    {
        return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true);
    }
}

bool UProjectMatchReplay::IsOwnedName(const FString& Name)
{
    if (!Name.StartsWith(ReplayPrefix()) || Name.Len() != ReplayPrefix().Len() + 32) return false;
    FGuid Id;
    return FGuid::ParseExact(Name.Mid(ReplayPrefix().Len()), EGuidFormats::Digits, Id) && Id.IsValid();
}
TArray<FString> UProjectMatchReplay::StreamOptions(bool RecordMapChanges)const
{
    TArray<FString> Options{TEXT("ReplayStreamerOverride=LocalFileNetworkReplayStreaming")};FString Directory;
    if(Streamer&&Streamer->GetDemoPath(Directory)==EStreamingOperationResult::Success&&!Directory.IsEmpty())Options.Add(TEXT("ReplayStreamerDemoPath=")+Directory);
    if(RecordMapChanges)Options.Add(TEXT("RecordMapChanges"));return Options;
}
bool UProjectMatchReplay::ShouldCreateSubsystem(UObject* Outer) const
{
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}
void UProjectMatchReplay::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UProjectProject01AuthSubsystem>();
    GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>()->OnLocalSessionCleared.AddUObject(this,&ThisClass::SessionCleared);
    bAllowPersistence = !FApp::IsUnattended();
#if WITH_EDITOR
    bAllowPersistence = bAllowPersistence && !GIsEditor;
#endif
    if (bAllowPersistence) GConfig->GetBool(TEXT("ProjectDFSD.Replays"), TEXT("RecordMatches"), bRecordingEnabled, GGameUserSettingsIni);
    Streamer = FNetworkReplayStreaming::Get().GetFactory(ReplayFactory).CreateReplayStreamer();
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), .5f);
}
void UProjectMatchReplay::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if(auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>())Auth->OnLocalSessionCleared.RemoveAll(this);
    StopRecording(); Streamer.Reset(); OnChanged.Clear();
    Super::Deinitialize();
}
void UProjectMatchReplay::SessionCleared(){bPlaybackPending=false;StopRecording();}
void UProjectMatchReplay::SetRecordingEnabled(bool Enabled)
{
    bRecordingEnabled = Enabled;
    if(Enabled){BlockedMatch.Reset();bCatalogueReady=false;}
    if (!Enabled) StopRecording();
    if (bAllowPersistence)
    {
        GConfig->SetBool(TEXT("ProjectDFSD.Replays"), TEXT("RecordMatches"), Enabled, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
    OnChanged.Broadcast();
}
void UProjectMatchReplay::StopRecording()
{
    // Never stop a manual developer replay, another PIE instance or playback.
    auto* World = GetWorld(); auto* Driver = RecordingDriver.Get();
    if (World && Driver && World->GetDemoNetDriver() == Driver && Driver->IsRecording())
        if (auto* Replays = GetGameInstance()->GetSubsystem<UReplaySubsystem>()) Replays->StopReplay();
    RecordingDriver.Reset(); RecordingName.Reset(); RecordingMatch.Reset();
    bCatalogueReady=false;
}
bool UProjectMatchReplay::Tick(float)
{
    auto* World = GetWorld(); if (!World) return true;
    if (bPlaybackPending)
    {
        if (World->IsPlayingReplay() && Cast<AProjectReplayController>(GetGameInstance()->GetFirstLocalPlayerController()))
        { bPlaybackPending = false; OnChanged.Broadcast(); }
        // Engine trap with replays shorter than its 5 s read-ahead: the first tick buffers the whole file, processes no
        // frame when that tick is shorter than the first recorded frame, and from then on "no data on frame 0" waits
        // forever. Moving to 0.5 s once makes the driver process the buffered frames.
        else if (auto* Driver = World->IsPlayingReplay() ? World->GetDemoNetDriver() : nullptr;
            Driver && !bPlaybackNudged && Driver->GetDemoFrameNum() == 0 && Driver->GetDemoCurrentTime() > 0 &&
            Driver->GetReplayStreamer().IsValid() && !Driver->GetReplayStreamer()->IsDataAvailable() &&
            FPlatformTime::Seconds() - PlaybackStartedAt > 1.0)
        { bPlaybackNudged = true; Driver->GotoTimeInSeconds(FMath::Min(.5f, Driver->GetDemoTotalTime())); }
        else if (FPlatformTime::Seconds() - PlaybackStartedAt > 30)
        { Status = TEXT("리플레이를 불러오지 못했습니다. 파일과 게임 버전을 확인하세요."); ExitPlayback(); }
        return true;
    }
    if (World->IsPlayingReplay()) return true;
    auto* PC = Cast<AMultiplayTestPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    FGuid Match;
    const bool Eligible = bRecordingEnabled && PC && !PC->IsServerControlledBot() &&
        FGuid::Parse(PC->GetAuthenticatedMatchId(), Match) && Match.IsValid() &&
        PC->GetMatchPhase() == EMultiplayTestMatchPhase::InProgress;
    if (!RecordingName.IsEmpty())
    {
        const bool SameMatch = PC && PC->GetAuthenticatedMatchId() == RecordingMatch;
        const bool Ending = PC && (PC->GetMatchPhase() == EMultiplayTestMatchPhase::Ending || PC->GetMatchPhase() == EMultiplayTestMatchPhase::Results);
        if (!bRecordingEnabled || Ending || (PC && !SameMatch) || (!PC && World->GetNetMode() == NM_Standalone)) StopRecording();
        else if (!RecordingDriver.IsValid() || World->GetDemoNetDriver() != RecordingDriver.Get())
        { BlockedMatch=RecordingMatch;RecordingDriver.Reset();RecordingName.Reset();RecordingMatch.Reset();Status=TEXT("녹화가 중단되었습니다. 다음 경기 또는 녹화 설정을 다시 켠 뒤 재시도합니다.");OnChanged.Broadcast();return true; }
        else
        {
            if(auto* Driver=RecordingDriver.Get();Driver&&Driver->IsRecordingPaused()&&Eligible)
                GetGameInstance()->StartRecordingReplay(RecordingName,FString(),StreamOptions(true));
            FString Directory;
            if(auto* Driver=RecordingDriver.Get();Driver&&Driver->GetReplayStreamer()->GetDemoPath(Directory)==EStreamingOperationResult::Success)
            {
                const int64 Bytes=IFileManager::Get().FileSize(*(FPaths::Combine(Directory,RecordingName)+FNetworkReplayStreaming::GetReplayFileExtension()));
                if(Bytes>FMath::Min<int64>(512LL*1024*1024,2LL*1024*1024*1024-ListedBytes)||Driver->GetDemoCurrentTime()>3600)
                {BlockedMatch=RecordingMatch;StopRecording();Status=TEXT("녹화 용량 또는 60분 제한에 도달해 녹화를 마쳤습니다.");OnChanged.Broadcast();}
            }
            return true;
        }
    }
    if (!Eligible || World->GetDemoNetDriver()) return true;
    // PIE keeps recordings in memory-only application state disabled: testing does not fill a developer's disk.
    if (!bAllowPersistence) return true;
    if(BlockedMatch==PC->GetAuthenticatedMatchId()||IsBusy())return true;
    if(!bCatalogueReady){RefreshList();return true;}
    if(Entries.Num()>=10||ListedBytes>=2LL*1024*1024*1024)
    {BlockedMatch=PC->GetAuthenticatedMatchId();Status=TEXT("리플레이 보관 한도(10개 / 2GB)에 도달했습니다. 기존 파일을 삭제하면 새 경기를 녹화할 수 있습니다.");OnChanged.Broadcast();return true;}
    RecordingName = ReplayPrefix() + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    RecordingMatch = PC->GetAuthenticatedMatchId();
    const FString Friendly = FString::Printf(TEXT("%s · %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")),PC->GetAuthenticatedRole()==TEXT("Survivor")?TEXT("생존자"):PC->GetAuthenticatedRole()==TEXT("Observer")?TEXT("관전자"):TEXT("마네킹"));
    GetGameInstance()->StartRecordingReplay(RecordingName, Friendly, StreamOptions(true));
    RecordingDriver = World->GetDemoNetDriver();
    if(auto* Driver=RecordingDriver.Get())Driver->SetMaxDesiredRecordTimeMS(2.f);
    if (!RecordingDriver.IsValid()) { BlockedMatch=RecordingMatch;RecordingName.Reset();RecordingMatch.Reset();Status=TEXT("경기 녹화를 시작하지 못했습니다. 저장 공간을 확인하세요.");OnChanged.Broadcast(); }
    return true;
}
void UProjectMatchReplay::RefreshList()
{
    if (!Streamer || IsBusy()) return;
    bListing = true; OnChanged.Broadcast();
    Streamer->EnumerateStreams(FNetworkVersion::GetReplayVersion(), 0, FString(), {},
        FEnumerateStreamsCallback::CreateWeakLambda(this, [this](const FEnumerateStreamsResult& Result)
    {
        if(!Streamer){bListing=false;return;}
        bListing = false; Entries.Reset();ListedBytes=0;
        if (!Result.WasSuccessful())
        {Status=TEXT("저장된 리플레이 목록을 읽지 못했습니다.");if(const auto* PC=Cast<AMultiplayTestPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))BlockedMatch=PC->GetAuthenticatedMatchId();}
        else
        {
            for (const auto& Item : Result.FoundStreams)
                if (IsOwnedName(Item.Name) && Item.Name!=RecordingName)
                    Entries.Add({Item.Name, Item.bIsLive?TEXT("중단되었거나 다른 창에서 녹화 중인 경기"):Item.FriendlyName, Item.Timestamp,Item.bIsLive?0:Item.LengthInMS, Item.SizeInBytes});
            FString Directory;
            if(Streamer->GetDemoPath(Directory)==EStreamingOperationResult::Success)
                for(const auto& Name:Result.CorruptedStreams)if(IsOwnedName(Name)&&!Entries.ContainsByPredicate([&](const auto& E){return E.Name==Name;}))
                {
                    const FString Path=FPaths::Combine(Directory,Name)+FNetworkReplayStreaming::GetReplayFileExtension();
                    Entries.Add({Name,TEXT("손상되거나 완료되지 않은 녹화 · 삭제 가능"),IFileManager::Get().GetTimeStamp(*Path),0,FMath::Max<int64>(0,IFileManager::Get().FileSize(*Path))});
                }
            Entries.Sort([](const auto& A, const auto& B) { return A.RecordedAt > B.RecordedAt; });
            for(const auto& E:Entries)ListedBytes+=FMath::Max<int64>(0,E.Bytes);
            bCatalogueReady=true;
            Status = Entries.IsEmpty() ? TEXT("이 게임 버전으로 저장한 경기가 없습니다.") : TEXT("리플레이에는 이 클라이언트가 받은 경기 데이터만 포함됩니다. 음성 대화는 녹음하지 않습니다.");
        }
        OnChanged.Broadcast();
    }));
}
bool UProjectMatchReplay::Play(const FString& Name)
{
    auto* World = GetWorld(); auto* PC = GetGameInstance()->GetFirstLocalPlayerController();
    if (!World || !PC || World->GetNetMode() != NM_Standalone || Cast<AMultiplayTestPlayerController>(PC) || IsBusy() ||
        !IsOwnedName(Name) || !Entries.ContainsByPredicate([&](const auto& E) { return E.Name == Name && E.DurationMs>0; }))
    { Status = TEXT("경기에서 나온 뒤 로비에서 저장된 리플레이를 선택하세요."); OnChanged.Broadcast(); return false; }
    if (const auto* Lobby = GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>(); Lobby && Lobby->HasCurrentRoom())
    { Status = TEXT("방에서 나온 뒤 리플레이를 재생하세요."); OnChanged.Broadcast(); return false; }
    StopRecording(); bPlaybackPending = true; bPlaybackNudged = false; PlaybackStartedAt = FPlatformTime::Seconds(); PlaybackDepartureWorld = World;
    auto* Replays = GetGameInstance()->GetSubsystem<UReplaySubsystem>();
    if (!Replays || !Replays->PlayReplay(Name, nullptr, StreamOptions()))
    { bPlaybackPending = false; Status = TEXT("리플레이 재생을 시작하지 못했습니다."); OnChanged.Broadcast(); return false; }
    return true;
}
void UProjectMatchReplay::Delete(const FString& Name)
{
    if (!bAllowPersistence || !Streamer || IsBusy() || !IsOwnedName(Name) || !Entries.ContainsByPredicate([&](const auto& E) { return E.Name == Name; })) return;
    bDeleting = true; OnChanged.Broadcast();
    Streamer->DeleteFinishedStream(Name, FDeleteFinishedStreamCallback::CreateWeakLambda(this, [this](const FDeleteFinishedStreamResult& Result)
    {
        bDeleting = false; Status = Result.WasSuccessful() ? TEXT("리플레이를 삭제했습니다.") : TEXT("사용 중이거나 삭제할 수 없는 파일입니다.");
        if (Result.WasSuccessful()){BlockedMatch.Reset();RefreshList();}else OnChanged.Broadcast();
    }));
}
void UProjectMatchReplay::ExitPlayback()
{
    bPlaybackPending = false;
    if (auto* Replays = GetGameInstance()->GetSubsystem<UReplaySubsystem>())
    { TGuardValue<bool> KeepWorld(Replays->bLoadDefaultMapOnStop, false); Replays->StopReplay(); }
    if (auto* Loading = GetGameInstance()->GetSubsystem<UProjectLoadingScreenSubsystem>()) Loading->CancelLoading();
    const auto* Auth = GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();
    UGameplayStatics::OpenLevel(this, FName(Auth && Auth->IsSignedIn() ? TEXT("/Game/MyProject/Level/LobbyLevel") : TEXT("/Game/MyProject/Level/LoginLevel")), true);
}

AProjectReplayController::AProjectReplayController() { bShowMouseCursor = true; PrimaryActorTick.bTickEvenWhenPaused = true; }
void AProjectReplayController::BeginPlay()
{
    Super::BeginPlay();
    EnsureReplayUI();
}
void AProjectReplayController::ReceivedPlayer(){Super::ReceivedPlayer();EnsureReplayUI();}
void AProjectReplayController::EnsureReplayUI()
{
    if (Controls || !IsLocalController() || !GetWorld()->IsPlayingReplay() || !GetGameInstance()->GetGameViewportClient()) return;
    ChangeState(NAME_Spectating); SetInputMode(FInputModeGameAndUI());
    if(auto* Settings=UProjectProject01GameUserSettings::Get())Settings->RestoreGameplaySFX();
    if (auto* Loading = GetGameInstance()->GetSubsystem<UProjectLoadingScreenSubsystem>()) Loading->CancelLoading();
    if (auto* GI = GetGameInstance<UProjectProject01GameInstance>()) GI->ClearEntireScreenFade();
    auto Button = [this](const FString& Text, TFunction<void()> Action)
    { return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).OnClicked_Lambda([Action = MoveTemp(Action)] { Action(); return FReply::Handled(); })[ReplayText(Text)]; };
    Controls = SNew(SVerticalBox)
        +SVerticalBox::Slot().FillHeight(1)[SNew(SBorder).Visibility(EVisibility::HitTestInvisible).BorderBackgroundColor(FLinearColor::Transparent)]
        +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(16)
        [SNew(SVerticalBox)
          +SVerticalBox::Slot().AutoHeight()[ReplayText(TEXT("경기 리플레이 · 오른쪽 마우스를 누른 채 시점 회전 · WASD 이동"))]
          +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SSlider).IsEnabled_Lambda([this] { return !bSeeking; }).Value_Lambda([this]
            { const auto* D=GetWorld()->GetDemoNetDriver(); return bScrubbing?ScrubTarget:D && D->GetDemoTotalTime()>0 ? D->GetDemoCurrentTime()/D->GetDemoTotalTime() : 0.f; })
            .OnMouseCaptureBegin_Lambda([this]{bScrubbing=true;if(auto* D=GetWorld()->GetDemoNetDriver())ScrubTarget=D->GetDemoTotalTime()>0?D->GetDemoCurrentTime()/D->GetDemoTotalTime():0;})
            .OnMouseCaptureEnd_Lambda([this]{bScrubbing=false;if(auto* D=GetWorld()->GetDemoNetDriver())Seek(ScrubTarget*D->GetDemoTotalTime());})
            .OnValueChanged_Lambda([this](float V) { ScrubTarget=V; })]
          +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("재생 / 일시정지"), [this] { bPaused=!bPaused; })]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("-10초"), [this] { if(auto* D=GetWorld()->GetDemoNetDriver())Seek(D->GetDemoCurrentTime()-10); })]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("+10초"), [this] { if(auto* D=GetWorld()->GetDemoNetDriver())Seek(D->GetDemoCurrentTime()+10); })]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("속도 변경"), [this] { PlaybackSpeed=PlaybackSpeed<2?PlaybackSpeed*2:.5f; })]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).ColorAndOpacity(ProjectDFSDUI::Text()).Text_Lambda([this]
              { auto* D=GetWorld()->GetDemoNetDriver();return FText::FromString(FString::Printf(TEXT("%.0f / %.0f초 · %.1f배%s"),D?D->GetDemoCurrentTime():0,D?D->GetDemoTotalTime():0,PlaybackSpeed,bSeeking?TEXT(" · 이동 중"):TEXT(""))); })]
            +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("로비로 돌아가기"), [this] { if(auto* R=GetGameInstance()->GetSubsystem<UProjectMatchReplay>())R->ExitPlayback(); })]]]];
    if (auto* V = GetGameInstance()->GetGameViewportClient()) V->AddViewportWidgetContent(Controls.ToSharedRef(), 1000002);
}
void AProjectReplayController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Controls) if(auto* V=GetGameInstance()?GetGameInstance()->GetGameViewportClient():nullptr)V->RemoveViewportWidgetContent(Controls.ToSharedRef());
    Controls.Reset(); Super::EndPlay(Reason);
}
void AProjectReplayController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    if (!GetWorld()->IsPlayingReplay()) return;
    EnsureReplayUI();
    if (bSeeking && FPlatformTime::Seconds()-SeekStartedAt>10) { bSeeking=false; bPaused=true; }
    GetWorld()->GetWorldSettings()->DemoPlayTimeDilation=bPaused&&!bSeeking?0.f:PlaybackSpeed;
}
bool AProjectReplayController::InputKey(const FInputKeyEventArgs& Params)
{
    if (Params.Key==EKeys::RightMouseButton)
    {
        if (Params.Event==IE_Pressed) { bShowMouseCursor=false;SetInputMode(FInputModeGameOnly()); }
        else if(Params.Event==IE_Released) { bShowMouseCursor=true;SetInputMode(FInputModeGameAndUI()); }
    }
    return Super::InputKey(Params);
}
void AProjectReplayController::Seek(float Seconds)
{
    auto* D=GetWorld()->GetDemoNetDriver();if(!D||bSeeking||!FMath::IsFinite(Seconds))return;
    bSeeking=true;SeekStartedAt=FPlatformTime::Seconds();
    D->GotoTimeInSeconds(FMath::Clamp(Seconds,0.f,FMath::Max(0.f,D->GetDemoTotalTime()-.01f)),FOnGotoTimeDelegate::CreateWeakLambda(this,[this](bool){bSeeking=false;}));
}
