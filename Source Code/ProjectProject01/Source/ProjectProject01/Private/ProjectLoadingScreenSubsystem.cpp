#include "ProjectLoadingScreenSubsystem.h"
#include "ProjectLoadingScreenState.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "MultiplayTestPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LevelStreaming.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#if !UE_SERVER
#include "MoviePlayer.h"
#endif

namespace ProjectLoading
{
	using FStatePtr=TSharedPtr<FProjectLoadingScreenState,ESPMode::ThreadSafe>;
	bool IsFrontEnd(const FString& Map)
	{return Map.Contains(TEXT("TitleLevel")) || Map.Contains(TEXT("LoginLevel")) || Map.Contains(TEXT("LobbyLevel")) ||
		Map.Contains(TEXT("LeaderBoardTest")) || Map.StartsWith(TEXT("/Engine/")) || Map.IsEmpty();}

	class SScreen final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScreen){} SLATE_ARGUMENT(FStatePtr,State) SLATE_EVENT(FSimpleDelegate,OnCancel) SLATE_END_ARGS()
		void Construct(const FArguments& Args)
		{
			State=Args._State;
			BarStyle=FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
			BarStyle.SetFillImage(*FCoreStyle::Get().GetBrush("WhiteBrush"));
			auto Text=[this](TFunction<FString()> Value,int32 Size,bool Small=false)
			{return SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(FLinearColor(.88f,.87f,.83f))
				.Text_Lambda([Value](){return FText::FromString(Value());}).AutoWrapText(false)
				.WrapTextAt_Lambda([this,Small](){return Small?PeerWidth():OwnWidth();});};
			TSharedRef<SVerticalBox> Own=SNew(SVerticalBox);
			Own->AddSlot().AutoHeight().Padding(0,0,0,12)[Text([](){return FString(TEXT("게임을 불러오는 중"));},24)];
			Own->AddSlot().AutoHeight().Padding(0,0,0,8)[Text([this](){const auto S=State->Read();const FString Role=S.OwnRole.IsEmpty()?FString():TEXT(" · ")+S.OwnRole;return FString::Printf(TEXT("%s (나)%s — %d%%"),*S.OwnName,*Role,S.Progress);},22)];
			Own->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(24)[SNew(SProgressBar).Style(&BarStyle).Percent_Lambda([this](){return TOptional<float>(State->Read().Progress/100.f);}).FillColorAndOpacity(FLinearColor(.66f,.25f,.12f))]];
			Own->AddSlot().AutoHeight().Padding(0,10,0,0)[Text([this](){const auto S=State->Read();return FString::Printf(TEXT("%s · %d초 경과"),*S.Stage,FMath::Max(0,int32(FPlatformTime::Seconds()-S.Started)));},16)];
			Own->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0,8,0,0)
			[SNew(SButton).Visibility_Lambda([this](){return State->Read().CanCancel?EVisibility::Visible:EVisibility::Collapsed;})
			.OnClicked_Lambda([Cancel=Args._OnCancel](){Cancel.ExecuteIfBound();return FReply::Handled();})
			[Text([](){return FString(TEXT("세션 대기 취소"));},15)]];
			TSharedRef<SVerticalBox> Peers=SNew(SVerticalBox);
			for(int32 Index=0;Index<2;++Index)
			{
				auto Other=[this,Index]()->TOptional<FProjectLoadingParticipant>
				{auto S=State->Read();S.Participants.RemoveAll([&S](const auto& P){return P.UserId==S.OwnId || (S.OwnId.IsEmpty() && P.DisplayName==S.OwnName);});return S.Participants.IsValidIndex(Index)?TOptional<FProjectLoadingParticipant>(S.Participants[Index]):TOptional<FProjectLoadingParticipant>();};
				TSharedRef<SVerticalBox> Row=SNew(SVerticalBox).Visibility_Lambda([Other](){return Other().IsSet()?EVisibility::Visible:EVisibility::Collapsed;});
				Row->AddSlot().AutoHeight()[Text([Other](){const auto P=Other();return P.IsSet()?FString::Printf(TEXT("%s — %d%%"),*P->DisplayName,P->Progress):FString();},15,true)];
				Row->AddSlot().AutoHeight().Padding(0,5,0,5)[SNew(SBox).HeightOverride(12)[SNew(SProgressBar).Style(&BarStyle).Percent_Lambda([Other](){const auto P=Other();return TOptional<float>(P.IsSet()?P->Progress/100.f:0.f);}).FillColorAndOpacity(FLinearColor(.45f,.46f,.48f))]];
				Row->AddSlot().AutoHeight().Padding(0,0,0,16)[Text([Other](){const auto P=Other();return P.IsSet()?FString(P->Progress==100?TEXT("준비 완료"):P->Progress>=70?TEXT("화면 준비 중"):TEXT("접속·맵 로딩 확인 대기")):FString();},12,true)];
				Peers->AddSlot().AutoHeight()[Row];
			}
			TSharedRef<SOverlay> Layout=SNew(SOverlay)
				+SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(24,0,0,0)
				[SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(PeerWidth());})[Peers]]
				+SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(MessageWidth());})
				[SNew(STextBlock).Text(FText::FromString(TEXT("모든 참가자가 준비되면 시작됩니다.")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular",23)).ColorAndOpacity(FLinearColor(.88f,.87f,.83f))
				.AutoWrapText(false).WrapTextAt_Lambda([this](){return MessageWidth();}).Justification(ETextJustify::Center)]]
				+SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(24,0,24,32)
				[SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(OwnWidth());})[Own]];
			ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black)
				.Padding(0)[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
				.ColorAndOpacity_Lambda([this](){return FLinearColor(1,1,1,State->ContentOpacity(FPlatformTime::Seconds()));})[Layout]]];
		}
	private:
		float ScreenWidth() const {const float Width=float(GetCachedGeometry().GetLocalSize().X);return Width>0?Width:1280.f;}
		float PeerWidth() const {return FMath::Min(260.f,ScreenWidth()*.23f);}
		float MessageWidth() const {return FMath::Min(600.f,ScreenWidth()*.42f);}
		float OwnWidth() const {return FMath::Min(900.f,FMath::Max(200.f,ScreenWidth()-48.f));}
		TSharedPtr<FProjectLoadingScreenState,ESPMode::ThreadSafe> State;
		FProgressBarStyle BarStyle;
	};
}

