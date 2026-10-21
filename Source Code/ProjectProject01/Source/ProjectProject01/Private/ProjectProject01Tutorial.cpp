#include "ProjectProject01Tutorial.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LoginWidget.h"
#include "PlayerCharacter.h"
#include "MannequinAICharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BoxComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"

namespace ProjectTutorial
{
	const FName Map(TEXT("/Game/MyProject/Level/MultiplayerTutorial"));
	FText Text(const TCHAR* Value) { return FText::FromString(Value); }
	FString RemoveObsoleteCommandSentence(FString Result)
	{
		// Strip this removed sentence from already-saved plans as well as native defaults.
		for (const TCHAR* Removed : {
			TEXT("직접 조작하지 않아도 명령할 수 있으며, 직접 조작 중 내린 명령은 조작 해제 후 수행합니다."),
			TEXT("직접 조작하지 않아도 명령을 수행할 수 있으며, 직접 조작 중 내린 명령은 조작 해제 후 수행합니다.") })
		{
			Result.ReplaceInline(*(FString(Removed)+TEXT("\n")),TEXT(""));
			Result.ReplaceInline(Removed,TEXT(""));
		}
		return Result;
	}
	FText ResolveText(const FText& Value)
	{
		FString Result=RemoveObsoleteCommandSentence(Value.ToString());
		// Keep already-saved tutorial plans consistent with the explicit confirmation button.
		Result.ReplaceInline(TEXT("‘조작 연습 시작’"),TEXT("‘확인’"));
		if (auto* S=UProjectProject01GameUserSettings::Get())
		{
			Result.ReplaceInline(TEXT("{Sprint}"),*S->GetSprintKey().GetDisplayName().ToString());
			Result.ReplaceInline(TEXT("{Help}"),*S->GetHelpPingKey().GetDisplayName().ToString());
			Result.ReplaceInline(TEXT("{Danger}"),*S->GetDangerPingKey().GetDisplayName().ToString());
			Result.ReplaceInline(TEXT("{Location}"),*S->GetLocationPingKey().GetDisplayName().ToString());
			Result.ReplaceInline(TEXT("{PTT}"),*S->GetVoicePushToTalkKey().GetDisplayName().ToString());
			Result.ReplaceInline(TEXT("{Voice}"),*S->GetVoiceToggleKey().GetDisplayName().ToString());
			const FString Move=S->GetMoveForwardKey().GetDisplayName().ToString()+TEXT(" / ")+
				S->GetMoveLeftKey().GetDisplayName().ToString()+TEXT(" / ")+S->GetMoveBackwardKey().GetDisplayName().ToString()+TEXT(" / ")+S->GetMoveRightKey().GetDisplayName().ToString();
			Result.ReplaceInline(TEXT("{Move}"),*Move);
		}
		return FText::FromString(Result);
	}
	UTextBlock* Label(UWidgetTree* Tree,UVerticalBox* Root,int32 Size)
	{
		auto* T=Tree->ConstructWidget<UTextBlock>();
		T->SetColorAndOpacity(FLinearColor::White); T->SetAutoWrapText(true); T->SetWrapTextAt(620);
		T->SetJustification(ETextJustify::Left); T->SetLineHeightPercentage(1.2f);
		auto Font=T->GetFont(); Font.Size=Size; T->SetFont(Font);
		Root->AddChildToVerticalBox(T)->SetPadding(FMargin(8)); return T;
	}
	UButton* Button(UWidgetTree* Tree,UVerticalBox* Root,const TCHAR* Caption)
	{
		auto* B=Tree->ConstructWidget<UButton>(); auto* T=Tree->ConstructWidget<UTextBlock>();
		T->SetText(Text(Caption)); T->SetColorAndOpacity(ProjectDFSDUI::Text());
		auto Font=T->GetFont(); Font.Size=20; T->SetFont(Font); B->AddChild(T);ProjectDFSDUI::Apply(B);
		Root->AddChildToVerticalBox(B)->SetPadding(FMargin(8)); return B;
	}
}

void UProjectTutorialTargetMovement::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
	PracticeInput=GetPendingInputVector();
	Super::TickComponent(DeltaTime,TickType,TickFunction);
}
void UProjectTutorialTargetMovement::ServerAutonomousProxyTick(float DeltaTime)
{
	// Keep the normal collision/gravity/navigation physics, without pretending the bot
	// is a local human (which would also enable owner-only UI, audio and camera fades).
	if (HasValidData() && CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled() &&
		AProjectProject01TutorialGameMode::IsTutorialWorld(GetWorld())) ControlledCharacterMove(PracticeInput,DeltaTime);
}
AProjectTutorialTargetCharacter::AProjectTutorialTargetCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UProjectTutorialTargetMovement>(ACharacter::CharacterMovementComponentName))
{
}

UProjectProject01ObjectiveComponent::UProjectProject01ObjectiveComponent()
{
	SetIsReplicatedByDefault(true);
}
void UProjectProject01ObjectiveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(UProjectProject01ObjectiveComponent,State,COND_OwnerOnly);
}
void UProjectProject01ObjectiveComponent::BeginPlay() { Super::BeginPlay(); OnRep_State(); }
bool UProjectProject01ObjectiveComponent::UpdateObjective(FName Id,FText Title,FText Instructions,int32 Step,int32 Total)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Id.IsNone() || Title.IsEmpty()) return false;
	if (State.Id==Id && State.Title.EqualTo(Title) && State.Instructions.EqualTo(Instructions) && State.Step==Step && State.Total==Total && !State.bCompleted) return false;
	State.Id=Id; State.Title=Title; State.Instructions=Instructions;
	State.Total=FMath::Max(0,Total); State.Step=FMath::Clamp(Step,0,State.Total); State.bCompleted=false; ++State.Revision;
	GetOwner()->ForceNetUpdate(); OnRep_State(); return true;
}
void UProjectProject01ObjectiveComponent::CompleteObjective()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || State.Id.IsNone() || State.bCompleted) return;
	State.bCompleted=true; ++State.Revision; GetOwner()->ForceNetUpdate(); OnRep_State();
}
void UProjectProject01ObjectiveComponent::OnRep_State()
{
	auto* PC=Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || State.Id.IsNone() || !PC->GetLocalPlayer()) return;
	if (State.Revision==LastShownRevision) return;
	LastShownRevision=State.Revision;
	if (IsValid(HUD)) HUD->RemoveFromParent();
	HUD=CreateWidget<UProjectProject01TransientNoticeWidget>(PC,UProjectProject01TransientNoticeWidget::StaticClass());
	if (HUD)
	{
		FString Notice=(State.bCompleted ? FString(TEXT("목표를 달성했습니다.")) : FString(TEXT("목표가 갱신되었습니다.")))+TEXT("\n")+State.Title.ToString();
		if (!AProjectProject01TutorialGameMode::IsTutorialWorld(GetWorld()) && !State.Instructions.IsEmpty()) Notice+=TEXT("\n")+State.Instructions.ToString();
		HUD->ShowNotice(Notice,2.5f,1.5f);
		HUD->AddToViewport(935);
	}
}

AProjectProject01TutorialPlan::AProjectProject01TutorialPlan()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	auto Add=[](TArray<FProjectTutorialStep>& Out,const TCHAR* Id,const TCHAR* Title,const TCHAR* Hint,EProjectTutorialCondition Condition,float Amount=1,FName Event=NAME_None)
	{
		FProjectTutorialStep S; S.Id=Id; S.Title=ProjectTutorial::Text(Title); S.Instructions=ProjectTutorial::Text(Hint); S.Condition=Condition; S.RequiredAmount=Amount; S.Event=Event; Out.Add(S);
	};
	Add(SurvivorSteps,TEXT("Survivor.Intro"),TEXT("동료와 협력하여 탈출하세요."),TEXT("당신은 생존자입니다.\n\n마네킹을 바라보면서 자신에게 접근하지 못하게 막고,\n무사히 마지막 탈출구까지 도달하는 것이\n최종 목표입니다."),EProjectTutorialCondition::Continue);
	Add(SurvivorSteps,TEXT("Survivor.Walk"),TEXT("{Move}로 이동하며\n마우스를 통해 시점을 움직일 수 있습니다."),TEXT(""),EProjectTutorialCondition::Walk,1000);
	Add(SurvivorSteps,TEXT("Survivor.Sprint"),TEXT("이동하며 {Sprint}를 눌러 달릴 수 있습니다."),TEXT("달릴 때 스태미나가 소모되며\n싱글 플레이와는 다르게 스태미나가 전부 소모되면\n달릴 수 없습니다."),EProjectTutorialCondition::Sprint,2000);
	Add(SurvivorSteps,TEXT("Survivor.Observe"),TEXT("마네킹을 바라보면\n적이 다가오지 못하게 막을 수 있습니다."),TEXT("싱글 플레이에서는 파트너가 존재하나\n여기서는 존재하지 않습니다.\n\n동료와 협력하여\n사방을 주의하면서 다니는 것이 중요합니다."),EProjectTutorialCondition::ObserveMannequin,3);
	Add(SurvivorSteps,TEXT("Survivor.Escape"),TEXT("이제 탈출구로 이동할 시간입니다."),TEXT("배운 조작을 활용해 탈출 지점에 도달하세요."),EProjectTutorialCondition::Trigger,1,TEXT("Tutorial.Escape"));
	Add(MannequinSteps,TEXT("Mannequin.Intro"),TEXT("생존자의 탈출을 저지하세요"),TEXT("당신은 마네킹 플레이어입니다.\n마네킹의 시점을 선택하고 명령을 내리거나\n직접 조작할 수 있습니다.\n\n생존자 전원을 붙잡아 탈출을 막는 것이\n최종 목표입니다.\n생존자가 바라보는 마네킹은 멈추게 되며\n이들이 바라보지 않는 시야에서 잡는 것이\n중요한 관건입니다."),EProjectTutorialCondition::Continue);
	Add(MannequinSteps,TEXT("Mannequin.Select"),TEXT("다른 마네킹에게 빙의를 할지 선택할 수 있습니다."),TEXT("머리 위의 번호를 확인하고\n0~9의 숫자 키 중 원하는 번호를 눌러\n다른 마네킹으로 시점을 전환할 수 있습니다."),EProjectTutorialCondition::SelectMannequin,3);
	Add(MannequinSteps,TEXT("Mannequin.Manual"),TEXT("자신이 선택한 마네킹을 직접 조종할 수 있습니다."),TEXT("V키를 눌러 직접 조작을 시작하세요."),EProjectTutorialCondition::ManualControl);
	Add(MannequinSteps,TEXT("Mannequin.Move"),TEXT("{Move}로 이동하며\n마우스를 통해 시점을 움직일 수 있습니다."),TEXT("직접 조작 중에만\n마네킹을 조종해 이동시킬 수 있습니다."),EProjectTutorialCondition::MoveMannequin,1000);
	Add(MannequinSteps,TEXT("Mannequin.Command"),TEXT("마네킹에게 맡길 명령을 지정하세요."),TEXT("시점을 선택한 마네킹에게 명령을 내릴 수 있습니다.\n\nZ: 정찰\nX: 감지 범위 추격\nC: 자신을 따라오기\nQ: 따라오는 마네킹을 정찰 상태로 변경\n\nB: 전체 마네킹의 접촉 데스카운트 소모\n    활성/비활성화\n마우스 좌클릭: 직접 조작 중 공격"),EProjectTutorialCondition::CommandPractice,3);
	Add(MannequinSteps,TEXT("Mannequin.Release"),TEXT("이제 빙의를 다른 번호의 마네킹을 선택해\n명령을 내리세요."),TEXT("다음 맵으로 이동하기 전까지\n한번 내린 명령은 유지됩니다."),EProjectTutorialCondition::ReleaseMannequin,3);
	Add(MannequinSteps,TEXT("Mannequin.Capture"),TEXT("연습용 생존자 1명이 나타났습니다."),TEXT("직접 마네킹을 조작하여\n생존자의 시야를 피해 붙잡으세요.\n\n(단, 마네킹 플레이어는 접촉 데스카운트를\n비활성화 시켜도 언제든 소모시킬 수 있습니다.)"),EProjectTutorialCondition::Capture);
	SurvivorClosing=ProjectTutorial::Text(TEXT("동료와 함께 무사히 탈출하시길 바랍니다.\n건승을 빕니다!"));
	MannequinClosing=ProjectTutorial::Text(TEXT("생존자가 탈출할 수 없게 저지하시길 바랍니다.\n건승을 빕니다!"));
}

