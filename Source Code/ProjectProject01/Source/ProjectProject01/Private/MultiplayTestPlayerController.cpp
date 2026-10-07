// File: Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp
// Build target: ProjectProject01Server / ProjectProject01

#include "MultiplayTestPlayerController.h"

#include "InputCoreTypes.h"
#include "MannequinAICharacter.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01LoginWidget.h"
#include "Net/UnrealNetwork.h"

void AMultiplayTestPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	// 로비의 UIOnly 입력 상태가 클라이언트 트래블 뒤에도 남지 않도록 게임 입력을 명시적으로 복구한다.
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void AMultiplayTestPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!ensureMsgf(IsValid(InputComponent), TEXT("MultiplayTestPlayerController has no InputComponent.")))
	{
		return;
	}

	InputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot0);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot1);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot2);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot3);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot4);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot5);
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot6);
	InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot7);
	InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot8);
	InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &AMultiplayTestPlayerController::SelectMannequinSlot9);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinManualControl);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AMultiplayTestPlayerController::RequestPostPossessionChaseCommand);
	InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &AMultiplayTestPlayerController::ToggleSessionMenu);
}

void AMultiplayTestPlayerController::ToggleSessionMenu()
{
	if (!IsLocalController())
	{
		return;
	}
	if (IsValid(SessionMenuWidget) && SessionMenuWidget->IsInViewport())
	{
		SessionMenuWidget->CloseMenu();
		return;
	}

	SessionMenuWidget = CreateWidget<UProjectProject01SessionMenuWidget>(
		this, UProjectProject01SessionMenuWidget::StaticClass());
	if (!ensureMsgf(IsValid(SessionMenuWidget), TEXT("MultiplayTest player controller could not create the session menu.")))
	{
		return;
	}
	SessionMenuWidget->ConfigureForSession(true);
	SessionMenuWidget->AddToViewport(500);

	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(SessionMenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

AMannequinAICharacter* AMultiplayTestPlayerController::GetViewedMannequin() const
{
	return IsValid(ViewedMannequin) ? ViewedMannequin.Get() : nullptr;
}

void AMultiplayTestPlayerController::SetViewedMannequin(AMannequinAICharacter* Mannequin)
{
	if (!HasAuthority() || !IsValid(Mannequin))
	{
		return;
	}

	ViewedMannequin = Mannequin;
	ApplyViewedMannequinCamera(Mannequin);
	ForceNetUpdate();
	ClientApplyViewedMannequin(Mannequin);
}

void AMultiplayTestPlayerController::SetAuthenticatedLobbyIdentity(
	const FProjectProject01ValidatedJoinClaim& Claim)
{
	if (!HasAuthority())
	{
		return;
	}
	AuthenticatedUserId = Claim.UserId;
	AuthenticatedMatchId = Claim.MatchId;
	AuthenticatedRole = Claim.Role;
}

void AMultiplayTestPlayerController::SetServerMatchState(
	const EMultiplayTestMatchPhase NewPhase,
	const EMultiplayTestSurvivorState NewSurvivorState)
{
	if (!HasAuthority())
	{
		return;
	}

	MatchPhase = NewPhase;
	SurvivorState = NewSurvivorState;
	ForceNetUpdate();
}

void AMultiplayTestPlayerController::DeliverMatchResultToOwner(const FMultiplayTestMatchResult& Result)
{
	if (!HasAuthority())
	{
		return;
	}
	ClientPresentMatchResult(Result);
}

void AMultiplayTestPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	ApplyViewedMannequinCamera(GetViewedMannequin());
}

void AMultiplayTestPlayerController::ClientApplyViewedMannequin_Implementation(AMannequinAICharacter* Mannequin)
{
	ApplyViewedMannequinCamera(Mannequin);
}

void AMultiplayTestPlayerController::ClientPresentMatchResult_Implementation(
	const FMultiplayTestMatchResult& Result)
{
	if (!IsLocalController())
	{
		return;
	}

	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	if (IsValid(SessionMenuWidget))
	{
		SessionMenuWidget->RemoveFromParent();
		SessionMenuWidget = nullptr;
	}
	if (IsValid(MatchResultWidget))
	{
		MatchResultWidget->RemoveFromParent();
	}

	MatchResultWidget = CreateWidget<UProjectProject01MatchResultWidget>(
		this, UProjectProject01MatchResultWidget::StaticClass());
	if (!ensureMsgf(IsValid(MatchResultWidget), TEXT("Unable to create the multiplayer match result widget.")))
	{
		return;
	}
	MatchResultWidget->ConfigureResult(Result);
	MatchResultWidget->AddToViewport(700);
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(MatchResultWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

void AMultiplayTestPlayerController::OnRep_ViewedMannequin()
{
	ApplyViewedMannequinCamera(GetViewedMannequin());
}

void AMultiplayTestPlayerController::ApplyViewedMannequinCamera(AMannequinAICharacter* Mannequin)
{
	if (!IsLocalController() || !IsValid(Mannequin))
	{
		return;
	}

	// 번호키로 선택한 시점은 Possess/UnPossess 직후의 자동 Pawn 카메라 갱신보다 우선한다.
	// 이 함수는 마네킹 조종자로 지정된 컨트롤러에서만 서버가 호출한다.
	bAutoManageActiveCameraTarget = false;

	const FViewTargetTransitionParams TransitionParams;
	SetViewTarget(Mannequin, TransitionParams);
}

void AMultiplayTestPlayerController::SelectMannequinSlot0() { RequestMannequinSlot(0); }
void AMultiplayTestPlayerController::SelectMannequinSlot1() { RequestMannequinSlot(1); }
void AMultiplayTestPlayerController::SelectMannequinSlot2() { RequestMannequinSlot(2); }
void AMultiplayTestPlayerController::SelectMannequinSlot3() { RequestMannequinSlot(3); }
void AMultiplayTestPlayerController::SelectMannequinSlot4() { RequestMannequinSlot(4); }
void AMultiplayTestPlayerController::SelectMannequinSlot5() { RequestMannequinSlot(5); }
void AMultiplayTestPlayerController::SelectMannequinSlot6() { RequestMannequinSlot(6); }
void AMultiplayTestPlayerController::SelectMannequinSlot7() { RequestMannequinSlot(7); }
void AMultiplayTestPlayerController::SelectMannequinSlot8() { RequestMannequinSlot(8); }
void AMultiplayTestPlayerController::SelectMannequinSlot9() { RequestMannequinSlot(9); }

void AMultiplayTestPlayerController::RequestMannequinSlot(int32 Slot)
{
	if (!IsLocalController() || Slot < 0 || Slot > 9)
	{
		return;
	}

	ServerRequestMannequinSlot(Slot);
}

void AMultiplayTestPlayerController::RequestMannequinManualControl()
{
	if (IsLocalController())
	{
		ServerRequestMannequinManualControl();
	}
}

void AMultiplayTestPlayerController::RequestPostPossessionChaseCommand()
{
	if (IsLocalController())
	{
		ServerRequestPostPossessionChaseCommand();
	}
}

void AMultiplayTestPlayerController::ServerRequestMannequinSlot_Implementation(int32 Slot)
{
	if (!AllowServerRequest(0, 12, 1.0f))
	{
		return;
	}
	if (Slot < 0 || Slot > 9)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Rejected invalid mannequin slot %d from %s."), Slot, *GetName());
		return;
	}

	UWorld* World = GetWorld();
	AMultiplayTestGameMode* GameMode = IsValid(World)
		? World->GetAuthGameMode<AMultiplayTestGameMode>()
		: nullptr;
	if (!ensureMsgf(IsValid(GameMode), TEXT("Mannequin slot request requires AMultiplayTestGameMode.")))
	{
		return;
	}

	GameMode->TryPossessMannequin(this, Slot);
}

bool AMultiplayTestPlayerController::ServerRequestMannequinSlot_Validate(const int32 Slot)
{
	return Slot >= 0 && Slot <= 9;
}

void AMultiplayTestPlayerController::ServerRequestMannequinManualControl_Implementation()
{
	if (!AllowServerRequest(1, 6, 1.0f))
	{
		return;
	}
	UWorld* World = GetWorld();
	AMultiplayTestGameMode* GameMode = IsValid(World)
		? World->GetAuthGameMode<AMultiplayTestGameMode>()
		: nullptr;
	if (!ensureMsgf(IsValid(GameMode), TEXT("Mannequin manual-control request requires AMultiplayTestGameMode.")))
	{
		return;
	}

	GameMode->TryEnableMannequinManualControl(this);
}

bool AMultiplayTestPlayerController::ServerRequestMannequinManualControl_Validate()
{
	return true;
}

void AMultiplayTestPlayerController::ServerRequestPostPossessionChaseCommand_Implementation()
{
	if (!AllowServerRequest(2, 6, 1.0f))
	{
		return;
	}
	UWorld* World = GetWorld();
	AMultiplayTestGameMode* GameMode = IsValid(World)
		? World->GetAuthGameMode<AMultiplayTestGameMode>()
		: nullptr;
	if (!ensureMsgf(IsValid(GameMode), TEXT("Mannequin post-possession command requires AMultiplayTestGameMode.")))
	{
		return;
	}

	GameMode->TryQueuePostPossessionChaseCommand(this);
}

bool AMultiplayTestPlayerController::ServerRequestPostPossessionChaseCommand_Validate()
{
	return true;
}

bool AMultiplayTestPlayerController::AllowServerRequest(
	const uint8 RequestType,
	const int32 MaximumCalls,
	const float WindowSeconds)
{
	if (!HasAuthority() || RequestType >= UE_ARRAY_COUNT(RpcWindowStartSeconds) ||
		MaximumCalls <= 0 || WindowSeconds <= 0.0f)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	const float Now = World->GetTimeSeconds();
	if (Now - RpcWindowStartSeconds[RequestType] >= WindowSeconds)
	{
		RpcWindowStartSeconds[RequestType] = Now;
		RpcWindowCallCount[RequestType] = 0;
	}
	if (++RpcWindowCallCount[RequestType] > MaximumCalls)
	{
		UE_LOG(LogProjectProject01Multiplayer, Warning,
			TEXT("Security rejected excessive mannequin RPC type %d from %s."),
			RequestType, *GetNameSafe(this));
		return false;
	}
	return true;
}

void AMultiplayTestPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, ViewedMannequin, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, MatchPhase, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, SurvivorState, COND_OwnerOnly);
}
