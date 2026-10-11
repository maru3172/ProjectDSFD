// File: Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp
// Build target: ProjectProject01Server / ProjectProject01

#include "MultiplayTestPlayerController.h"

#include "InputCoreTypes.h"
#include "MannequinAICharacter.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01PingMarker.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "InputKeyEventArgs.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

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

	ServiceNoticeWidget = CreateWidget<UProjectProject01ServiceNoticeWidget>(
		this, UProjectProject01ServiceNoticeWidget::StaticClass());
	if (IsValid(ServiceNoticeWidget))
	{
		ServiceNoticeWidget->AddToViewport(950);
	}
	VoiceStatusWidget = CreateWidget<UProjectProject01VoiceStatusWidget>(
		this, UProjectProject01VoiceStatusWidget::StaticClass());
	if (IsValid(VoiceStatusWidget))
	{
		VoiceStatusWidget->AddToViewport(940);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UProjectProject01AuthSubsystem* Auth =
			GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
		{
			bHadAuthenticatedSession = Auth->IsSignedIn();
			Auth->OnServiceStatusChanged.AddUniqueDynamic(
				this, &AMultiplayTestPlayerController::HandleServiceStatusChanged);
		}
	}
	PollMultiplayerService();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ServiceStatusTimer, this, &AMultiplayTestPlayerController::PollMultiplayerService,
			5.0f, true, 5.0f);
	}
}

void AMultiplayTestPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsLocalController())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ServiceStatusTimer);
		}
		if (IsValid(VoiceStatusWidget))
		{
			VoiceStatusWidget->RemoveFromParent();
			VoiceStatusWidget = nullptr;
		}
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UProjectProject01AuthSubsystem* Auth =
				GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>())
			{
				Auth->OnServiceStatusChanged.RemoveDynamic(
					this, &AMultiplayTestPlayerController::HandleServiceStatusChanged);
			}
			if (UProjectProject01VoiceChatSubsystem* Voice = GameInstance->GetSubsystem<UProjectProject01VoiceChatSubsystem>())
			{
				Voice->LeaveMatch();
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AMultiplayTestPlayerController::PollMultiplayerService()
{
	if (!IsLocalController())
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectProject01AuthSubsystem* Auth = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
	if (!IsValid(Auth))
	{
		return;
	}
	if (!Auth->IsSignedIn())
	{
		// 티켓 없이 맵만 직접 여는 로컬 PIE는 그대로 허용한다. 실제 로그인 후 세션이
		// 만료·폐기된 클라이언트만 로그인 화면으로 돌려보낸다.
		if (bHadAuthenticatedSession)
		{
			UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LoginLevel")));
		}
		return;
	}
	bHadAuthenticatedSession = true;
	Auth->CheckMultiplayerServiceStatus();
	if (UProjectProject01LobbySubsystem* Lobby =
		GameInstance->GetSubsystem<UProjectProject01LobbySubsystem>();
		IsValid(Lobby) && !Lobby->IsRequestInFlight())
	{
		// 인증된 경량 요청이 401이면 AuthSubsystem이 토큰 갱신을 시도하고,
		// 만료·폐기된 리프레시 토큰이면 세션을 지운다.
		Lobby->RefreshCurrentRoom();
	}
}

void AMultiplayTestPlayerController::HandleServiceStatusChanged(
	const bool bMaintenanceEnabled,
	const FString& Announcement,
	const FString& ShutdownAtUtc,
	const FString& Message)
{
	if (!IsValid(ServiceNoticeWidget))
	{
		return;
	}
	FString Notice = !Announcement.IsEmpty() ? Announcement : (bMaintenanceEnabled ? Message : FString());
	if (!Notice.IsEmpty() && !ShutdownAtUtc.IsEmpty())
	{
		Notice += FString::Printf(TEXT(" (서버 종료 예정: %s)"), *ShutdownAtUtc);
	}
	ServiceNoticeWidget->SetNotice(Notice, bMaintenanceEnabled);
}

bool AMultiplayTestPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (IsLocalController())
	{
		const UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get();
		if (IsValid(Settings))
		{
			if (Params.Key == Settings->GetVoicePushToTalkKey())
			{
				if (UGameInstance* GI = GetGameInstance())
				{
					if (UProjectProject01VoiceChatSubsystem* Voice = GI->GetSubsystem<UProjectProject01VoiceChatSubsystem>())
					{
						if (Params.Event == IE_Pressed) Voice->SetPushToTalkHeld(true);
						else if (Params.Event == IE_Released) Voice->SetPushToTalkHeld(false);
					}
				}
			}
			else if (Params.Event == IE_Pressed && Params.Key == Settings->GetVoiceToggleKey())
			{
				if (UGameInstance* GI = GetGameInstance())
				{
					if (UProjectProject01VoiceChatSubsystem* Voice = GI->GetSubsystem<UProjectProject01VoiceChatSubsystem>()) Voice->ToggleOpenMic();
				}
				return true;
			}
			else if (Params.Event == IE_Pressed && Params.Key == Settings->GetHelpPingKey())
			{
				RequestTeamPing(EProjectProject01PingType::Help); return true;
			}
			else if (Params.Event == IE_Pressed && Params.Key == Settings->GetDangerPingKey())
			{
				RequestTeamPing(EProjectProject01PingType::Danger); return true;
			}
			else if (Params.Event == IE_Pressed && Params.Key == Settings->GetLocationPingKey())
			{
				RequestTeamPing(EProjectProject01PingType::Location); return true;
			}
		}
	}
	return Super::InputKey(Params);
}