AProjectProject01ObjectiveTrigger::AProjectProject01ObjectiveTrigger()
{
	Volume=CreateDefaultSubobject<UBoxComponent>(TEXT("ObjectiveVolume")); SetRootComponent(Volume);
	Volume->SetBoxExtent(FVector(150,150,150)); Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore); Volume->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
}
void AProjectProject01ObjectiveTrigger::BeginPlay()
{
	Super::BeginPlay(); if (HasAuthority()) Volume->OnComponentBeginOverlap.AddUniqueDynamic(this,&ThisClass::OnOverlap);
}
bool AProjectProject01ObjectiveTrigger::ApplyTo(AMultiplayTestPlayerController* PC)
{
	if (!HasAuthority() || !IsValid(PC) || (bOncePerPlayer && UsedBy.Contains(PC))) return false;
	auto* Objective=PC->FindComponentByClass<UProjectProject01ObjectiveComponent>();
	if (!Objective || (!RequiredObjective.IsNone() && Objective->GetObjective().Id!=RequiredObjective)) return false;
	bool bApplied=false;
	if (!TutorialEvent.IsNone())
	{
		if (auto* Tutorial=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) bApplied=Tutorial->SignalEvent(PC,TutorialEvent);
	}
	else if (bCompleteCurrentObjective)
	{
		const auto State=Objective->GetObjective();
		if (!State.Id.IsNone() && !State.bCompleted) { Objective->CompleteObjective(); bApplied=true; }
	}
	else bApplied=Objective->UpdateObjective(NewObjectiveId,Title,Instructions);
	if (bApplied) UsedBy.Add(PC); return bApplied;
}
void AProjectProject01ObjectiveTrigger::OnOverlap(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,int32,bool,const FHitResult&)
{
	if (!HasAuthority() || !GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>()) return;
	auto* Pawn=Cast<APawn>(Other); auto* Entering=Pawn ? Cast<AMultiplayTestPlayerController>(Pawn->GetController()) : nullptr;
	if (!Entering || Entering->GetPawn()!=Pawn || Entering->GetMatchPhase()!=EMultiplayTestMatchPhase::InProgress ||
		Entering->GetSurvivorState()==EMultiplayTestSurvivorState::Eliminated || Entering->GetSurvivorState()==EMultiplayTestSurvivorState::TransitionWaiting || Entering->GetSurvivorState()==EMultiplayTestSurvivorState::Escaped) return;
	if (Audience==EProjectObjectiveAudience::EnteringPlayer) { ApplyTo(Entering); return; }
	for (TActorIterator<AMultiplayTestPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->GetMatchPhase()!=EMultiplayTestMatchPhase::InProgress) continue;
		const bool bSurvivor=It->GetSurvivorState()!=EMultiplayTestSurvivorState::NotApplicable;
		if (Audience==EProjectObjectiveAudience::Everyone || (Audience==EProjectObjectiveAudience::Survivors && bSurvivor) || (Audience==EProjectObjectiveAudience::Mannequin && !bSurvivor)) ApplyTo(*It);
	}
}