TSharedRef<SWidget> MakeProjectLoadingScreenWidget(TSharedPtr<FProjectLoadingScreenState,ESPMode::ThreadSafe> State,FSimpleDelegate OnCancel)
{return SNew(ProjectLoading::SScreen).State(State).OnCancel(OnCancel);}

bool UProjectLoadingScreenSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer() && !IsRunningCommandlet();}
void UProjectLoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);State=MakeShared<FProjectLoadingScreenState,ESPMode::ThreadSafe>();
	PreLoadHandle=FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this,&ThisClass::PreLoadMap);
	PostLoadHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this,&ThisClass::PostLoadMap);
	Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::TickLoading),.05f);
}
void UProjectLoadingScreenSubsystem::Deinitialize()
{
	CancelLoading();FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadHandle);FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	Super::Deinitialize();
}
bool UProjectLoadingScreenSubsystem::IsLoading() const {return State && State->Read().Active;}
void UProjectLoadingScreenSubsystem::BeginLoading(bool Multiplayer)
{
	if(!State || IsRunningDedicatedServer() || (GetWorld() && GetWorld()->GetNetMode()==NM_DedicatedServer)) return;
	if(!IsLoading())
	{
		State->Begin(Multiplayer,FPlatformTime::Seconds());DepartureWorld=GetWorld();bAllowCurrentWorld=false;EpochId.Reset();
		WorldLoadingStartedAt=FPlatformTime::Seconds();bAwaitingServer=false;PreparationProgress=0;
		if(auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>())
		{FScopeLock Lock(&State->Mutex);if(!Auth->GetSignedInDisplayName().IsEmpty())State->Data.OwnName=Auth->GetSignedInDisplayName();}
		if(Multiplayer)if(auto* Lobby=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();Lobby && Lobby->HasCurrentRoom())
		{FScopeLock Lock(&State->Mutex);for(const auto& P:Lobby->GetCurrentRoom().Members){FProjectLoadingParticipant Row;Row.UserId=P.UserId;Row.DisplayName=P.DisplayName;State->Data.Participants.Add(Row);}}
	}
	AttachOverlay();
}
void UProjectLoadingScreenSubsystem::ShowSessionPreparation(const FString& Stage,uint8 Progress,bool CanCancel,FSimpleDelegate OnCancel)
{
	const bool WasLoading=IsLoading();BeginLoading(true);if(!IsLoading())return;
	if(WasLoading && !bAwaitingServer)return; // A stale lobby poll must not restart an admitted connection.
	if(!bAwaitingServer || Progress>PreparationProgress)PhaseStartedAt=FPlatformTime::Seconds();
	if(bAwaitingServer && Progress<PreparationProgress)return;
	bAwaitingServer=true;PreparationProgress=Progress;PreparationCancelAction=OnCancel;
	State->Advance(FMath::Min<uint8>(Progress,15),Stage);
	{FScopeLock Lock(&State->Mutex);State->Data.CanCancel=CanCancel && PreparationCancelAction.IsBound();}
	if(auto* Lobby=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();Lobby && Lobby->HasCurrentRoom())
	{
		const FString Role=Lobby->GetCurrentRoom().AssignedRole;
		if(Role==TEXT("Mannequin") || Role==TEXT("Survivor"))
		{FScopeLock Lock(&State->Mutex);State->Data.OwnRole=Role==TEXT("Mannequin")?TEXT("마네킹"):TEXT("생존자");}
	}
}
void UProjectLoadingScreenSubsystem::RequestCancelPreparation()
{
	if(!bAwaitingServer || !State || !State->Read().CanCancel)return;
	{FScopeLock Lock(&State->Mutex);State->Data.CanCancel=false;State->Data.Stage=TEXT("세션 대기를 취소하는 중…");}
	PreparationCancelAction.ExecuteIfBound();
}
void UProjectLoadingScreenSubsystem::BeginServerTravel()
{
	BeginLoading(true);if(!State)return;
	bAwaitingServer=false;PreparationCancelAction.Unbind();WorldLoadingStartedAt=FPlatformTime::Seconds();
	{FScopeLock Lock(&State->Mutex);State->Data.CanCancel=false;}
	State->Advance(20,TEXT("게임 서버 접속 중…"));
}
void UProjectLoadingScreenSubsystem::AttachOverlay()
{
	if(!IsLoading())return;
	auto* Viewport=GetGameInstance()->GetGameViewportClient();if(!Viewport)return;
	if(AttachedViewport.Get()==Viewport && Overlay)return;
	if(auto* Old=AttachedViewport.Get();Old && Overlay)Old->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	Overlay=MakeProjectLoadingScreenWidget(State,FSimpleDelegate::CreateUObject(this,&ThisClass::RequestCancelPreparation));
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(),1000001);AttachedViewport=Viewport;
}
void UProjectLoadingScreenSubsystem::PreLoadMap(const FWorldContext& Context,const FString& Map)
{
	if(Context.OwningGameInstance!=GetGameInstance() || Context.RunAsDedicated)return;
	if(ProjectLoading::IsFrontEnd(Map)){CancelLoading();return;}
	BeginLoading(GetWorld() && GetWorld()->GetNetMode()==NM_Client);State->Advance(30,TEXT("맵을 읽는 중"));
	{FScopeLock Lock(&State->Mutex);State->Data.Map=Map;}
#if !UE_SERVER
	if(IsMoviePlayerEnabled() && GetMoviePlayer() && !GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		FLoadingScreenAttributes Attributes;Attributes.bAutoCompleteWhenLoadingCompletes=true;Attributes.bMoviesAreSkippable=false;
		Attributes.bAllowEngineTick=false;Attributes.WidgetLoadingScreen=SNew(ProjectLoading::SScreen).State(State);
		GetMoviePlayer()->SetupLoadingScreen(Attributes);
	}
#endif
}
void UProjectLoadingScreenSubsystem::PostLoadMap(UWorld* World)
{
	if(!World || World->GetGameInstance()!=GetGameInstance())return;
	if(ProjectLoading::IsFrontEnd(World->GetOutermost()->GetName())){CancelLoading();return;}
	if(!IsLoading())return;
	State->Advance(70,TEXT("맵 로딩 완료 · 플레이 화면 준비 중"));AttachOverlay();
}
void UProjectLoadingScreenSubsystem::BeginMultiplayerGate(const FString& Epoch,const FString& OwnId,const FString& OwnName,int32 Expected,bool Released)
{
	BeginLoading(true);EpochId=Epoch;bAllowCurrentWorld=true;
	if(bAwaitingServer){bAwaitingServer=false;WorldLoadingStartedAt=FPlatformTime::Seconds();}
	PreparationCancelAction.Unbind();
	FScopeLock Lock(&State->Mutex);State->Data.CanCancel=false;State->Data.Multiplayer=true;State->Data.Released=Released;State->Data.Expected=Expected;State->Data.OwnId=OwnId;
	if(!OwnName.IsEmpty())State->Data.OwnName=OwnName;
}
void UProjectLoadingScreenSubsystem::UpdateParticipants(const FString& Epoch,const TArray<FProjectLoadingParticipant>& Participants)
{
	if(!State || Epoch!=EpochId)return;FScopeLock Lock(&State->Mutex);
	for(const auto& P:Participants)
	{
		if(auto* Existing=State->Data.Participants.FindByPredicate([&](const auto& X){return X.UserId==P.UserId;}))*Existing=P;
		else State->Data.Participants.Add(P);
	}
}
void UProjectLoadingScreenSubsystem::ReleaseMultiplayerGate(const FString& Epoch)
{if(State && Epoch==EpochId){FScopeLock Lock(&State->Mutex);State->Data.Released=true;}}
void UProjectLoadingScreenSubsystem::NotifyViewReady()
{if(State)State->Advance(100,TEXT("준비 완료 · 다른 참가자를 기다리는 중"));}
uint8 UProjectLoadingScreenSubsystem::GetLocalProgress() const {return State?State->Read().Progress:0;}
bool UProjectLoadingScreenSubsystem::FinishPresentation(float RevealSeconds)
{
	if(!IsLoading())return false;if(bAwaitingServer)return true;
	PendingRevealSeconds=RevealSeconds;NotifyViewReady();State->TryClose(FPlatformTime::Seconds());return true;
}
void UProjectLoadingScreenSubsystem::CancelLoading()
{
	if(auto* Viewport=AttachedViewport.Get();Viewport && Overlay)Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	AttachedViewport.Reset();Overlay.Reset();DepartureWorld.Reset();EpochId.Reset();bAllowCurrentWorld=false;
	bAwaitingServer=false;PreparationProgress=0;PhaseStartedAt=0;WorldLoadingStartedAt=0;PreparationCancelAction.Unbind();
	if(State){FScopeLock Lock(&State->Mutex);State->Data.Active=false;}
}
void UProjectLoadingScreenSubsystem::FailLoading(const FString& Message)
{
	CancelLoading();
	if(auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();Auth && Auth->IsSignedIn())Auth->InvalidateSessionFromServerFailure(Message);
	else if(auto* PC=GetGameInstance()->GetFirstLocalPlayerController())PC->ClientReturnToMainMenuWithTextReason(FText::FromString(Message));
}
bool UProjectLoadingScreenSubsystem::TickLoading(float Delta)
{
	if(!IsLoading())return true;AttachOverlay();const double Now=FPlatformTime::Seconds();auto Snapshot=State->Read();
	if(bAwaitingServer)
	{
		// Backend allocation already has 120s queue / 90s startup bounds. A short
		// grace lets its ordinary rollback arrive; never consume map-loading's timeout here.
		const double Limit=PreparationProgress>=15?20:PreparationProgress>=10?100:130;
		if(Now-PhaseStartedAt>Limit)FailLoading(TEXT("세션 또는 게임 서버 준비 시간이 초과되었습니다. 다시 로그인해 주세요."));
		return true;
	}
	if(Now-WorldLoadingStartedAt>120){FailLoading(TEXT("참가자 또는 맵 로딩 시간이 초과되었습니다. 다시 로그인해 주세요."));return true;}
	UWorld* World=GetWorld();
	if(World && (bAllowCurrentWorld || World!=DepartureWorld.Get()) && World->HasBegunPlay())
	{
		int32 Total=0,Ready=0;
		for(const auto* Level:World->GetStreamingLevels())if(IsValid(Level) && (Level->ShouldBeLoaded() || Level->ShouldBeVisible()))
		{++Total;if((!Level->ShouldBeLoaded() || Level->IsLevelLoaded()) && (!Level->ShouldBeVisible() || Level->IsLevelVisible()))++Ready;}
		State->Advance(uint8(70+(Total>0?20*Ready/Total:20)),TEXT("플레이 화면·시점 준비 중"));
	}
	if(Snapshot.Progress>=100)
	{
		State->TryClose(Now);Snapshot=State->Read();
		if(Snapshot.Closing && Now-Snapshot.ClosingAt>=.4)
		{
			const float Reveal=PendingRevealSeconds;CancelLoading();
			if(auto* GI=Cast<UProjectProject01GameInstance>(GetGameInstance()))GI->FadeEntireScreenFromBlack(Reveal);
		}
	}
	return true;
}