void AMultiplayTestPlayerController::RequestTeamPing(const EProjectProject01PingType Type)
{
	if (IsLocalController())
	{
		ServerRequestTeamPing(Type);
	}
}

bool AMultiplayTestPlayerController::ServerRequestTeamPing_Validate(const EProjectProject01PingType Type)
{
	return static_cast<uint8>(Type) <= static_cast<uint8>(EProjectProject01PingType::Location);
}

void AMultiplayTestPlayerController::ServerRequestTeamPing_Implementation(const EProjectProject01PingType Type)
{
	if (!AllowServerRequest(5, 4, 5.0f)) return;
	AMultiplayTestGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>() : nullptr;
	if (!IsValid(GameMode)) return;

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector End = ViewLocation + ViewRotation.Vector() * 3000.0f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectProject01TeamPing), false, GetPawn());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params);
	GameMode->TryBroadcastSurvivorPing(this, Type, bHit ? Hit.ImpactPoint : End);
}

void AMultiplayTestPlayerController::ClientReceiveTeamPing_Implementation(
	const EProjectProject01PingType Type,
	const FVector_NetQuantize Location,
	const FString& SenderName)
{
	if (!IsLocalController() || SurvivorState != EMultiplayTestSurvivorState::Active || !IsValid(GetWorld())) return;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector MarkerLocation = FVector(Location) + FVector(0, 0, 60);
	const FVector ViewLocation = PlayerCameraManager
		? PlayerCameraManager->GetCameraLocation()
		: GetFocalLocation();
	const FRotator MarkerRotation = (ViewLocation - MarkerLocation).Rotation();
	if (AProjectProject01PingMarker* Marker = GetWorld()->SpawnActor<AProjectProject01PingMarker>(
		AProjectProject01PingMarker::StaticClass(), MarkerLocation, MarkerRotation, SpawnParams))
	{
		Marker->Configure(Type, SenderName);
	}
}

void AMultiplayTestPlayerController::ClientConfigureSurvivorCommunication_Implementation(
	const FString& UserId,
	const FString& MatchId,
	const FString& InRole)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UProjectProject01VoiceChatSubsystem* Voice = GI->GetSubsystem<UProjectProject01VoiceChatSubsystem>())
		{
			Voice->ConfigureForMatch(UserId, MatchId, InRole, this);
		}
	}
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
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AMultiplayTestPlayerController::ToggleSessionMenu);
	InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &AMultiplayTestPlayerController::SelectPreviousSpectatorTarget);
	InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &AMultiplayTestPlayerController::SelectNextSpectatorTarget);
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

	const EMultiplayTestSurvivorState PreviousSurvivorState = SurvivorState;
	MatchPhase = NewPhase;
	SurvivorState = NewSurvivorState;
	ForceNetUpdate();
	if (PreviousSurvivorState == EMultiplayTestSurvivorState::Active &&
		NewSurvivorState != EMultiplayTestSurvivorState::Active)
	{
		ClientConfigureSurvivorCommunication(FString(), FString(), TEXT("Disabled"));
	}
}