bool AProjectProject01TutorialGameMode::IsTutorialWorld(const UWorld* World)
{
	if (!World) return false;
	if (World->GetAuthGameMode<AProjectProject01TutorialGameMode>()) return true;
	const auto* GS=World->GetGameState();
	const auto Class=GS && GS->GameModeClass ? GS->GameModeClass : World->GetWorldSettings()->DefaultGameMode;
	return Class && Class->IsChildOf(StaticClass());
}
AProjectProject01TutorialMessageTrigger::AProjectProject01TutorialMessageTrigger()
{
	Volume=CreateDefaultSubobject<UBoxComponent>(TEXT("MessageVolume")); SetRootComponent(Volume);
	Volume->SetBoxExtent(FVector(150)); Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore); Volume->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
}
void AProjectProject01TutorialMessageTrigger::BeginPlay()
{
	Super::BeginPlay(); if (HasAuthority()) Volume->OnComponentBeginOverlap.AddUniqueDynamic(this,&ThisClass::OnOverlap);
}
void AProjectProject01TutorialMessageTrigger::OnOverlap(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,int32,bool,const FHitResult&)
{
	if (!HasAuthority() || (bOnce && bShown)) return;
	auto* Pawn=Cast<APawn>(Other);
	auto* PC=Pawn ? Cast<AMultiplayTestPlayerController>(Pawn->GetController()) : nullptr;
	if (auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>(); PC && Mode)
		if (Mode->ShowMessage(PC,Message,RequiredStep)) bShown=true;
}
bool AProjectProject01TutorialGameMode::ShowMessage(AMultiplayTestPlayerController* PC,FText Message,FName RequiredStep)
{
	const auto* Step=GetCurrentStep();
	if (PC!=Learner || !bInitialized || bFinishing || bAwaitingRead || Message.IsEmpty() || !Step || (!RequiredStep.IsNone() && Step->Id!=RequiredStep)) return false;
	ExtraMessage=Message; bAwaitingRead=true; SetMenuInput(true); return true;
}
void AProjectProject01TutorialGameMode::InitGame(const FString& Map,const FString& Options,FString& Error)
{
	Super::InitGame(Map,Options,Error);
	bMannequin=UGameplayStatics::ParseOption(Options,TEXT("TutorialRole")).Equals(TEXT("Mannequin"),ESearchCase::IgnoreCase);
	bPracticeOnly=UGameplayStatics::ParseOption(Options,TEXT("TutorialPracticeOnly"))==TEXT("1");
	bFreePractice=UGameplayStatics::ParseOption(Options,TEXT("FreePractice"))==TEXT("1");
	auto ReadCount=[&Options](const TCHAR* Key,int32 Default,int32 Maximum)
	{
		int32 Value=0;const FString Text=UGameplayStatics::ParseOption(Options,Key);
		return LexTryParseString(Value,*Text) ? FMath::Clamp(Value,1,Maximum) : Default;
	};
	PracticeMannequins=ReadCount(TEXT("PracticeMannequins"),10,10);
	PracticeSurvivors=ReadCount(TEXT("PracticeSurvivors"),1,2);
	bPracticeStationary=UGameplayStatics::ParseOption(Options,TEXT("PracticeStationary"))==TEXT("1");
	bPracticeStandingWatch=UGameplayStatics::ParseOption(Options,TEXT("PracticeStandingWatch"))!=TEXT("0");
	bPracticeInfiniteLives=UGameplayStatics::ParseOption(Options,TEXT("PracticeInfiniteLives"))==TEXT("1");
	bPracticeCorners=UGameplayStatics::ParseOption(Options,TEXT("PracticeCorners"))==TEXT("1");
	PracticeCornerCount=FMath::Min(ReadCount(TEXT("PracticeCornerCount"),1,2),PracticeSurvivors);
	bPracticeDirector=UGameplayStatics::ParseOption(Options,TEXT("PracticeDirector"))==TEXT("1");
	bPracticeDirectorToggleContact=UGameplayStatics::ParseOption(Options,TEXT("PracticeDirectorToggleContact"))!=TEXT("0");
	PracticeRandom.GenerateNewSeed();
	if (bFreePractice) MannequinSpawnCount=PracticeMannequins;
}
void AProjectProject01TutorialGameMode::PreLogin(const FString&,const FString&,const FUniqueNetIdRepl&,FString& Error)
{
	Error=TEXT("Tutorial is local practice only; network connections are not accepted.");
}
FString AProjectProject01TutorialGameMode::InitNewPlayer(APlayerController* PC,const FUniqueNetIdRepl& Id,const FString& Options,const FString& Portal)
{
	if (GetNetMode()!=NM_Standalone) return TEXT("Tutorial requires standalone practice.");
	const FString DesiredRole=bMannequin ? TEXT("Mannequin") : TEXT("Survivor");
	if (auto* MPC=Cast<AMultiplayTestPlayerController>(PC))
	{
		FProjectProject01ValidatedJoinClaim Claim; Claim.UserId=TEXT("Tutorial.Learner"); Claim.MatchId=TEXT("Tutorial.Local"); Claim.Role=DesiredRole; Claim.DisplayName=TEXT("연습 플레이어"); MPC->SetAuthenticatedLobbyIdentity(Claim);
	}
	return Super::InitNewPlayer(PC,Id,Options+TEXT("?LobbyRole=")+DesiredRole,Portal);
}
void AProjectProject01TutorialGameMode::PostLogin(APlayerController* PC)
{
	Super::PostLogin(PC); Learner=Cast<AMultiplayTestPlayerController>(PC);
	if (GetWorld()->HasBegunPlay()) InitializePractice();
}
void AProjectProject01TutorialGameMode::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AProjectProject01TutorialPlan> It(GetWorld()); It; ++It) { Plan=*It; break; }
	if (!Plan) Plan=GetWorld()->SpawnActor<AProjectProject01TutorialPlan>();
	Steps=bMannequin ? Plan->MannequinSteps : Plan->SurvivorSteps;
	// Upgrade only the obsolete built-in explanation serialized in older maps.
	// Keep authored steps, distances and unrelated custom instructions intact.
	for (auto& Step:Steps)
	{
		if (Step.Id==TEXT("Mannequin.Command") && Step.Instructions.ToString().Contains(TEXT("직접 조작 중 해당 키를 누르면")))
			for (const auto& Default:GetDefault<AProjectProject01TutorialPlan>()->MannequinSteps)
				if (Default.Id==Step.Id) { Step.Instructions=Default.Instructions;break; }
		if ((Step.Id==TEXT("Survivor.Walk") || Step.Id==TEXT("Mannequin.Move")) && FMath::IsNearlyEqual(Step.RequiredAmount,800.f)) Step.RequiredAmount=1000.f;
		if (Step.Id==TEXT("Survivor.Sprint") && FMath::IsNearlyEqual(Step.RequiredAmount,1200.f)) Step.RequiredAmount=2000.f;
		Step.Instructions=FText::FromString(ProjectTutorial::RemoveObsoleteCommandSentence(Step.Instructions.ToString()));
		auto Canonical=[](const FText& Text)
		{
			FString Out;for (TCHAR Character:Text.ToString()) if (!FChar::IsWhitespace(Character)) Out.AppendChar(Character);
			return Out;
		};
		const auto& Defaults=bMannequin ? GetDefault<AProjectProject01TutorialPlan>()->MannequinSteps : GetDefault<AProjectProject01TutorialPlan>()->SurvivorSteps;
		for (const auto& Default:Defaults) if (Default.Id==Step.Id)
		{
			if (Canonical(Step.Title)==Canonical(Default.Title)) Step.Title=Default.Title;
			if (Canonical(Step.Instructions)==Canonical(Default.Instructions)) Step.Instructions=Default.Instructions;
			// Old command lists used inline separators; migrate their standard layout only.
			if (Step.Id==TEXT("Mannequin.Command") && Step.Instructions.ToString().Contains(TEXT("Z: 정찰 / X: 감지 범위 추격 / C: 자신을 따라오기")))
			{
				FString Layout=Step.Instructions.ToString();
				Layout.ReplaceInline(TEXT("Z: 정찰 / X: 감지 범위 추격 / C: 자신을 따라오기"),TEXT("Z: 정찰\nX: 감지 범위 추격\nC: 자신을 따라오기"));
				Layout.ReplaceInline(TEXT("\nZ:"),TEXT("\n\nZ:"));Layout.ReplaceInline(TEXT("\nB:"),TEXT("\n\nB:"));
				Layout.ReplaceInline(TEXT("B: 전체 마네킹의 접촉 데스카운트 소모 활성/비활성화"),TEXT("B: 전체 마네킹의 접촉 데스카운트 소모\n    활성/비활성화"));
				Step.Instructions=FText::FromString(Layout);
			}
			break;
		}
	}
	InitializePractice();
}
void AProjectProject01TutorialGameMode::InitializePractice()
{
	if (bInitialized || !Learner || !Plan || GetNetMode()!=NM_Standalone) return;
	if (Steps.IsEmpty()) { UE_LOG(LogTemp,Error,TEXT("TutorialPlan has no steps for this role.")); ReturnToMatching(); return; }
	bInitialized=true; bGlobalMannequinCatchDamageEnabled=bFreePractice && !bMannequin; MatchTimeLimitSeconds=0;
	MatchStartWorldSeconds=GetWorld()->GetTimeSeconds(); SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);
	// Standalone PostLogin precedes GameMode BeginPlay and runtime mannequin spawning.
	if (bMannequin && !Learner->GetViewedMannequin()) AssignRandomInitialMannequinView(Learner);
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It) It->SetCatchDamageEnabled(bGlobalMannequinCatchDamageEnabled);
	if (bFreePractice)
	{
		bAwaitingRead=false;PracticeTacticalSeconds=0;
		NextDirectorContactToggle=GetWorld()->GetTimeSeconds()+PracticeRandom.FRandRange(6,12);
		if (bMannequin) SpawnPracticeSurvivors(PracticeSurvivors);
		if (auto* Goals=Learner->FindComponentByClass<UProjectProject01ObjectiveComponent>())
			Goals->UpdateObjective(bMannequin ? FName(TEXT("Practice.Capture")) : FName(TEXT("Survivor.Escape")),ProjectTutorial::Text(bMannequin ? TEXT("연습용 생존자를 붙잡으세요.") : TEXT("마네킹을 경계하며 탈출 지점에 도달하세요.")),FText());
		PracticeWidget=CreateWidget<UProjectProject01PracticeWidget>(Learner,UProjectProject01PracticeWidget::StaticClass());
		if (PracticeWidget) PracticeWidget->AddToViewport(1500);
		// Let the controller's initial readiness/reveal timers run before pausing.
		bPracticeAwaitingPresentation=true;bPracticeMenuOpen=true;SetMenuInput(true);return;
	}
	Widget=CreateWidget<UProjectProject01TutorialWidget>(Learner,UProjectProject01TutorialWidget::StaticClass());
	if (Widget) Widget->AddToViewport(960);
	StepIndex=GetInitialStepIndex();
	EnterStep();
}
int32 AProjectProject01TutorialGameMode::GetInitialStepIndex() const
{
	if (!bPracticeOnly) return 0;
	for (int32 Index=Steps.Num()-1;Index>=0;--Index)
		if (Steps[Index].Condition==EProjectTutorialCondition::Trigger || Steps[Index].Condition==EProjectTutorialCondition::Capture) return Index;
	return FMath::Max(0,Steps.Num()-1);
}
const FProjectTutorialStep* AProjectProject01TutorialGameMode::GetCurrentStep() const { return !bFreePractice && Steps.IsValidIndex(StepIndex) ? &Steps[StepIndex] : nullptr; }
FText AProjectProject01TutorialGameMode::GetClosingText() const { return Plan ? (bMannequin ? Plan->MannequinClosing : Plan->SurvivorClosing) : FText::GetEmpty(); }
void AProjectProject01TutorialGameMode::SetMenuInput(bool bCursor)
{
	if (!Learner) return;
	Learner->bShowMouseCursor=bCursor; Learner->ResetIgnoreMoveInput(); Learner->ResetIgnoreLookInput();
	Learner->SetIgnoreMoveInput(bCursor || bFinishing); Learner->SetIgnoreLookInput(bCursor || bFinishing);
	if (bCursor) if (auto* Character=Cast<ACharacter>(Learner->GetPawn())) Character->GetCharacterMovement()->StopMovementImmediately();
	if (bCursor) for (auto& Bot:PracticeBots) if (IsValid(Bot.Pawn)) Bot.Pawn->GetCharacterMovement()->StopMovementImmediately();
	if (bCursor) { FInputModeGameAndUI Input; if (PracticeWidget) Input.SetWidgetToFocus(PracticeWidget->TakeWidget()); else if (Widget) Input.SetWidgetToFocus(Widget->TakeWidget()); Input.SetHideCursorDuringCapture(false); Learner->SetInputMode(Input); }
	else Learner->SetInputMode(FInputModeGameOnly());
}
void AProjectProject01TutorialGameMode::EnterStep()
{
	const auto* S=GetCurrentStep(); if (!S || !Learner) return;
	Progress=0;
	CommandedMannequin.Reset(); ReleasedMannequin.Reset(); LastSelectedMannequin=Learner->GetViewedMannequin();
	LastLocation=Learner->GetPawn() ? Learner->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	bAwaitingRead=true;
	if (auto* C=Learner->FindComponentByClass<UProjectProject01ObjectiveComponent>()) C->UpdateObjective(S->Id,ProjectTutorial::ResolveText(S->Title),ProjectTutorial::ResolveText(S->Instructions),StepIndex+1,Steps.Num());
	if (S->Condition==EProjectTutorialCondition::Capture) SpawnPracticeSurvivor();
	SetMenuInput(true);
}
void AProjectProject01TutorialGameMode::AdvanceStep()
{
	if (bFinishing) return;
	++StepIndex;
	if (StepIndex>=Steps.Num()) FinishTutorial(); else EnterStep();
}
void AProjectProject01TutorialGameMode::RecordAction(AMultiplayTestPlayerController* PC,EProjectTutorialCondition Action)
{
	const auto* S=GetCurrentStep();
	if (PC!=Learner || !bInitialized || bFinishing || bAwaitingRead || !S) return;
	if (Action==EProjectTutorialCondition::SelectMannequin)
	{
		auto* Viewed=PC->GetViewedMannequin();
		if (!Viewed || Viewed==LastSelectedMannequin.Get()) return;
		LastSelectedMannequin=Viewed;
		if (S->Condition==EProjectTutorialCondition::ReleaseMannequin)
		{
			if (auto* Commanded=CommandedMannequin.Get(); Commanded && Commanded!=Viewed && !PC->GetPawn() && !Commanded->IsManualControlEnabled())
			{ ReleasedMannequin=Commanded; CommandedMannequin.Reset(); }
			return;
		}
	}
	const bool bPersistentCommand=Action==EProjectTutorialCondition::PatrolCommand || Action==EProjectTutorialCondition::ChaseCommand || Action==EProjectTutorialCondition::FollowCommand;
	if (bPersistentCommand && S->Condition==EProjectTutorialCondition::ReleaseMannequin)
	{
		if (!ReleasedMannequin.IsValid()) CommandedMannequin=PC->GetViewedMannequin();
		return;
	}
	if (S->Condition==EProjectTutorialCondition::CommandPractice)
	{
		if (!bPersistentCommand || !IsValid(PC->GetViewedMannequin())) return;
		// RecordAction is called after the authoritative command succeeds. There is
		// no additional hidden review timer once the visible action goal is complete.
		++Progress; TryFinishPracticeStep(); return;
	}
	if (S->Condition==Action || (S->Condition==EProjectTutorialCondition::IssueCommand && bPersistentCommand))
	{ ++Progress; TryFinishPracticeStep(); }
}
bool AProjectProject01TutorialGameMode::TryFinishPracticeStep()
{
	const auto* S=GetCurrentStep();
	if (!S || bAwaitingRead || bFinishing || Progress<FMath::Max(.1f,S->RequiredAmount)) return false;
	AdvanceStep(); return true;
}
FText AProjectProject01TutorialGameMode::GetPracticeStatusText() const
{
	const auto* S=GetCurrentStep(); if (!S) return FText::GetEmpty();
	FString Hint=ProjectTutorial::ResolveText(S->Title).ToString();
	if (S->Condition==EProjectTutorialCondition::CommandPractice)
	{
		Hint+=FString::Printf(TEXT("\n명령 상태 확인 %d/%d\nZ / X / C 중 원하는 명령을 선택하세요."),FMath::FloorToInt(Progress),FMath::CeilToInt(S->RequiredAmount));
	}
	else if (S->Condition==EProjectTutorialCondition::ReleaseMannequin)
		Hint+=FString::Printf(TEXT("\n명령 맡기기 %d/%d\nZ/X/C 명령 → 다른 번호 선택"),FMath::FloorToInt(Progress),FMath::CeilToInt(S->RequiredAmount));
	else if (S->Condition==EProjectTutorialCondition::SelectMannequin)
		Hint+=FString::Printf(TEXT("\n시점 전환 %d/%d"),FMath::FloorToInt(Progress),FMath::CeilToInt(S->RequiredAmount));
	else if (S->Condition==EProjectTutorialCondition::ObserveMannequin) Hint+=FString::Printf(TEXT("\n마네킹 주시 %.1f / %g 초"),FMath::FloorToFloat(FMath::Min(Progress,S->RequiredAmount)*10.f)/10.f,S->RequiredAmount);
	else if (S->Condition==EProjectTutorialCondition::Walk || S->Condition==EProjectTutorialCondition::Sprint || S->Condition==EProjectTutorialCondition::MoveMannequin)
	{
		Hint+=FString::Printf(TEXT("\n%s %.1f / %.1f m"),S->Condition==EProjectTutorialCondition::Sprint?TEXT("달리기"):TEXT("이동"),
			FMath::FloorToFloat(FMath::Min(Progress,S->RequiredAmount)/10.f)/10.f,S->RequiredAmount/100.f);
	}
	return FText::FromString(Hint);
}
bool AProjectProject01TutorialGameMode::SignalEvent(AMultiplayTestPlayerController* PC,FName Event)
{
	if (bFreePractice)
	{
		if (!bReturning && !bPracticeRoundEnded && PC==Learner && !bMannequin && Event==TEXT("Tutorial.Escape"))
		{ FinishFreePractice();return true; }
		return false;
	}
	const auto* S=GetCurrentStep();
	if (!bInitialized || bFinishing || bAwaitingRead || PC!=Learner || !S || S->Condition!=EProjectTutorialCondition::Trigger || Event.IsNone() || S->Event!=Event) return false;
	if (++Progress>=FMath::Max(1.0f,S->RequiredAmount)) AdvanceStep(); return true;
}
void AProjectProject01TutorialGameMode::ContinueTutorial()
{
	if (bFinishing) { if (!bResultVisible) { bClosingConfirmed=true; SetMenuInput(false); } return; }
	if (!bAwaitingRead) return;
	bAwaitingRead=false;
	if (!ExtraMessage.IsEmpty()) { ExtraMessage=FText::GetEmpty(); SetMenuInput(false); return; }
	if (const auto* S=GetCurrentStep(); S && S->Condition==EProjectTutorialCondition::Continue) RecordAction(Learner,EProjectTutorialCondition::Continue);
	else SetMenuInput(false);
}
void AProjectProject01TutorialGameMode::ToggleTutorialMenu()
{
	if (bFreePractice) { ShowPracticeMenu(!bPracticeMenuOpen);return; }
	if (!Learner || bFinishing) return;
	// ESC may reopen the explanation, but only its explicit confirmation can dismiss it.
	if (!bAwaitingRead) { bAwaitingRead=true; SetMenuInput(true); }
}
void AProjectProject01TutorialGameMode::Tick(float Delta)
{
	Super::Tick(Delta);
	if (!bInitialized || !Learner || bReturning) return;
	if (bFreePractice)
	{
		if (bPracticeAwaitingPresentation)
		{
			if (!Learner->IsInitialGameplayPresentationReady()) return;
			if (bMannequin && bPracticeCorners)
			{
				auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation Floor;
				const bool bNavReady=Nav && Nav->ProjectPointToNavigation(Plan->GetActorLocation(),Floor,FVector(200,200,300));
				// Dynamic navigation is not ready during BeginPlay. Delay placement,
				// not the whole round indefinitely, and keep the learner behind the menu.
				if (!bNavReady && GetWorld()->GetTimeSeconds()-MatchStartWorldSeconds<3) return;
				TArray<FVector> Reserved;
				for(int32 Index=PracticeCornerCount;Index<PracticeBots.Num();++Index) if(PracticeBots[Index].Pawn) Reserved.Add(PracticeBots[Index].Pawn->GetActorLocation());
				for(int32 Index=0;Index<FMath::Min(PracticeCornerCount,PracticeBots.Num());++Index)
				{
					auto& Bot=PracticeBots[Index];if(!Bot.Pawn)continue;
					const auto* Capsule=Bot.Pawn->GetCapsuleComponent();FVector Location;FRotator Rotation;
					if(bNavReady && FindPracticeCornerLocation(Bot.Pawn->GetActorLocation(),Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight(),Reserved,Location,Rotation) && Bot.Pawn->TeleportTo(Location,Rotation))
					{Bot.bSpawnedAtCorner=true;++PracticeCornersPlaced;Bot.Controller->SetControlRotation(Rotation);Bot.LastLocation=Bot.Pawn->GetActorLocation();}
					Reserved.Add(Bot.Pawn->GetActorLocation());
				}
			}
			bPracticeAwaitingPresentation=false;ShowPracticeMenu(true);return;
		}
		// Local practice does not need to wait for every synthetic spectator's
		// death handoff to resolve a round. Keep the death animation, but stop AI now.
		if (!bPracticeRoundEnded)
		{
			if (bMannequin && !PracticeBots.IsEmpty())
			{
				bool bAllCaught=true;
				for (const auto& Bot:PracticeBots) if (IsValid(Bot.Pawn) && !Bot.Pawn->IsGameOver()) { bAllCaught=false;break; }
				if (bAllCaught) FinishFreePractice();
			}
			else if (const auto* Pawn=Cast<APlayerCharacter>(Learner->GetPawn());Pawn && Pawn->IsGameOver()) FinishFreePractice();
		}
		if (bPracticeRoundEnded)
		{ PracticeEndSeconds+=Delta;if (PracticeEndSeconds>=3.2f && !bPracticeMenuOpen) ShowPracticeMenu(true);return; }
		PracticeTacticalSeconds+=Delta;TickPracticeSurvivor(Delta);TickPracticeMannequinDirector(Delta);
		if (!bMannequin && Learner->GetPawn())
			for (TActorIterator<AProjectProject01ObjectiveTrigger> It(GetWorld());It;++It)
				if (It->TutorialEvent==TEXT("Tutorial.Escape") && It->Volume->IsOverlappingActor(Learner->GetPawn())) { It->ApplyTo(Learner);break; }
		return;
	}
	if (bFinishing)
	{
		if (!bClosingConfirmed) return;
		EndSeconds+=Delta;
		if (EndSeconds-Delta<=0)
			if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->FadeEntireScreenToBlack(1.0f);
		if (EndSeconds>=1.1f && !bResultVisible)
		{
			bResultVisible=true; SetMenuInput(true);
			if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->FadeEntireScreenFromBlack(0.6f);
		}
		return;
	}
	const auto* S=GetCurrentStep(); if (!S) return;
	if (bAwaitingRead)
	{
		// A late Pawn restart can restore GameOnly input after the explanation opened.
		if (!Learner->bShowMouseCursor) SetMenuInput(true);
		return;
	}
	APawn* Pawn=Learner->GetPawn();
	if (Pawn)
	{
		const float Distance=FVector::Dist2D(Pawn->GetActorLocation(),LastLocation); LastLocation=Pawn->GetActorLocation();
		const auto* Survivor=Cast<APlayerCharacter>(Pawn);
		const bool bMove=(S->Condition==EProjectTutorialCondition::Walk && Survivor && !Survivor->IsSprintingForBreathing()) || (S->Condition==EProjectTutorialCondition::MoveMannequin && Cast<AMannequinAICharacter>(Pawn));
		const bool bSprint=S->Condition==EProjectTutorialCondition::Sprint && Survivor && Survivor->IsSprintingForBreathing();
		if ((bMove || bSprint) && !Learner->IsMoveInputIgnored() && Pawn->GetVelocity().SizeSquared2D()>25 && Distance>0.1f)
		{ Progress+=FMath::Min(Distance,100.0f); }
	}
	if (S->Condition==EProjectTutorialCondition::ObserveMannequin)
	{
		bool bObserved=false;
		for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It)
			if (It->IsFrozenBySurvivorVision()) { bObserved=true; break; }
		Progress=bObserved ? Progress+FMath::Max(0.f,Delta) : 0;
	}
	if (S->Condition==EProjectTutorialCondition::ReleaseMannequin)
	{
		if (auto* Released=ReleasedMannequin.Get())
		{
			if (Released->IsManualControlEnabled()) ReleasedMannequin.Reset();
			else if (Cast<AAIController>(Released->GetController()))
			{
				++Progress; ReleasedMannequin.Reset();
				if (TryFinishPracticeStep()) return;
			}
		}
	}
	if (S->Condition==EProjectTutorialCondition::Walk || S->Condition==EProjectTutorialCondition::Sprint || S->Condition==EProjectTutorialCondition::MoveMannequin || S->Condition==EProjectTutorialCondition::ObserveMannequin)
		TryFinishPracticeStep();
	TickPracticeSurvivor(Delta);
	// A prerequisite may become true while already standing inside the volume.
	if (const auto* Current=GetCurrentStep(); Current && Current->Condition==EProjectTutorialCondition::Trigger && Learner->GetPawn())
		for (TActorIterator<AProjectProject01ObjectiveTrigger> It(GetWorld()); It; ++It)
			if (It->TutorialEvent==Current->Event && It->Volume->IsOverlappingActor(Learner->GetPawn()))
			{ It->ApplyTo(Learner); break; }
}
void AProjectProject01TutorialGameMode::UpdatePracticeTactics()
{
	if (PracticeBots.Num()<2) return;
	if (PracticeTacticalSeconds>=NextTacticalDecision)
	{
		const auto& A=PracticeBots[0];const auto& B=PracticeBots[1];
		if (!IsValid(A.Pawn) || !IsValid(B.Pawn) || A.Pawn->IsGameOver() || B.Pawn->IsGameOver()) return;
		const float Spacing=FVector::Dist2D(A.Pawn->GetActorLocation(),B.Pawn->GetActorLocation());
		const bool bCanCover=A.Controller && A.Controller->LineOfSightTo(B.Pawn);
		// Weighted by the current situation, never a scatter/regroup/cover clock.
		const float SpreadWeight=Spacing<350?3.f:1.f;
		const float RegroupWeight=Spacing>900?5.f:Spacing>550?2.f:.3f;
		const float CoverWeight=bCanCover && Spacing<900?2.5f:.2f;
		const float Pick=PracticeRandom.FRandRange(0,SpreadWeight+RegroupWeight+CoverWeight);
		PracticeTacticalPhase=Pick<SpreadWeight?0:Pick<SpreadWeight+RegroupWeight?1:2;
		NextTacticalDecision=PracticeTacticalSeconds+PracticeRandom.FRandRange(3.5f,8.5f);
		for (auto& Bot:PracticeBots) Bot.RepathSeconds=0;
	}
	if (PracticeTacticalSeconds>=NextCoverDecision)
	{
		PracticeCoverIndex=PracticeRandom.RandRange(0,PracticeBots.Num()-1);
		NextCoverDecision=PracticeTacticalSeconds+PracticeRandom.FRandRange(1.8f,4.8f);
	}
}

