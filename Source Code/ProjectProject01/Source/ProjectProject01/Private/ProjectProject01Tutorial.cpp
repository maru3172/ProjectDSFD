#include "ProjectProject01Tutorial.h"
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
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"

namespace ProjectTutorial
{
	const FName Map(TEXT("/Game/MyProject/Level/MultiplayerTutorial"));
	FText Text(const TCHAR* Value) { return FText::FromString(Value); }
	FText ResolveText(const FText& Value)
	{
		FString Result=Value.ToString();
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
		T->SetColorAndOpacity(FLinearColor::White); T->SetAutoWrapText(true); T->SetWrapTextAt(560);
		auto Font=T->GetFont(); Font.Size=Size; T->SetFont(Font);
		Root->AddChildToVerticalBox(T)->SetPadding(FMargin(8)); return T;
	}
	UButton* Button(UWidgetTree* Tree,UVerticalBox* Root,const TCHAR* Caption)
	{
		auto* B=Tree->ConstructWidget<UButton>(); auto* T=Tree->ConstructWidget<UTextBlock>();
		T->SetText(Text(Caption)); T->SetColorAndOpacity(FLinearColor::Black);
		auto Font=T->GetFont(); Font.Size=20; T->SetFont(Font); B->AddChild(T);
		Root->AddChildToVerticalBox(B)->SetPadding(FMargin(8)); return B;
	}
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
	Add(SurvivorSteps,TEXT("Survivor.Intro"),TEXT("생존자 · 동료와 협력하여 탈출하세요"),TEXT("당신은 생존자입니다. 마네킹을 바라보면 멈출 수 있지만, 시선을 돌리면 추격할 수 있습니다. 동료와 협력하여 마지막 탈출구에 도착하는 것이 최종 목표입니다.\n이곳은 멀티 규칙을 배우는 별도 연습 맵입니다. 연습 중 AI 접촉 피해는 꺼져 있습니다.\n준비되면 ‘조작 연습 시작’을 누르세요."),EProjectTutorialCondition::Continue);
	Add(SurvivorSteps,TEXT("Survivor.Walk"),TEXT("주변을 둘러보고 걸어 보세요"),TEXT("마우스로 시점을 움직이고 {Move}로 이동하세요. 약 3m 걸으면 다음 설명으로 넘어갑니다."),EProjectTutorialCondition::Walk,300);
	Add(SurvivorSteps,TEXT("Survivor.Sprint"),TEXT("달리기와 스태미나를 확인하세요"),TEXT("이동하며 {Sprint}를 눌러 약 2m 달려 보세요. 달리면 스태미나가 소모됩니다. 시선을 돌린 동안에도 마네킹이 접근할 수 있습니다."),EProjectTutorialCondition::Sprint,200);
	Add(SurvivorSteps,TEXT("Survivor.Observe"),TEXT("마네킹을 바라보며 멈추게 하세요"),TEXT("연습장 마네킹을 가까이서 1초간 바라보세요. 벽에 가려져 있거나 시야 밖에 있으면 멈추지 않습니다. 동료와 번갈아 바라보는 것이 중요합니다."),EProjectTutorialCondition::ObserveMannequin,1);
	Add(SurvivorSteps,TEXT("Survivor.Ping"),TEXT("핑으로 동료에게 정보를 전달하세요"),TEXT("{Help}: 도움 / {Danger}: 위험 / {Location}: 위치. 셋 중 하나를 눌러 보세요.\n실제 경기에서는 생존자끼리만 보이스가 들립니다. {PTT}: 누르는 동안 말하기, {Voice}: 상시 송신 전환. 여기서는 실제 음성 연결을 하지 않습니다."),EProjectTutorialCondition::Ping);
	Add(SurvivorSteps,TEXT("Survivor.Escape"),TEXT("연습장의 ‘탈출’ 표지로 이동하세요"),TEXT("배운 조작을 사용해 탈출 표시 영역에 들어가세요. 실제 경기에서는 동료를 구출하고 함께 탈출하는 것이 목표입니다.\n이 영역은 이번 연습을 끝낼 뿐, 다른 층이나 맵으로 이동하지 않습니다. ESC: 잠시 멈추고 메뉴 조작."),EProjectTutorialCondition::Trigger,1,TEXT("Tutorial.Escape"));
	Add(MannequinSteps,TEXT("Mannequin.Intro"),TEXT("마네킹 · 생존자의 탈출을 저지하세요"),TEXT("당신은 마네킹 플레이어입니다. 여러 마네킹의 시점을 선택하고 명령하거나 직접 조작할 수 있습니다. 생존자 전원을 붙잡아 탈출을 막는 것이 최종 목표입니다.\n생존자가 바라보는 마네킹은 멈춥니다. 카메라를 둘러보는 것과 마네킹을 직접 조작하는 것은 다릅니다."),EProjectTutorialCondition::Continue);
	Add(MannequinSteps,TEXT("Mannequin.Select"),TEXT("다른 마네킹의 시점을 선택하세요"),TEXT("머리 위 번호를 확인하고 0~9 중 해당 번호를 눌러 보세요. 아직 직접 조작 상태가 아닙니다. 마우스로 카메라만 둘러볼 수 있습니다."),EProjectTutorialCondition::SelectMannequin);
	Add(MannequinSteps,TEXT("Mannequin.Manual"),TEXT("선택한 마네킹을 직접 조작하세요"),TEXT("V를 누르면 직접 조작합니다. 카메라가 그 마네킹이 바라보던 방향으로 정렬된 뒤 움직일 수 있습니다. 바라보고 있는 생존자가 있으면 움직이지 못합니다."),EProjectTutorialCondition::ManualControl);
	Add(MannequinSteps,TEXT("Mannequin.Move"),TEXT("직접 조작으로 이동해 보세요"),TEXT("{Move}와 마우스로 약 2m 이동하세요. 직접 조작 중에만 몸의 방향이 카메라 방향을 따릅니다."),EProjectTutorialCondition::MoveMannequin,200);
	Add(MannequinSteps,TEXT("Mannequin.Command"),TEXT("AI에게 맡길 명령을 지정하세요"),TEXT("직접 조작 중 Z: 정찰 / X: 감지 범위 추격 / C: 자신을 따라오기. 하나를 눌러 지정하세요. 빙의를 해제하면 지정한 명령을 유지하며, 생존자를 감지하면 추격을 우선합니다.\nQ: 추종 초기화. B: 전체 AI의 접촉 데스카운트 소모 전환. 직접 조작 중 공격과 접촉은 이 설정과 별개입니다."),EProjectTutorialCondition::IssueCommand);
	Add(MannequinSteps,TEXT("Mannequin.Release"),TEXT("빙의를 해제하고 AI에게 맡기세요"),TEXT("번호키로 다른 마네킹 시점을 선택해 직접 조작을 해제하세요. V를 다시 누르는 방식이 아닙니다. 명령은 현재 맵이 끝날 때까지 유지되고 다음 맵에서는 초기화됩니다."),EProjectTutorialCondition::ReleaseMannequin);
	Add(MannequinSteps,TEXT("Mannequin.Capture"),TEXT("연습용 생존자 1명을 붙잡으세요"),TEXT("연습장 중앙에 생존자 1명이 나타났습니다. 번호키와 V로 직접 조작한 뒤 가까이 다가가 마우스 좌클릭으로 공격하세요. 사거리와 벽을 확인하세요.\n이번 연습에서는 AI 접촉 피해가 꺼져 있으므로 직접 붙잡아야 합니다."),EProjectTutorialCondition::Capture);
	SurvivorClosing=ProjectTutorial::Text(TEXT("실전에서도 동료와 협력하여 무사히 탈출하시길 바랍니다. 건승을 빕니다."));
	MannequinClosing=ProjectTutorial::Text(TEXT("생존자가 탈출할 수 없게 저지하시길 바랍니다. 건승을 빕니다."));
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
	InitializePractice();
}
void AProjectProject01TutorialGameMode::InitializePractice()
{
	if (bInitialized || !Learner || !Plan || GetNetMode()!=NM_Standalone) return;
	if (Steps.IsEmpty()) { UE_LOG(LogTemp,Error,TEXT("TutorialPlan has no steps for this role.")); ReturnToMatching(); return; }
	bInitialized=true; bGlobalMannequinCatchDamageEnabled=false; MatchTimeLimitSeconds=0;
	MatchStartWorldSeconds=GetWorld()->GetTimeSeconds(); SetAuthoritativeMatchPhase(EMultiplayTestMatchPhase::InProgress);
	// Standalone PostLogin precedes GameMode BeginPlay and runtime mannequin spawning.
	if (bMannequin && !Learner->GetViewedMannequin()) AssignRandomInitialMannequinView(Learner);
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It) It->SetCatchDamageEnabled(false);
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
const FProjectTutorialStep* AProjectProject01TutorialGameMode::GetCurrentStep() const { return Steps.IsValidIndex(StepIndex) ? &Steps[StepIndex] : nullptr; }
FText AProjectProject01TutorialGameMode::GetClosingText() const { return Plan ? (bMannequin ? Plan->MannequinClosing : Plan->SurvivorClosing) : FText::GetEmpty(); }
void AProjectProject01TutorialGameMode::SetMenuInput(bool bCursor)
{
	if (!Learner) return;
	Learner->bShowMouseCursor=bCursor; Learner->ResetIgnoreMoveInput(); Learner->ResetIgnoreLookInput();
	Learner->SetIgnoreMoveInput(bCursor || bFinishing); Learner->SetIgnoreLookInput(bCursor || bFinishing);
	if (bCursor) if (auto* Character=Cast<ACharacter>(Learner->GetPawn())) Character->GetCharacterMovement()->StopMovementImmediately();
	if (bCursor) { FInputModeGameAndUI Input; if (Widget) Input.SetWidgetToFocus(Widget->TakeWidget()); Input.SetHideCursorDuringCapture(false); Learner->SetInputMode(Input); }
	else Learner->SetInputMode(FInputModeGameOnly());
}
void AProjectProject01TutorialGameMode::EnterStep()
{
	const auto* S=GetCurrentStep(); if (!S || !Learner) return;
	Progress=0; LastLocation=Learner->GetPawn() ? Learner->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	bAwaitingRead=true;
	if (auto* C=Learner->FindComponentByClass<UProjectProject01ObjectiveComponent>()) C->UpdateObjective(S->Id,S->Title,ProjectTutorial::ResolveText(S->Instructions),StepIndex+1,Steps.Num());
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
	if (S->Condition==Action || (S->Condition==EProjectTutorialCondition::ReleaseMannequin && Action==EProjectTutorialCondition::SelectMannequin && !PC->GetPawn()))
		if (++Progress>=FMath::Max(1.0f,S->RequiredAmount)) AdvanceStep();
}
bool AProjectProject01TutorialGameMode::SignalEvent(AMultiplayTestPlayerController* PC,FName Event)
{
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
	if (!Learner || bFinishing) return;
	// ESC may reopen the explanation, but only its explicit confirmation can dismiss it.
	if (!bAwaitingRead) { bAwaitingRead=true; SetMenuInput(true); }
}
void AProjectProject01TutorialGameMode::Tick(float Delta)
{
	Super::Tick(Delta);
	if (!bInitialized || !Learner || bReturning) return;
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
		const bool bMove=S->Condition==EProjectTutorialCondition::Walk || (S->Condition==EProjectTutorialCondition::MoveMannequin && Cast<AMannequinAICharacter>(Pawn));
		const bool bSprint=S->Condition==EProjectTutorialCondition::Sprint && Survivor && Survivor->IsSprintingForBreathing();
		if ((bMove || bSprint) && !Learner->IsMoveInputIgnored() && Pawn->GetVelocity().SizeSquared2D()>25) Progress+=FMath::Min(Distance,100.0f);
	}
	if (S->Condition==EProjectTutorialCondition::ObserveMannequin)
	{
		bool bObserved=false;
		for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It)
			if (It->IsFrozenBySurvivorVision()) { bObserved=true; break; }
		Progress=bObserved ? Progress+Delta : 0;
	}
	if (S->Condition==EProjectTutorialCondition::Walk || S->Condition==EProjectTutorialCondition::Sprint || S->Condition==EProjectTutorialCondition::MoveMannequin || S->Condition==EProjectTutorialCondition::ObserveMannequin)
		if (Progress>=FMath::Max(0.1f,S->RequiredAmount)) AdvanceStep();
	// A prerequisite may become true while already standing inside the volume.
	if (const auto* Current=GetCurrentStep(); Current && Current->Condition==EProjectTutorialCondition::Trigger && Learner->GetPawn())
		for (TActorIterator<AProjectProject01ObjectiveTrigger> It(GetWorld()); It; ++It)
			if (It->TutorialEvent==Current->Event && It->Volume->IsOverlappingActor(Learner->GetPawn()))
			{ It->ApplyTo(Learner); break; }
}
void AProjectProject01TutorialGameMode::SpawnPracticeSurvivor()
{
	if (!Learner || !Plan) return;
	auto* PC=GetWorld()->SpawnActor<AMultiplayTestPlayerController>();
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector Location=Plan->GetActorTransform().TransformPosition(Plan->PracticeTargetOffset);
	auto* Pawn=GetWorld()->SpawnActor<APlayerCharacter>(DefaultPawnClass,Location,FRotator(0,90,0),Params);
	if (!PC || !Pawn) return;
	PC->Possess(Pawn); PC->SetControlRotation(FRotator(0,90,0)); Pawn->SetRemainingDeathCountForGameMode(0);
	FProjectProject01ValidatedJoinClaim Claim; Claim.UserId=TEXT("Tutorial.Target"); Claim.MatchId=TEXT("Tutorial.Local"); Claim.Role=TEXT("Survivor"); Claim.DisplayName=TEXT("연습용 생존자"); PC->SetAuthenticatedLobbyIdentity(Claim);
	RegisterMatchPlayer(PC); PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
	// Only the learner's directly controlled body can deal damage in this practice.
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It) It->SetCatchDamageEnabled(false);
}
void AProjectProject01TutorialGameMode::FinishAuthoritativeMatch(const FString& Reason)
{
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
void AProjectProject01TutorialGameMode::ReturnToMatching()
{
	if (bReturning) return; bReturning=true;
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
	Panel=WidgetTree->ConstructWidget<UBorder>(); Panel->SetPadding(FMargin(14)); Panel->SetBrushColor(FLinearColor(0.025f,0.025f,0.03f,0.94f));
	auto* CanvasSlot=Canvas->AddChildToCanvas(Panel); CanvasSlot->SetAnchors(FAnchors(0.5f,0.5f)); CanvasSlot->SetAlignment(FVector2D(0.5f,0.5f)); CanvasSlot->SetAutoSize(true);
	auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(); Bounds->SetWidthOverride(620); Bounds->SetMaxDesiredHeight(620); Panel->AddChild(Bounds);
	auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Bounds->AddChild(Scroll);
	auto* Root=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Root);
	Heading=ProjectTutorial::Label(WidgetTree,Root,26); Body=ProjectTutorial::Label(WidgetTree,Root,21);
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
	Heading->SetText(ProjectTutorial::Text(Result ? TEXT("튜토리얼 완료") : Mode->IsMannequinTutorial() ? TEXT("선택한 역할: 마네킹") : TEXT("선택한 역할: 생존자")));
	Body->SetText(Finish ? Mode->GetClosingText() : !Mode->GetExtraMessage().IsEmpty() ? Mode->GetExtraMessage() : Step ? FText::Format(FText::FromString(TEXT("{0}\n\n{1}")),Step->Title,ProjectTutorial::ResolveText(Step->Instructions)) : FText::GetEmpty());
	if (Result) Body->SetText(FText::Format(FText::FromString(TEXT("{0}\n\n{1}")),
		ProjectTutorial::Text(Mode->IsMannequinTutorial() ? TEXT("연습 결과: 생존자 1명 붙잡기 성공") : TEXT("연습 결과: 무사히 탈출했습니다.")),Mode->GetClosingText()));
	ContinueButton->SetVisibility(!Result ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	RestartButton->SetVisibility(Result ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	FullRestartButton->SetVisibility(Result ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const bool Show=Mode->IsAwaitingRead() || Finish;
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