void AMultiplayTestPlayerController::DeliverMatchResultToOwner(const FMultiplayTestMatchResult& Result)
{
	if (!HasAuthority())
	{
		return;
	}
	ClientPresentMatchResult(Result);
}

void AMultiplayTestPlayerController::DeclareVoluntaryExit()
{
	if (IsLocalController())
	{
		ServerDeclareVoluntaryExit();
	}
}

void AMultiplayTestPlayerController::EnterSurvivorSpectator(
	AActor* InitialTarget,
	const bool bFadeTransition)
{
	if (!HasAuthority())
	{
		return;
	}
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(false);
	ClientEnterSurvivorSpectator(InitialTarget, bFadeTransition);
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

void AMultiplayTestPlayerController::ClientEnterSurvivorSpectator_Implementation(
	AActor* InitialTarget,
	const bool bFadeTransition)
{
	if (!IsLocalController())
	{
		return;
	}
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(false);
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	if (!bFadeTransition)
	{
		if (IsValid(InitialTarget))
		{
			SetViewTarget(InitialTarget);
		}
		return;
	}
	if (!IsValid(PlayerCameraManager) || !IsValid(GetWorld()))
	{
		if (IsValid(InitialTarget))
		{
			SetViewTarget(InitialTarget);
		}
		return;
	}

	PlayerCameraManager->StartCameraFade(0.0f, 1.0f, 0.5f, FLinearColor::Black, false, true);
	// 마지막 생존자의 탈락처럼 관전 대상이 없는 경우에는 암전을 유지한다.
	// 곧 표시되는 경기 결과 위젯은 검은 게임 화면 위에 정상적으로 나타난다.
	if (!IsValid(InitialTarget))
	{
		return;
	}
	const TWeakObjectPtr<AMultiplayTestPlayerController> WeakController(this);
	const TWeakObjectPtr<AActor> WeakTarget(InitialTarget);
	FTimerHandle SpectatorFadeTimer;
	GetWorld()->GetTimerManager().SetTimer(SpectatorFadeTimer, [WeakController, WeakTarget]()
	{
		AMultiplayTestPlayerController* Controller = WeakController.Get();
		AActor* Target = WeakTarget.Get();
		if (!IsValid(Controller) || !IsValid(Target))
		{
			return;
		}
		Controller->SetViewTarget(Target);
		if (IsValid(Controller->PlayerCameraManager))
		{
			Controller->PlayerCameraManager->StartCameraFade(
				1.0f, 0.0f, 0.5f, FLinearColor::Black, false, false);
		}
	}, 0.5f, false);
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

void AMultiplayTestPlayerController::SelectPreviousSpectatorTarget()
{
	if (IsLocalController() &&
		(SurvivorState == EMultiplayTestSurvivorState::Eliminated ||
		 SurvivorState == EMultiplayTestSurvivorState::Escaped))
	{
		ServerCycleSurvivorSpectator(-1);
	}
}

void AMultiplayTestPlayerController::SelectNextSpectatorTarget()
{
	if (IsLocalController() &&
		(SurvivorState == EMultiplayTestSurvivorState::Eliminated ||
		 SurvivorState == EMultiplayTestSurvivorState::Escaped))
	{
		ServerCycleSurvivorSpectator(1);
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

void AMultiplayTestPlayerController::ServerDeclareVoluntaryExit_Implementation()
{
	if (!AllowServerRequest(3, 2, 2.0f))
	{
		return;
	}
	bVoluntaryExitDeclared = true;
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (AMultiplayTestGameMode* GameMode = World->GetAuthGameMode<AMultiplayTestGameMode>(); IsValid(GameMode))
		{
			GameMode->DeclareVoluntaryExit(this);
		}
	}
}

bool AMultiplayTestPlayerController::ServerDeclareVoluntaryExit_Validate()
{
	return true;
}

void AMultiplayTestPlayerController::ServerCycleSurvivorSpectator_Implementation(const int32 Direction)
{
	if (!AllowServerRequest(4, 4, 1.0f) || Direction == 0)
	{
		return;
	}
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (AMultiplayTestGameMode* GameMode = World->GetAuthGameMode<AMultiplayTestGameMode>(); IsValid(GameMode))
		{
			GameMode->CycleSurvivorSpectator(this, Direction);
		}
	}
}

bool AMultiplayTestPlayerController::ServerCycleSurvivorSpectator_Validate(const int32 Direction)
{
	return Direction == -1 || Direction == 1;
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