AMannequinAICharacter* AProjectProject01TutorialGameMode::GetMannequinPlayerFocusPawn() const
{
	return IsPracticeDirectorEnabled()?PracticeDirectorBody.Get():Super::GetMannequinPlayerFocusPawn();
}

void AProjectProject01TutorialGameMode::SelectPracticeDirectorBody(AMannequinAICharacter* Body)
{
	if (!IsPracticeDirectorEnabled() || !IsValid(Body) || PracticeDirectorBody==Body) return;
	if (auto* Old=PracticeDirectorBody.Get())
	{
		if(Old->IsManualControlEnabled())
		{
			if(!bDirectorCommandIssued)AssignMannequinCommand(Old,EMultiplayMannequinCommand::HoldPosition);
			StopMannequinAI(Old);Old->SetManualControlEnabled(false);
		}
	}
	// Number selection shares the view first. V/manual control starts after a reaction delay.
	PracticeDirectorBody=Body;bDirectorCommandIssued=false;NextDirectorMove=0;
	DirectorManualReadyAt=GetWorld()->GetTimeSeconds()+PracticeRandom.FRandRange(.35f,1.4f);
}

bool AProjectProject01TutorialGameMode::IssuePracticeDirectorCommand(EMultiplayMannequinCommand Command)
{
	auto* Body=PracticeDirectorBody.Get();
	if (!IsPracticeDirectorEnabled() || !Body) return false;
	if (Command==EMultiplayMannequinCommand::HoldPosition)
	{
		// No Z/X/C before changing bodies means hold, exactly like a real player.
		return !bDirectorCommandIssued; // Waiting does not erase a Z/X/C command already given in this control session.
	}
	AssignMannequinCommand(Body,Command);bDirectorCommandIssued=true;return true;
}

bool AProjectProject01TutorialGameMode::ResetPracticeDirectorFollowers()
{
	auto* Body=PracticeDirectorBody.Get();
	return IsPracticeDirectorEnabled() && Body && ResetFollowingMannequinCommands();
}

bool AProjectProject01TutorialGameMode::SetPracticeDirectorContactDamage(bool bEnabled)
{
	if (!IsPracticeDirectorEnabled() || !bPracticeDirectorToggleContact || bGlobalMannequinCatchDamageEnabled==bEnabled) return false;
	SetGlobalMannequinCatchDamage(bEnabled);
	ContactChangedUntil=GetWorld()->GetTimeSeconds()+4;
	NextDirectorContactToggle=GetWorld()->GetTimeSeconds()+PracticeRandom.FRandRange(9,22);
	return true;
}

bool AProjectProject01TutorialGameMode::IsPracticeContactDamageEnabled() const { return bGlobalMannequinCatchDamageEnabled; }
FText AProjectProject01TutorialGameMode::GetPracticeContactDamageText() const
{
	const TCHAR* State=bGlobalMannequinCatchDamageEnabled?TEXT("활성화"):TEXT("비활성화");
	const FString Head=GetWorld()->GetTimeSeconds()<ContactChangedUntil?
		FString::Printf(TEXT("접촉 피해가 %s되었습니다."),State):FString::Printf(TEXT("마네킹 접촉 피해: %s"),State);
	return FText::FromString(Head+
		(bPracticeDirectorToggleContact?TEXT("\nAI의 접촉 피해 전환: 허용"):TEXT("\nAI의 접촉 피해 전환: 금지")));
}

void AProjectProject01TutorialGameMode::UpdatePracticeDirectorPacing(float Now)
{
	if (Now<NextDirectorPacing) return;
	// A sparring partner creates pressure and openings, not an optimal kill loop.
	const uint8 Previous=DirectorBeat;const auto* Body=PracticeDirectorBody.Get();
	const APawn* Target=nullptr;if(Learner)Target=Learner->GetPawn();
	const bool bClose=Body && Target && FVector::Dist2D(Body->GetActorLocation(),Target->GetActorLocation())<450;
	const float Pick=PracticeRandom.FRand();
	DirectorBeat=Pick<(bClose?.2f:.3f)?0:Pick<.6f?1:Pick<.85f?2:3;
	if (DirectorBeat==Previous && PracticeRandom.FRand()<.6f) DirectorBeat=static_cast<uint8>(PracticeRandom.RandRange(0,3));
	NextDirectorPacing=Now+PracticeRandom.FRandRange(DirectorBeat==3?.65f:1.6f,DirectorBeat==3?1.6f:4.3f);
	DirectorStandOff=MultiplayerManualAttackRange+PracticeRandom.FRandRange(140,260);
	DirectorRepositionAngle=PracticeRandom.FRandRange(50,120)*(PracticeRandom.RandRange(0,1)?1.f:-1.f);
	DirectorRepositionRadius=PracticeRandom.FRandRange(320,580);
	NextDirectorMove=0;
	if(DirectorBeat==0 && Previous!=0)NextDirectorAttack=Now+PracticeRandom.FRandRange(.6f,1.2f);
}

void AProjectProject01TutorialGameMode::TickPracticeMannequinDirector(float Delta)
{
	if (!IsPracticeDirectorEnabled() || bPracticeMenuOpen || bPracticeRoundEnded || bReturning || !Learner) return;
	auto* Target=Cast<APlayerCharacter>(Learner->GetPawn());
	if (!IsValid(Target) || Target->IsGameOver()) return;
	const float Now=GetWorld()->GetTimeSeconds();
	UpdatePracticeDirectorPacing(Now);
	if (Now>=NextDirectorDecision)
	{
		NextDirectorDecision=Now+PracticeRandom.FRandRange(1.7f,5.3f);
		DirectorApproachOffset=FVector(PracticeRandom.FRandRange(-160,160),PracticeRandom.FRandRange(-160,160),0);
		AMannequinAICharacter* Chosen=nullptr;float TotalWeight=0;
		for (TActorIterator<AMannequinAICharacter> It(GetWorld());It;++It)
		{
			auto* AI=Cast<AAIController>(It->GetController());
			const float Distance=FVector::Dist2D(It->GetActorLocation(),Target->GetActorLocation());
			const bool bSeesTarget=AI && AI->LineOfSightTo(Target);
			// Prefer an unseen body with a real view of the survivor. Jitter breaks
			// ties without selecting by slot order or a predictable rotation.
			const float KnownDistance=bSeesTarget?Distance:1500.f;
			float Score=.15f+(bSeesTarget?2.f:.25f)*(It->IsFrozenBySurvivorVision()?.15f:1.f)*
				(1.f/(1.f+KnownDistance/600.f));
			if (*It==PracticeDirectorBody && bDirectorCommandIssued) Score*=.45f;
			TotalWeight+=Score;
			if (PracticeRandom.FRandRange(0,TotalWeight)<Score) Chosen=*It;
		}
		SelectPracticeDirectorBody(Chosen);
	}
	auto* Body=PracticeDirectorBody.Get();if (!Body) return;
	auto* AI=Cast<AAIController>(Body->GetController());if (!AI) return;
	if (!Body->IsManualControlEnabled() && Now>=DirectorManualReadyAt)
	{
		bDirectorCommandIssued=false;
		Body->SetManualControlEnabled(true); // AIController supplies physical movement, not a second human/network client.
	}
	const bool bSeesTarget=AI->LineOfSightTo(Target);
	if (bSeesTarget) { DirectorLastSeen=Target->GetActorLocation();DirectorLastSeenSeconds=Now; }
	if (Now>=NextDirectorAction)
	{
		NextDirectorAction=Now+PracticeRandom.FRandRange(1.3f,4.6f);
		int32 Followers=0;
		for(TActorIterator<AMannequinAICharacter> It(GetWorld());It;++It)
			if(It->GetMultiplayCommand()==EMultiplayMannequinCommand::FollowMannequinPlayer)++Followers;
		const float Roll=PracticeRandom.FRand();
		if (bPracticeDirectorToggleContact && Now>=NextDirectorContactToggle && Roll<.22f)
			SetPracticeDirectorContactDamage(!bGlobalMannequinCatchDamageEnabled);
		else if (Followers>0 && Roll<.36f) ResetPracticeDirectorFollowers();
		else
		{
			// View-only commands run immediately; manual orders survive handoff.
			// Fewer followers favours recruiting; visible prey favours range pursuit.
			const float Pick=PracticeRandom.FRand();
			const float FollowWeight=Followers<2?.35f:.15f;
			IssuePracticeDirectorCommand(Pick<FollowWeight?EMultiplayMannequinCommand::FollowMannequinPlayer:
				Pick<FollowWeight+(bSeesTarget?.35f:.2f)?EMultiplayMannequinCommand::ChaseInDetectionRange:
				Pick<.9f?EMultiplayMannequinCommand::Patrol:EMultiplayMannequinCommand::HoldPosition);
		}
	}
	if (!Body->IsManualControlEnabled()) return; // View sharing cannot directly move or attack.
	if (Body->IsFrozenBySurvivorVision()) { AI->StopMovement();NextDirectorMove=0;return; }
	if (DirectorBeat==3) { AI->StopMovement();return; }
	if (Now>=NextDirectorMove)
	{
		NextDirectorMove=Now+PracticeRandom.FRandRange(.3f,.9f);
		if (Now-DirectorLastSeenSeconds<4.f)
		{
			// Slightly different approach points, but complete navigable paths only.
			FVector Goal=DirectorLastSeen;
			const FVector Away=(Body->GetActorLocation()-Goal).GetSafeNormal2D();
			if(DirectorBeat==1)Goal+=Away*DirectorStandOff;
			else if(DirectorBeat==2)Goal+=Away.RotateAngleAxis(DirectorRepositionAngle,FVector::UpVector)*DirectorRepositionRadius;
			else if (FVector::Dist2D(Body->GetActorLocation(),Goal)>350)
				Goal+=DirectorApproachOffset;
			auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Body->GetActorLocation(),Goal,Body);
			if (Path && Path->IsValid() && !Path->IsPartial()) AI->MoveToLocation(Goal,DirectorBeat==0?FMath::Max(100.f,MultiplayerManualAttackRange*.75f):40.f,false,true,false,true,nullptr,false);
			else AI->StopMovement(); // Do not keep an old pressure route when a sparring reposition is unreachable.
		}
		else if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation Goal;
			if (AI->GetMoveStatus()!=EPathFollowingStatus::Moving && Nav->GetRandomReachablePointInRadius(Body->GetActorLocation(),900,Goal))
				AI->MoveToLocation(Goal.Location,40,false,true,false,true,nullptr,false);
		}
	}
	const FVector Heading=Body->GetVelocity().GetSafeNormal2D();
	if (!Heading.IsNearlyZero()) Body->SetActorRotation(FMath::RInterpConstantTo(Body->GetActorRotation(),Heading.Rotation(),Delta,180));
	if (DirectorBeat==0 && Now>=NextDirectorAttack)
	{
		NextDirectorAttack=Now+PracticeRandom.FRandRange(.35f,1.15f);
		const FVector Offset=Target->GetActorLocation()-Body->GetActorLocation();
		if (bSeesTarget && MultiplayerManualAttackRange>0 && Offset.SizeSquared()<=FMath::Square(MultiplayerManualAttackRange) &&
			FVector::DotProduct(Body->GetActorForwardVector(),Offset.GetSafeNormal())>=.25f)
		{
			if(Target->HandleMannequinCatch(Body))
			{
				DirectorBeat=3;NextDirectorPacing=Now+PracticeRandom.FRandRange(.9f,1.8f);AI->StopMovement();
			}
		}
	}
}

bool AProjectProject01TutorialGameMode::FindPracticeCornerLocation(const FVector& Anchor,float Radius,float HalfHeight,const TArray<FVector>& Reserved,FVector& OutLocation,FRotator& OutRotation) const
{
	auto* World=GetWorld();auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!World || !Nav) return false;
	FCollisionQueryParams Walls(SCENE_QUERY_STAT(PracticeCornerWalls),true);
	for (TActorIterator<APawn> It(World);It;++It) Walls.AddIgnoredActor(*It);
	const FVector Origin(Anchor.X,Anchor.Y,Anchor.Z);
	const float Clearance=Radius+85,Reach=FMath::Max(5000.f,Plan->PracticeTargetRoamRadius*6);
	TArray<FVector> Candidates;TArray<FRotator> Rotations;
	for (int32 Ray=0;Ray<24;++Ray)
	{
		FHitResult First;const FVector Direction=FRotator(0,Ray*15,0).Vector();
		if (!World->LineTraceSingleByChannel(First,Origin,Origin+Direction*Reach,ECC_Visibility,Walls) || FMath::Abs(First.ImpactNormal.Z)>.3f) continue;
		const FVector NearWall=First.ImpactPoint+First.ImpactNormal*Clearance;
		const FVector Tangent=FVector::CrossProduct(First.ImpactNormal,FVector::UpVector).GetSafeNormal();
		for (const float Sign:{-1.f,1.f})
		{
			FHitResult Second;
			if (!World->LineTraceSingleByChannel(Second,NearWall,NearWall+Tangent*Sign*Reach,ECC_Visibility,Walls) ||
				FMath::Abs(Second.ImpactNormal.Z)>.3f || FMath::Abs(FVector::DotProduct(First.ImpactNormal,Second.ImpactNormal))>.35f) continue;
			FNavLocation Floor;
			if (!Nav->ProjectPointToNavigation(Second.ImpactPoint+Second.ImpactNormal*Clearance,Floor,FVector(70,70,300))) continue;
			const FVector Candidate=Floor.Location+FVector(0,0,HalfHeight+2);
			if (Reserved.ContainsByPredicate([&](const FVector& Other){return FVector::DistSquared2D(Candidate,Other)<FMath::Square(400.f);})) continue;
			FCollisionQueryParams Capsule(SCENE_QUERY_STAT(PracticeCornerCapsule),false);
			if (World->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,HalfHeight),Capsule)) continue;
			auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Anchor,Floor.Location);
			if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
			if (Candidates.ContainsByPredicate([&](const FVector& Other){return FVector::DistSquared2D(Candidate,Other)<FMath::Square(200.f);})) continue;
			Candidates.Add(Candidate);Rotations.Add((First.ImpactNormal+Second.ImpactNormal).Rotation());
		}
	}
	if (Candidates.IsEmpty()) return false;
	const int32 Pick=FMath::RandRange(0,Candidates.Num()-1);OutLocation=Candidates[Pick];OutRotation=Rotations[Pick];return true;
}

void AProjectProject01TutorialGameMode::SpawnPracticeSurvivor()
{
	SpawnPracticeSurvivors(1);
}
void AProjectProject01TutorialGameMode::SpawnPracticeSurvivors(int32 Count)
{
	if (!Learner || !Plan) return;
	for (auto& Bot:PracticeBots)
	{
		MatchPlayerRecords.Remove(Bot.Controller);
		if (IsValid(Bot.Pawn)) Bot.Pawn->Destroy();
		if (IsValid(Bot.Controller)) Bot.Controller->Destroy();
	}
	PracticeBots.Reset();PracticeRandom.GenerateNewSeed();PracticeCornersPlaced=0;
	for (int32 Index=0;Index<FMath::Clamp(Count,1,2);++Index)
	{
	auto* PC=GetWorld()->SpawnActor<AMultiplayTestPlayerController>();
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	FVector Location=Plan->GetActorTransform().TransformPosition(Plan->PracticeTargetOffset+FVector(0,Index*320,0));
	FRotator SpawnRotation(0,90,0);
	const auto* Template=DefaultPawnClass ? Cast<ACharacter>(DefaultPawnClass->GetDefaultObject()) : nullptr;
	if (!Template) Template=GetDefault<AProjectTutorialTargetCharacter>();
	const float HalfHeight=Template->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Floor;
		if (Nav->ProjectPointToNavigation(Location,Floor,FVector(200,200,300)))
			Location=Floor.Location+FVector(0,0,HalfHeight+2);
	}
	auto* Pawn=GetWorld()->SpawnActor<AProjectTutorialTargetCharacter>(AProjectTutorialTargetCharacter::StaticClass(),Location,SpawnRotation,Params);
	if (!PC || !Pawn) { if (PC) PC->Destroy(); if (Pawn) Pawn->Destroy(); continue; }
	// Reuse the configured survivor appearance, while keeping the bot's native
	// movement component and never inheriting a Blueprint's human input scripts.
	if (Template)
	{
		auto* Mesh=Pawn->GetMesh(); auto* Source=Template->GetMesh();
		Mesh->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset()); Mesh->SetAnimInstanceClass(Source->GetAnimClass());
		Mesh->SetRelativeTransform(Source->GetRelativeTransform());
		for (int32 I=0;I<Source->GetNumMaterials();++I) Mesh->SetMaterial(I,Source->GetMaterial(I));
		Pawn->GetCapsuleComponent()->SetCapsuleSize(Template->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),Template->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
	}
	auto& Bot=PracticeBots.AddDefaulted_GetRef();Bot.Pawn=Pawn;Bot.Controller=PC;Bot.LastLocation=Pawn->GetActorLocation();
	Pawn->GetCharacterMovement()->MaxWalkSpeed=FMath::Clamp(Plan->PracticeTargetSpeed,50.f,500.f);
	PC->Possess(Pawn);
	// Standalone possession does not normally create the remote-player movement
	// branch. This bot supplies its moves locally through the dedicated component.
	Pawn->SetAutonomousProxy(true);
	PC->SetControlRotation(SpawnRotation);SetPlayerDefaults(Pawn); // Same death-count initialization as a real multiplayer survivor.
	FProjectProject01ValidatedJoinClaim Claim; Claim.UserId=FString::Printf(TEXT("Tutorial.Target.%d"),Index); Claim.MatchId=TEXT("Tutorial.Local"); Claim.Role=TEXT("Survivor"); Claim.DisplayName=FString::Printf(TEXT("연습용 생존자 %d"),Index+1); PC->SetAuthenticatedLobbyIdentity(Claim);
	RegisterMatchPlayer(PC); PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
	}
	// Only the learner's directly controlled body can deal damage in this practice.
	bGlobalMannequinCatchDamageEnabled=false;
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It) It->SetCatchDamageEnabled(false);
}
AMannequinAICharacter* AProjectProject01TutorialGameMode::FindPracticeThreat(const FProjectTutorialSurvivorBot& Bot) const
{
	AMannequinAICharacter* Result=nullptr;double Nearest=TNumericLimits<double>::Max();
	if (!Bot.Pawn) return nullptr;
	for (TActorIterator<AMannequinAICharacter> It(GetWorld());It;++It)
	{
		const double Distance=FVector::DistSquared2D(Bot.Pawn->GetActorLocation(),It->GetActorLocation());
		if (!It->IsHidden() && Distance<Nearest) { Nearest=Distance;Result=*It; }
	}
	return Result;
}
bool AProjectProject01TutorialGameMode::IsPracticeTargetSegmentClear(const FProjectTutorialSurvivorBot& Bot,const FVector& From,const FVector& To) const
{
	if (!Bot.Pawn || !GetWorld()) return false;
	const auto* Capsule=Bot.Pawn->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TutorialSurvivorRoute),false,Bot.Pawn);
	// Keep the sweep above the floor while retaining the body's horizontal width.
	const float Radius=Capsule->GetScaledCapsuleRadius();
	const float HalfHeight=FMath::Max(Radius,Capsule->GetScaledCapsuleHalfHeight()-8);
	FHitResult Hit;
	return !GetWorld()->SweepSingleByChannel(Hit,From,To,FQuat::Identity,ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius,HalfHeight),Params);
}
void AProjectProject01TutorialGameMode::UpdatePracticeTargetPath(FProjectTutorialSurvivorBot& Bot,int32 Index)
{
	if (!IsValid(Bot.Pawn) || !Plan) return;
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); if (!Nav) return;
	const FVector Start=Bot.Pawn->GetActorLocation();
	auto* Threat=FindPracticeThreat(Bot);
	const FProjectTutorialSurvivorBot* Buddy=nullptr;
	for (const auto& Other:PracticeBots) if (Other.Pawn!=Bot.Pawn && IsValid(Other.Pawn) && !Other.Pawn->IsGameOver()) { Buddy=&Other;break; }
	const int32 Phase=PracticeTacticalPhase;
	for (int32 Attempt=0;Attempt<20;++Attempt)
	{
		FNavLocation Goal;
		if (Buddy && Phase>0 && Attempt<6 && FVector::Dist2D(Start,Buddy->Pawn->GetActorLocation())>450)
		{
			const FVector Side=FVector(0,Index==0 ? -260.f : 260.f,0);
			if (!Nav->ProjectPointToNavigation(Buddy->Pawn->GetActorLocation()+Side,Goal,FVector(200,200,250))) continue;
		}
		else if (Attempt<6 && Bot.AvoidDirection.IsNearlyZero() && Threat && FVector::Dist2D(Start,Threat->GetActorLocation())<1200)
		{
			const FVector Away=(Start-Threat->GetActorLocation()).GetSafeNormal2D();
			const FVector Side=FVector::CrossProduct(Away,FVector::UpVector);
			const float Split=Buddy && Phase==0 ? (Index==0 ? -450.f : 450.f) : PracticeRandom.FRandRange(-650,650);
			const FVector Candidate=Start+Away*PracticeRandom.FRandRange(300,650)+Side*Split;
			if (!Nav->ProjectPointToNavigation(Candidate,Goal,FVector(180,180,250))) continue;
		}
		else if (!Nav->GetRandomReachablePointInRadius(Start,FMath::Max(200.f,Plan->PracticeTargetRoamRadius),Goal)) continue;
		if (FVector::DistSquared2D(Start,Goal.Location)<FMath::Square(150.f)) continue;
		if (Buddy && Phase==0 && Attempt<12 && FVector::Dist2D(Goal.Location,Buddy->Pawn->GetActorLocation())<400) continue;
		if (!Bot.AvoidDirection.IsNearlyZero() && FVector::DotProduct((Goal.Location-Start).GetSafeNormal2D(),Bot.AvoidDirection)>.65f) continue;
		auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Start,Goal.Location,Bot.Pawn);
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2) continue;
		// NavMesh does not necessarily contain live pawn obstacles. Reject paths
		// whose first segment points into another mannequin or a physical blocker.
		const FVector First=Path->PathPoints[1]+FVector(0,0,Start.Z-Path->PathPoints[0].Z);
		if (!IsPracticeTargetSegmentClear(Bot,Start,First)) continue;
		Bot.Path=Path->PathPoints; Bot.PathIndex=1; return;
	}
	Bot.Path.Reset(); // Never walk through a wall as a fallback.
}
void AProjectProject01TutorialGameMode::TickPracticeSurvivor(float Delta)
{
	const auto* S=GetCurrentStep();
	if (bAwaitingRead || bFinishing || !bMannequin || (!bFreePractice && (!S || S->Condition!=EProjectTutorialCondition::Capture))) return;
	if (!bFreePractice) PracticeTacticalSeconds+=Delta;
	UpdatePracticeTactics();
	for (int32 Index=0;Index<PracticeBots.Num();++Index)
	{
		auto& Bot=PracticeBots[Index];auto* Pawn=Bot.Pawn.Get();auto* PC=Bot.Controller.Get();
		if (!IsValid(Pawn) || !PC || Pawn->IsGameOver()) continue;
		if (bFreePractice && bPracticeStationary)
		{
			Bot.bCovering=false;Bot.bRequestedSprintLastTick=false;Pawn->SetPracticeSprint(false);
			Pawn->GetCharacterMovement()->StopMovementImmediately();Pawn->ConsumeMovementInputVector();
			Bot.LookSeconds+=FMath::Min(Delta,.1f);
			if (bPracticeStandingWatch)
			{
				// The real survivor sight direction changes too: mannequins in that
				// view stop normally. This is not just an animation on the mesh.
				if (!Bot.bWatchTargetChosen)
				{
					const float CurrentYaw=PC->GetControlRotation().Yaw;
					// Choose a fresh signed turn, not a fixed left/right arc around spawn.
					const float Turn=PracticeRandom.FRandRange(35,155)*(PracticeRandom.RandRange(0,1)?1.f:-1.f);
					Bot.WatchTargetYaw=FRotator::NormalizeAxis(CurrentYaw+Turn);
					Bot.WatchTurnSpeed=PracticeRandom.FRandRange(40,100);Bot.bWatchTargetChosen=true;
				}
				if (Bot.WatchHoldSeconds>0)
				{
					Bot.WatchHoldSeconds=FMath::Max(0.f,Bot.WatchHoldSeconds-Delta);
					if (Bot.WatchHoldSeconds<=0) Bot.bWatchTargetChosen=false;
				}
				else
				{
					const FRotator View=FMath::RInterpConstantTo(PC->GetControlRotation(),FRotator(0,Bot.WatchTargetYaw,0),Delta,Bot.WatchTurnSpeed);
					PC->SetControlRotation(View);Pawn->SetActorRotation(View);
					if (FMath::Abs(FMath::FindDeltaAngleDegrees(View.Yaw,Bot.WatchTargetYaw))<1) Bot.WatchHoldSeconds=PracticeRandom.FRandRange(.4f,1.8f);
				}
			}
			continue;
		}
		const FProjectTutorialSurvivorBot* Buddy=nullptr;
		for (const auto& Other:PracticeBots) if (Other.Pawn!=Pawn && IsValid(Other.Pawn) && !Other.Pawn->IsGameOver()) { Buddy=&Other;break; }
		const int32 Phase=PracticeTacticalPhase;
		const auto* CurrentThreat=FindPracticeThreat(Bot);
		const float ThreatDistance=CurrentThreat?FVector::Dist2D(Pawn->GetActorLocation(),CurrentThreat->GetActorLocation()):100000;
		if(bFreePractice && PracticeTacticalSeconds>=Bot.NextPacingDecision)
		{
			Bot.NextPacingDecision=PracticeTacticalSeconds+PracticeRandom.FRandRange(2.8f,7.5f);
			if(ThreatDistance>550 && PracticeRandom.FRand()<.28f)Bot.PauseUntil=PracticeTacticalSeconds+PracticeRandom.FRandRange(.35f,1.1f);
		}
		const bool bHesitating=bFreePractice && PracticeTacticalSeconds<Bot.PauseUntil && ThreatDistance>300;
		const bool bRegroupHold=Buddy && Phase==1 && FVector::Dist2D(Pawn->GetActorLocation(),Buddy->Pawn->GetActorLocation())<500;
		Bot.bCovering=Buddy && Phase==2 && Index==PracticeCoverIndex &&
			FVector::Dist2D(Pawn->GetActorLocation(),Buddy->Pawn->GetActorLocation())<900 && PC->LineOfSightTo(Buddy->Pawn);
		Bot.RepathSeconds-=Delta;Bot.LookSeconds+=FMath::Min(Delta,.1f);Bot.StuckSeconds+=Delta;
		if (Bot.StuckSeconds>=.8f)
		{
			if (!Bot.bCovering && !bRegroupHold && !bHesitating && Bot.Path.IsValidIndex(Bot.PathIndex) && FVector::DistSquared2D(Bot.LastLocation,Pawn->GetActorLocation())<FMath::Square(20.f))
			{ Bot.AvoidDirection=(Bot.Path[Bot.PathIndex]-Pawn->GetActorLocation()).GetSafeNormal2D();Bot.Path.Reset();Bot.RepathSeconds=0; }
			else Bot.AvoidDirection=FVector::ZeroVector;
			Bot.StuckSeconds=0;Bot.LastLocation=Pawn->GetActorLocation();
		}
		if (Bot.RepathSeconds<=0 && !Bot.bCovering)
		{ UpdatePracticeTargetPath(Bot,Index);Bot.RepathSeconds=Bot.Path.IsEmpty() ? .35f : PracticeRandom.FRandRange(1.3f,2.2f); }
		FVector Direction=FVector::ZeroVector;
		if (!Bot.bCovering && !bRegroupHold && !bHesitating && Bot.Path.IsValidIndex(Bot.PathIndex))
		{
			if (FVector::DistSquared2D(Pawn->GetActorLocation(),Bot.Path[Bot.PathIndex])<FMath::Square(70.f))
			{ ++Bot.PathIndex;if (!Bot.Path.IsValidIndex(Bot.PathIndex)) Bot.RepathSeconds=0; }
			if (Bot.Path.IsValidIndex(Bot.PathIndex)) Direction=(Bot.Path[Bot.PathIndex]-Pawn->GetActorLocation()).GetSafeNormal2D();
		}
		if (!Direction.IsNearlyZero() && !IsPracticeTargetSegmentClear(Bot,Pawn->GetActorLocation(),Pawn->GetActorLocation()+Direction*100))
		{ Bot.AvoidDirection=Direction;Bot.Path.Reset();Bot.RepathSeconds=0;Direction=FVector::ZeroVector; }
		const auto* Threat=FindPracticeThreat(Bot);
		if (Pawn->GetCurrentStamina()<=.1f || (Bot.bRequestedSprintLastTick && !Pawn->IsPracticeSprinting())) Bot.bRecoveringStamina=true;
		else if (Pawn->GetCurrentStamina()>=Pawn->GetMaxStamina()*.35f) Bot.bRecoveringStamina=false;
		const bool bThreatNear=Threat && FVector::Dist2D(Pawn->GetActorLocation(),Threat->GetActorLocation())<1200;
		if (PracticeTacticalSeconds>=Bot.NextSprintDecision)
		{
			Bot.bWantsSprint=PracticeRandom.FRand()<(bThreatNear?.85f:Buddy?.5f:.25f);
			Bot.NextSprintDecision=PracticeTacticalSeconds+PracticeRandom.FRandRange(.8f,Bot.bWantsSprint?3.6f:2.8f);
		}
		const bool bSprint=!Direction.IsNearlyZero() && !Bot.bCovering && !Bot.bRecoveringStamina &&
			Bot.bWantsSprint;
		Pawn->SetPracticeSprint(bSprint);
		const bool bActualSprint=Pawn->IsPracticeSprinting();
		Bot.bRequestedSprintLastTick=bSprint && bActualSprint;
		if (bSprint && !bActualSprint) Bot.bRecoveringStamina=true;
		Pawn->GetCharacterMovement()->MaxWalkSpeed=bActualSprint ? FMath::Max(Plan->PracticeTargetSpeed,Plan->PracticeTargetSprintSpeed) : Plan->PracticeTargetSpeed;
		if (Bot.bCovering || bRegroupHold || bHesitating) Pawn->GetCharacterMovement()->StopMovementImmediately();
		else Pawn->AddMovementInput(Direction);
		const float Watch=FMath::Max(.5f,Plan->PracticeTargetWatchSeconds),Away=FMath::Max(1.f,Plan->PracticeTargetLookAwaySeconds);
		if (PracticeTacticalSeconds>=Bot.NextWatchDecision)
		{
			Bot.bWantsWatch=PracticeRandom.FRand()<(bThreatNear?.7f:.4f);
			Bot.WatchTurnSpeed=PracticeRandom.FRandRange(95,190);Bot.WatchYawOffset=PracticeRandom.FRandRange(-15,15);
			Bot.NextWatchDecision=PracticeTacticalSeconds+(Bot.bWantsWatch?Watch:Away)*PracticeRandom.FRandRange(.55f,1.65f);
		}
		const bool bWatch=Bot.bCovering || Bot.bWantsWatch;
		FVector Look=Direction;
		if (bWatch && Threat && PC->LineOfSightTo(Threat)) Look=(Threat->GetActorLocation()-Pawn->GetActorLocation()).GetSafeNormal2D().RotateAngleAxis(Bot.WatchYawOffset,FVector::UpVector);
		else if (Direction.IsNearlyZero() && Threat) Look=(Pawn->GetActorLocation()-Threat->GetActorLocation()).GetSafeNormal2D();
		if (!Look.IsNearlyZero())
		{
			const FRotator Rotation=FMath::RInterpConstantTo(PC->GetControlRotation(),Look.Rotation(),Delta,Bot.WatchTurnSpeed);
			PC->SetControlRotation(Rotation);Pawn->SetActorRotation(Rotation);
		}
	}
}
void AProjectProject01TutorialGameMode::FinishAuthoritativeMatch(const FString& Reason)
{
	if (bFreePractice) { if (bInitialized && !bReturning) FinishFreePractice();return; }
	// Never call the normal results/leaderboard/backend path for local practice.
	if (!bInitialized || bFinishing || Reason!=TEXT("AllSurvivorsEliminated")) return;
	const auto* S=GetCurrentStep();
	if (bMannequin && S && S->Condition==EProjectTutorialCondition::Capture) RecordAction(Learner,EProjectTutorialCondition::Capture);
}
void AProjectProject01TutorialGameMode::FinishTutorial()
{
	if (bFinishing) return;
	bFinishing=true; EndSeconds=0; SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending); StopAllMannequinsForMatchEnd();
	if (auto* C=Learner->FindComponentByClass<UProjectProject01ObjectiveComponent>()) C->CompleteObjective();
	if (auto* Pawn=Cast<ACharacter>(Learner->GetPawn())) Pawn->GetCharacterMovement()->StopMovementImmediately();
	bAwaitingRead=true; SetMenuInput(true);
	if (auto* Settings=UProjectProject01GameUserSettings::Get()) Settings->FadeGameplaySFX(0,1.5f);
}
void AProjectProject01TutorialGameMode::FinishFreePractice()
{
	if (bPracticeRoundEnded) return;
	bPracticeRoundEnded=true;PracticeEndSeconds=0;
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending);StopAllMannequinsForMatchEnd();
	for (auto& Bot:PracticeBots) if (Bot.Pawn) { Bot.Pawn->SetPracticeSprint(false);Bot.Pawn->GetCharacterMovement()->StopMovementImmediately(); }
	if (auto* Settings=UProjectProject01GameUserSettings::Get()) Settings->FadeGameplaySFX(0,1.5f);
}
void AProjectProject01TutorialGameMode::ShowPracticeMenu(bool bShow)
{
	if (!bFreePractice || bReturning) return;
	if (bPracticeRoundEnded) bShow=true;
	bPracticeMenuOpen=bShow;SetMenuInput(bShow);
	if (bShow && bPracticeRoundEnded) if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->ClearEntireScreenFade();
	UGameplayStatics::SetGamePaused(this,bShow);
}
FText AProjectProject01TutorialGameMode::GetFreePracticeStatus() const
{
	FString Result=bMannequin ? TEXT("마네킹 연습모드") : TEXT("생존자 연습모드");
	Result+=FString::Printf(TEXT("\n마네킹 %d개"),PracticeMannequins);
	Result+=HasPracticeInfiniteLives()?TEXT("\n무한 목숨: 켜짐"):TEXT("\n목숨: 멀티플레이 규칙");
	if (bPracticeRoundEnded) Result+=TEXT("\n연습이 끝났습니다. 다시 연습하거나 역할을 전환하세요.");
	if (bMannequin)
	{
		int32 Active=0;for (const auto& Bot:PracticeBots) if (IsValid(Bot.Pawn) && !Bot.Pawn->IsGameOver()) ++Active;
		Result+=FString::Printf(TEXT("\n생존자 AI %d / %d명 남음"),Active,PracticeSurvivors);
		if (bPracticeStationary) Result+=bPracticeStandingWatch?TEXT("\n제자리 대기 · 주변 경계"):TEXT("\n제자리 대기 · 시야 고정");
		else if (PracticeBots.Num()>1 && Active>1)
		{
			const TCHAR* Phases[]={TEXT("분산 이동"),TEXT("재집결"),TEXT("교대 엄호")};
			Result+=FString::Printf(TEXT("\n협력 행동: %s"),Phases[PracticeTacticalPhase]);
		}
	}
	else Result+=bPracticeDirector?TEXT("\n마네킹 플레이어 AI: 켜짐"):TEXT("\n마네킹 플레이어 AI: 꺼짐");
	if (bMannequin && bPracticeCorners) Result+=FString::Printf(TEXT("\n코너 배치: %d / %d명%s"),PracticeCornersPlaced,PracticeCornerCount,PracticeCornersPlaced<PracticeCornerCount?TEXT(" (나머지는 기본 위치)"):TEXT(""));
	return FText::FromString(Result);
}
void AProjectProject01TutorialGameMode::RestartFreePractice(bool bAsMannequin,int32 Mannequins,int32 Survivors,bool bStationary,bool bStandingWatch,bool bInfiniteLives,bool bCorners,int32 CornerCount,bool bDirector,bool bAllowContactToggle)
{
	if (!bFreePractice || bReturning) return;
	bReturning=true;UGameplayStatics::SetGamePaused(this,false);SetMenuInput(true);
	SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending);StopAllMannequinsForMatchEnd();
	const FString Options=FString::Printf(TEXT("FreePractice=1?TutorialRole=%s?PracticeMannequins=%d?PracticeSurvivors=%d?PracticeStationary=%d?PracticeStandingWatch=%d?PracticeInfiniteLives=%d?PracticeCorners=%d?PracticeCornerCount=%d?PracticeDirector=%d?PracticeDirectorToggleContact=%d"),
		bAsMannequin ? TEXT("Mannequin") : TEXT("Survivor"),FMath::Clamp(Mannequins,1,10),FMath::Clamp(Survivors,1,2),bStationary?1:0,bStandingWatch?1:0,bInfiniteLives?1:0,bCorners?1:0,FMath::Clamp(CornerCount,1,FMath::Clamp(Survivors,1,2)),bDirector?1:0,bAllowContactToggle?1:0);
	if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->FadeEntireScreenToBlack(.6f);
	GetWorldTimerManager().SetTimer(ReturnTimer,FTimerDelegate::CreateWeakLambda(this,[this,Options]()
	{ UGameplayStatics::OpenLevel(this,ProjectTutorial::Map,true,Options); }),.65f,false);
}
void AProjectProject01TutorialGameMode::ReturnToMatching()
{
	if (bReturning) return; bReturning=true;
	if (bFreePractice)
	{
		UGameplayStatics::SetGamePaused(this,false);SetMenuInput(true);
		SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::Ending);StopAllMannequinsForMatchEnd();
	}
	if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->FadeEntireScreenToBlack(0.6f);
	GetWorldTimerManager().SetTimer(ReturnTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
	{
		auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();
		const TWeakObjectPtr<UProjectProject01GameInstance> WeakInstance(GetGameInstance<UProjectProject01GameInstance>());
		const TWeakObjectPtr<UWorld> OldWorld(GetWorld());
		const TWeakObjectPtr<AProjectProject01TutorialGameMode> WeakMode(this);
		const double Deadline=FPlatformTime::Seconds()+30;
		// The old world's timers die during OpenLevel. Use the instance's new ready
		// world to reveal the front end, not a fade started before the actual load.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakInstance,OldWorld,WeakMode,Deadline](float)
		{
			auto* Instance=WeakInstance.Get(); if (!Instance) return false;
			auto* ReadyWorld=Instance->GetWorld();
			if (IsValid(ReadyWorld) && ReadyWorld!=OldWorld.Get() && ReadyWorld->HasBegunPlay())
			{
				Instance->FadeEntireScreenFromBlack(0.6f);
				UE_LOG(LogTemp,Log,TEXT("[Tutorial] Front end ready; revealing %s."),*ReadyWorld->GetMapName());
				return false;
			}
			if (FPlatformTime::Seconds()>Deadline)
			{
				Instance->FadeEntireScreenFromBlack(0.6f);
				if (auto* Previous=WeakMode.Get()) { Previous->bReturning=false; Previous->SetMenuInput(true); }
				return false;
			}
			return true;
		}),0.05f);
		UGameplayStatics::OpenLevel(this,FName(Auth && Auth->IsSignedIn() ? TEXT("/Game/MyProject/Level/LobbyLevel") : TEXT("/Game/MyProject/Level/LoginLevel")),true);
	}),0.65f,false);
}
void AProjectProject01TutorialGameMode::RestartTutorial(const bool bOnlyPractice)
{
	if (bReturning) return; bReturning=true;
	if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->FadeEntireScreenToBlack(0.6f);
	GetWorldTimerManager().SetTimer(ReturnTimer,FTimerDelegate::CreateWeakLambda(this,[this,bOnlyPractice]()
	{
		FString Options=bMannequin ? TEXT("TutorialRole=Mannequin") : TEXT("TutorialRole=Survivor");
		if (bOnlyPractice) Options+=TEXT("?TutorialPracticeOnly=1");
		UGameplayStatics::OpenLevel(this,ProjectTutorial::Map,true,Options);
	}),0.65f,false);
}

bool UProjectProject01TutorialWidget::Initialize()
{
	if (!Super::Initialize()) return false;
	SetIsFocusable(true);
	auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Canvas;
	ScreenButton=WidgetTree->ConstructWidget<UButton>();
	ScreenButton->SetBackgroundColor(FLinearColor(0,0,0,0.55f));
	auto* BackSlot=Canvas->AddChildToCanvas(ScreenButton); BackSlot->SetAnchors(FAnchors(0,0,1,1)); BackSlot->SetOffsets(FMargin(0));
	Panel=WidgetTree->ConstructWidget<UBorder>(); Panel->SetPadding(FMargin(24)); Panel->SetBrushColor(ProjectDFSDUI::Panel().CopyWithNewOpacity(.96f));
	auto* CanvasSlot=Canvas->AddChildToCanvas(Panel); CanvasSlot->SetAnchors(FAnchors(0.5f,0.5f)); CanvasSlot->SetAlignment(FVector2D(0.5f,0.5f)); CanvasSlot->SetAutoSize(true);
	auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(); Bounds->SetWidthOverride(660); Bounds->SetMaxDesiredHeight(620); Panel->AddChild(Bounds);
	auto* Root=WidgetTree->ConstructWidget<UVerticalBox>(); Bounds->AddChild(Root);
	Heading=ProjectTutorial::Label(WidgetTree,Root,26);
	// Only the explanation scrolls; confirmation/return controls never move out of view.
	auto* BodyBounds=WidgetTree->ConstructWidget<USizeBox>();BodyBounds->SetMaxDesiredHeight(420);Root->AddChildToVerticalBox(BodyBounds);
	auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("TutorialInstructionsScroll"));BodyBounds->AddChild(Scroll);
	auto* Paragraph=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Paragraph);
	Body=ProjectTutorial::Label(WidgetTree,Paragraph,19);Body->SetLineHeightPercentage(1.1f);
	auto* ObjectiveWidth=WidgetTree->ConstructWidget<USizeBox>();ObjectiveWidth->SetWidthOverride(560);
	auto* ObjectiveSlot=Canvas->AddChildToCanvas(ObjectiveWidth); ObjectiveSlot->SetPosition(FVector2D(28,260)); ObjectiveSlot->SetAutoSize(true);
	ObjectiveStack=WidgetTree->ConstructWidget<UVerticalBox>();
	ObjectiveWidth->AddChild(ObjectiveStack);
	PracticeStatus=WidgetTree->ConstructWidget<UTextBlock>(); PracticeStatus->SetColorAndOpacity(ProjectDFSDUI::Text()); PracticeStatus->SetAutoWrapText(true);
	PracticeStatus->SetWrapTextAt(560);
	auto StatusFont=PracticeStatus->GetFont();StatusFont.Size=17;PracticeStatus->SetFont(StatusFont);PracticeStatus->SetShadowOffset(FVector2D(1,1));PracticeStatus->SetShadowColorAndOpacity(FLinearColor::Black);
	ObjectiveStack->AddChildToVerticalBox(PracticeStatus);
	ContinueButton=ProjectTutorial::Button(WidgetTree,Root,TEXT("확인"));
	ContinueButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleContinue);
	RestartButton=ProjectTutorial::Button(WidgetTree,Root,TEXT("실전 연습")); RestartButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleRestart);
	FullRestartButton=ProjectTutorial::Button(WidgetTree,Root,TEXT("튜토리얼 처음부터 다시하기")); FullRestartButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleRestartFromBeginning);
	auto* Return=ProjectTutorial::Button(WidgetTree,Root,TEXT("로비로 돌아가기")); Return->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleReturn);
	return true;
}
void UProjectProject01TutorialWidget::NativeTick(const FGeometry& G,float Delta)
{
	Super::NativeTick(G,Delta);
	auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>(); if (!Mode) return;
	const auto* Step=Mode->GetCurrentStep(); const bool Result=Mode->IsResultVisible(), Finish=Mode->IsFinishing();
	Heading->SetText(ProjectTutorial::Text(Result ? TEXT("튜토리얼 완료") : Mode->IsMannequinTutorial() ? TEXT("마네킹 튜토리얼") : TEXT("생존자 튜토리얼")));
	const FText PreviousBody=Body->GetText();
	Body->SetText(Finish ? Mode->GetClosingText() : !Mode->GetExtraMessage().IsEmpty() ? Mode->GetExtraMessage() : Step ?
		(Step->Instructions.IsEmpty() ? ProjectTutorial::ResolveText(Step->Title) : FText::Format(FText::FromString(TEXT("{0}\n\n{1}")),ProjectTutorial::ResolveText(Step->Title),ProjectTutorial::ResolveText(Step->Instructions))) : FText::GetEmpty());
	if (Result) Body->SetText(FText::Format(FText::FromString(TEXT("{0}\n\n{1}")),
		ProjectTutorial::Text(Mode->IsMannequinTutorial() ? TEXT("연습 결과: 생존자 1명 붙잡기 성공") : TEXT("연습 결과: 무사히 탈출했습니다.")),Mode->GetClosingText()));
	if (!PreviousBody.EqualTo(Body->GetText()))
		if (auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("TutorialInstructionsScroll")))) Scroll->ScrollToStart();
	ContinueButton->SetVisibility(!Result ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	RestartButton->SetVisibility(Result ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	FullRestartButton->SetVisibility(Result ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const bool Show=Mode->IsAwaitingRead() || Finish;
	const auto* Controller=Mode->GetLearnerController();
	const float Top=Mode->IsMannequinTutorial() && Controller ? Controller->GetMannequinStatusBottom()+16.f : 180.f;
	if (auto* ObjectiveSlot=Cast<UCanvasPanelSlot>(ObjectiveStack->GetParent()->Slot)) ObjectiveSlot->SetPosition(FVector2D(28,Top));
	PracticeStatus->SetText(Mode->GetPracticeStatusText());ObjectiveStack->SetVisibility(!Show ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	Panel->SetVisibility(Show ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	ScreenButton->SetVisibility(Show ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
FReply UProjectProject01TutorialWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& Event)
{
	if (auto* M=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>();
		M && !M->IsResultVisible() && (M->IsAwaitingRead() || M->IsFinishing())) return FReply::Handled();
	return Super::NativeOnPreviewKeyDown(G,Event);
}
FReply UProjectProject01TutorialWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& Event)
{
	if (auto* M=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>();
		M && !M->IsResultVisible() && (M->IsAwaitingRead() || M->IsFinishing())) return FReply::Handled();
	return Super::NativeOnKeyDown(G,Event);
}
void UProjectProject01TutorialWidget::HandleContinue() { if (auto* M=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) M->ContinueTutorial(); }
FReply UProjectProject01TutorialWidget::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& Event)
{
	// Backdrop/text clicks never acknowledge a step. The confirmation button owns that action.
	return Super::NativeOnMouseButtonDown(G,Event);
}
void UProjectProject01TutorialWidget::HandleReturn() { if (auto* M=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) M->ReturnToMatching(); }
void UProjectProject01TutorialWidget::HandleRestart() { if (auto* M=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) M->RestartTutorial(true); }
void UProjectProject01TutorialWidget::HandleRestartFromBeginning() { if (auto* M=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) M->RestartTutorial(false); }

bool UProjectProject01PracticeWidget::Initialize()
{
	if (!Super::Initialize()) return false;
	SetIsFocusable(true);
	auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
	ContactPanel=WidgetTree->ConstructWidget<UBorder>();ContactPanel->SetPadding(FMargin(12));ContactPanel->SetBrushColor(ProjectDFSDUI::Panel().CopyWithNewOpacity(.9f));
	auto* ContactPosition=Canvas->AddChildToCanvas(ContactPanel);ContactPosition->SetAutoSize(true);ContactPosition->SetPosition(FVector2D(28,130));
	auto* ContactWidth=WidgetTree->ConstructWidget<USizeBox>();ContactWidth->SetWidthOverride(500);ContactPanel->AddChild(ContactWidth);
	ContactStatus=WidgetTree->ConstructWidget<UTextBlock>();ContactStatus->SetAutoWrapText(false);ContactStatus->SetWrapTextAt(500);
	auto ContactFont=ContactStatus->GetFont();ContactFont.Size=16;ContactStatus->SetFont(ContactFont);ContactWidth->AddChild(ContactStatus);
	auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetPadding(FMargin(14));Frame->SetBrushColor(ProjectDFSDUI::Panel().CopyWithNewOpacity(.96f));
	auto* Position=Canvas->AddChildToCanvas(Frame);Position->SetAnchors(FAnchors(1,0));Position->SetAlignment(FVector2D(1,0));Position->SetPosition(FVector2D(-24,24));Position->SetAutoSize(true);
	PanelBounds=WidgetTree->ConstructWidget<USizeBox>();PanelBounds->SetWidthOverride(400);PanelBounds->SetHeightOverride(650);Frame->AddChild(PanelBounds);
	auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();PanelBounds->AddChild(Root);
	ToggleButton=ProjectTutorial::Button(WidgetTree,Root,TEXT("연습 설정 접기"));ToggleText=Cast<UTextBlock>(ToggleButton->GetContent());
	ToggleButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleToggle);
	SettingsPanel=WidgetTree->ConstructWidget<UVerticalBox>();
	auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Root->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Scroll->AddChild(SettingsPanel);
	Status=ProjectTutorial::Label(WidgetTree,SettingsPanel,16);Status->SetWrapTextAt(350);
	auto* Hint=ProjectTutorial::Label(WidgetTree,SettingsPanel,14);Hint->SetWrapTextAt(350);
	Hint->SetText(ProjectTutorial::Text(TEXT("ESC: 설정 열기/닫기 · 설정 중 일시정지\n설정 변경 후 ‘인원 적용 / 다시 연습’을 누르세요.\n적용·역할 전환 시 연습이 초기화됩니다.")));
	auto AddCount=[&](UVerticalBox* Parent,const TCHAR* Caption,int32 Maximum,int32 Value,
		TObjectPtr<UButton>& Minus,TObjectPtr<UButton>& Plus)
	{
		auto* Label=ProjectTutorial::Label(WidgetTree,Parent,17);Label->SetText(ProjectTutorial::Text(Caption));
		auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(8,2));
		auto CountButton=[&](const TCHAR* Text)
		{
			auto* Button=WidgetTree->ConstructWidget<UButton>();auto* CaptionText=WidgetTree->ConstructWidget<UTextBlock>();
			CaptionText->SetText(ProjectTutorial::Text(Text));auto Font=CaptionText->GetFont();Font.Size=22;CaptionText->SetFont(Font);
			Button->AddChild(CaptionText);ProjectDFSDUI::Apply(Button);return Button;
		};
		Minus=CountButton(TEXT(" − "));Row->AddChildToHorizontalBox(Minus);
		auto* Input=WidgetTree->ConstructWidget<USpinBox>();Input->SetMinValue(1);Input->SetMaxValue(Maximum);Input->SetMinSliderValue(1);Input->SetMaxSliderValue(Maximum);Input->SetDelta(1);
		Input->SetAlwaysUsesDeltaSnap(true);Input->SetEnableSlider(true);
		Input->SetForegroundColor(FSlateColor(FLinearColor::Black));
		auto Font=Input->GetFont();Font.Size=18;Input->SetFont(Font);
		Input->SetMinFractionalDigits(0);Input->SetMaxFractionalDigits(0);Input->SetValue(Value);
		auto* InputSlot=Row->AddChildToHorizontalBox(Input);InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));InputSlot->SetPadding(FMargin(8,0));
		Plus=CountButton(TEXT(" + "));Row->AddChildToHorizontalBox(Plus);
		return Input;
	};
	auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>();
	auto AddToggle=[&](UVerticalBox* Parent,const TCHAR* Caption,bool Checked)
	{
		auto* Toggle=WidgetTree->ConstructWidget<UCheckBox>();auto* Text=WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(ProjectTutorial::Text(Caption));Text->SetColorAndOpacity(ProjectDFSDUI::Text());auto Font=Text->GetFont();Font.Size=14;Text->SetFont(Font);
		Toggle->AddChild(Text);Toggle->SetIsChecked(Checked);Parent->AddChildToVerticalBox(Toggle)->SetPadding(FMargin(8,4));return Toggle;
	};
	DirectorToggle=AddToggle(SettingsPanel,TEXT("마네킹 플레이어 AI 활성화"),Mode && Mode->IsPracticeDirectorConfigured());
	DirectorContactToggle=AddToggle(SettingsPanel,TEXT("AI의 접촉 피해 전환 허용"),!Mode || Mode->CanPracticeDirectorToggleContact());
	DirectorContactToggle->SetToolTipText(ProjectTutorial::Text(TEXT("해제하면 AI가 접촉 피해 ON/OFF를 바꾸지 않습니다. 접촉 피해 자체를 끄는 설정은 아닙니다.")));
	InfiniteLivesToggle=AddToggle(SettingsPanel,TEXT("무한 목숨"),Mode && Mode->HasPracticeInfiniteLives());
	MannequinCount=AddCount(SettingsPanel,TEXT("마네킹 수 (1~10)"),10,Mode ? Mode->GetPracticeMannequinCount() : 10,MannequinMinus,MannequinPlus);
	SurvivorControls=WidgetTree->ConstructWidget<UVerticalBox>();SettingsPanel->AddChildToVerticalBox(SurvivorControls);
	SurvivorCount=AddCount(SurvivorControls,TEXT("생존자 AI 수 (1~2)"),2,Mode ? Mode->GetPracticeSurvivorCount() : 1,SurvivorMinus,SurvivorPlus);
	StationaryToggle=AddToggle(SurvivorControls,TEXT("생존자 AI 제자리 대기"),Mode && Mode->ArePracticeSurvivorsStationary());
	StandingWatchToggle=AddToggle(SurvivorControls,TEXT("대기 중 주변 경계"),!Mode || Mode->DoStationarySurvivorsLookAround());
	CornerToggle=AddToggle(SurvivorControls,TEXT("생존자 AI 코너 배치"),Mode && Mode->IsPracticeCornerPlacementEnabled());
	CornerControls=WidgetTree->ConstructWidget<UVerticalBox>();SurvivorControls->AddChildToVerticalBox(CornerControls);
	CornerCount=AddCount(CornerControls,TEXT("코너에 배치할 인원"),2,Mode?Mode->GetPracticeCornerCount():1,CornerMinus,CornerPlus);
	MannequinMinus->OnClicked.AddUniqueDynamic(this,&ThisClass::LessMannequins);MannequinPlus->OnClicked.AddUniqueDynamic(this,&ThisClass::MoreMannequins);
	SurvivorMinus->OnClicked.AddUniqueDynamic(this,&ThisClass::LessSurvivors);SurvivorPlus->OnClicked.AddUniqueDynamic(this,&ThisClass::MoreSurvivors);
	CornerMinus->OnClicked.AddUniqueDynamic(this,&ThisClass::LessCorners);CornerPlus->OnClicked.AddUniqueDynamic(this,&ThisClass::MoreCorners);
	// Keep apply/switch/back reachable even when the population controls scroll.
	auto* Actions=WidgetTree->ConstructWidget<UVerticalBox>();Root->AddChildToVerticalBox(Actions);
	ApplyButton=ProjectTutorial::Button(WidgetTree,Actions,TEXT("인원 적용 / 다시 연습"));ApplyText=Cast<UTextBlock>(ApplyButton->GetContent());ApplyButton->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleApply);
	SurvivorSwitch=ProjectTutorial::Button(WidgetTree,Actions,TEXT("생존자 연습으로 전환"));SurvivorSwitch->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleSurvivor);
	MannequinSwitch=ProjectTutorial::Button(WidgetTree,Actions,TEXT("마네킹 연습으로 전환"));MannequinSwitch->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleMannequin);
	auto* Back=ProjectTutorial::Button(WidgetTree,Actions,TEXT("로비로 돌아가기"));Back->OnClicked.AddUniqueDynamic(this,&ThisClass::HandleReturn);
	ProjectDFSDUI::ApplyTree(WidgetTree);return true;
}
void UProjectProject01PracticeWidget::NativeTick(const FGeometry& Geometry,float Delta)
{
	Super::NativeTick(Geometry,Delta);
	if (auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>())
	{
		SettingsPanel->SetVisibility(Mode->IsPracticeMenuOpen() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		ApplyButton->GetParent()->SetVisibility(Mode->IsPracticeMenuOpen() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		PanelBounds->SetHeightOverride(Mode->IsPracticeMenuOpen() ? 650 : 60);
		ToggleText->SetText(ProjectTutorial::Text(Mode->IsPracticeMenuOpen() ? TEXT("연습 설정 접기") : TEXT("연습 설정 펼치기 (ESC)")));
		ToggleButton->SetIsEnabled(!Mode->IsPracticeRoundEnded());Status->SetText(Mode->GetFreePracticeStatus());
		const bool bMannequin=Mode->IsMannequinTutorial();
		SurvivorControls->SetVisibility(bMannequin?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
		StandingWatchToggle->SetIsEnabled(StationaryToggle->IsChecked());
		DirectorToggle->SetVisibility(bMannequin?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
		DirectorContactToggle->SetVisibility(bMannequin?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
		DirectorContactToggle->SetIsEnabled(DirectorToggle->IsChecked());
		ContactPanel->SetVisibility(Mode->IsPracticeDirectorEnabled() && !Mode->IsPracticeRoundEnded()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		ContactStatus->SetText(Mode->GetPracticeContactDamageText());
		ContactStatus->SetColorAndOpacity(Mode->IsPracticeContactDamageEnabled()?FLinearColor(1,.45f,.35f):FLinearColor(.65f,1,.8f));
		CornerControls->SetIsEnabled(CornerToggle->IsChecked());
		const int32 MaximumCorners=FMath::Clamp(FMath::RoundToInt(SurvivorCount->GetValue()),1,2);
		CornerCount->SetMaxValue(MaximumCorners);CornerCount->SetMaxSliderValue(MaximumCorners);
		if(CornerCount->GetValue()>MaximumCorners)CornerCount->SetValue(MaximumCorners);
		CornerMinus->SetIsEnabled(CornerCount->GetValue()>1);CornerPlus->SetIsEnabled(CornerCount->GetValue()<MaximumCorners);
		SurvivorSwitch->SetVisibility(bMannequin?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
		MannequinSwitch->SetVisibility(bMannequin?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
		MannequinMinus->SetIsEnabled(MannequinCount->GetValue()>1);MannequinPlus->SetIsEnabled(MannequinCount->GetValue()<10);
		SurvivorMinus->SetIsEnabled(SurvivorCount->GetValue()>1);SurvivorPlus->SetIsEnabled(SurvivorCount->GetValue()<2);
	}
}
FReply UProjectProject01PracticeWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
	if (Event.GetKey()==EKeys::Escape) { HandleToggle();return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry,Event);
}
void UProjectProject01PracticeWidget::HandleToggle() { if (auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) Mode->ShowPracticeMenu(!Mode->IsPracticeMenuOpen()); }
void UProjectProject01PracticeWidget::SetCount(USpinBox* Input,int32 Delta,int32 Maximum)
{ if (Input) Input->SetValue(FMath::Clamp(FMath::RoundToInt(Input->GetValue())+Delta,1,Maximum)); }
void UProjectProject01PracticeWidget::LessMannequins() { SetCount(MannequinCount,-1,10); }
void UProjectProject01PracticeWidget::MoreMannequins() { SetCount(MannequinCount,1,10); }
void UProjectProject01PracticeWidget::LessSurvivors() { SetCount(SurvivorCount,-1,2); }
void UProjectProject01PracticeWidget::MoreSurvivors() { SetCount(SurvivorCount,1,2); }
void UProjectProject01PracticeWidget::LessCorners() { SetCount(CornerCount,-1,FMath::RoundToInt(SurvivorCount->GetValue())); }
void UProjectProject01PracticeWidget::MoreCorners() { SetCount(CornerCount,1,FMath::RoundToInt(SurvivorCount->GetValue())); }
void UProjectProject01PracticeWidget::ApplyRole(bool bMannequin)
{ if (auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) Mode->RestartFreePractice(bMannequin,FMath::RoundToInt(MannequinCount->GetValue()),FMath::RoundToInt(SurvivorCount->GetValue()),StationaryToggle->IsChecked(),StandingWatchToggle->IsChecked(),InfiniteLivesToggle->IsChecked(),CornerToggle->IsChecked(),FMath::RoundToInt(CornerCount->GetValue()),DirectorToggle->IsChecked(),DirectorContactToggle->IsChecked()); }
void UProjectProject01PracticeWidget::HandleApply() { if (auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) ApplyRole(Mode->IsMannequinTutorial()); }
void UProjectProject01PracticeWidget::HandleSurvivor() { ApplyRole(false); }
void UProjectProject01PracticeWidget::HandleMannequin() { ApplyRole(true); }
void UProjectProject01PracticeWidget::HandleReturn() { if (auto* Mode=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>()) Mode->ReturnToMatching(); }
