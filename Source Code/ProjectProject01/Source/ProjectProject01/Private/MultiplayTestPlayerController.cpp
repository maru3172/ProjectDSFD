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
#include "PlayerFeedbackWidgets.h"
#include "PlayerHeartbeatComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "InputKeyEventArgs.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace ProjectProject01SurvivorVoice
{
	bool IsAllowedRole(const FString& Role)
	{
		return Role.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01SurvivorVoiceRoleIsolationTest,
	"ProjectProject01.Multiplayer.SurvivorVoice.RoleIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01SurvivorVoiceRoleIsolationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Survivor role joins survivor voice"),
		ProjectProject01SurvivorVoice::IsAllowedRole(TEXT("Survivor")));
	TestTrue(TEXT("Role check remains case-insensitive"),
		ProjectProject01SurvivorVoice::IsAllowedRole(TEXT("survivor")));
	TestFalse(TEXT("Mannequin role cannot join or see survivor voice UI"),
		ProjectProject01SurvivorVoice::IsAllowedRole(TEXT("Mannequin")));
	TestFalse(TEXT("Disabled communication cannot join survivor voice"),
		ProjectProject01SurvivorVoice::IsAllowedRole(TEXT("Disabled")));
	return true;
}
#endif

AMultiplayTestPlayerController::AMultiplayTestPlayerController()
{
	MannequinCaptureSFXComponent = CreateDefaultSubobject<UMannequinCaptureSynthComponent>(
		TEXT("Mannequin Capture SFX Component"));
}

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
	bInitialGameplayFadePending = true;
	bLocalMultiplayerRoleKnown = false;
	bLocalMultiplayerMannequinRole = false;
	if (IsValid(PlayerCameraManager))
	{
		// 새 경기의 첫 프레임부터 완전한 검은 화면을 유지한다. 생존자 Pawn 또는
		// 마네킹 시점이 준비된 뒤 TryRevealInitialGameplayFade에서만 해제한다.
		PlayerCameraManager->StartCameraFade(
			1.0f, 1.0f, 0.0f, FLinearColor::Black, false, true);
	}
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		Settings->RestoreGameplaySFX();
		if (IsValid(MannequinCaptureSFXComponent))
		{
			MannequinCaptureSFXComponent->SoundClass = Settings->GetSFXSoundClass();
		}
	}

	ServiceNoticeWidget = CreateWidget<UProjectProject01ServiceNoticeWidget>(
		this, UProjectProject01ServiceNoticeWidget::StaticClass());
	if (IsValid(ServiceNoticeWidget))
	{
		ServiceNoticeWidget->AddToViewport(950);
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

void AMultiplayTestPlayerController::SeamlessTravelFrom(APlayerController* OldPC)
{
	Super::SeamlessTravelFrom(OldPC);

	const AMultiplayTestPlayerController* Previous = Cast<AMultiplayTestPlayerController>(OldPC);
	if (!IsValid(Previous))
	{
		return;
	}

	AuthenticatedUserId = Previous->AuthenticatedUserId;
	AuthenticatedMatchId = Previous->AuthenticatedMatchId;
	AuthenticatedRole = Previous->AuthenticatedRole;
	MatchPhase = Previous->MatchPhase;
	SurvivorState = Previous->SurvivorState;
	bLevelTransitionCarryover = Previous->bLevelTransitionCarryover;
	bSpawnAfterLevelTransition = Previous->bSpawnAfterLevelTransition;
	bLocalLevelTransitionFadeHeld = Previous->bLocalLevelTransitionFadeHeld;
	bHadAuthenticatedSession = Previous->bHadAuthenticatedSession;
}

void AMultiplayTestPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	if (IsLocalController() && bLocalLevelTransitionFadeHeld && IsValid(PlayerCameraManager))
	{
		// 새 맵의 첫 프레임이 노출되지 않도록 카메라가 만들어지는 즉시 완전 암전을 다시 고정한다.
		PlayerCameraManager->StartCameraFade(
			1.0f, 1.0f, 0.0f, FLinearColor::Black, false, true);
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
		if (IsValid(MannequinStatusWidget))
		{
			MannequinStatusWidget->RemoveFromParent();
			MannequinStatusWidget = nullptr;
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
		Notice += FString::Printf(TEXT(" (점검 자동 해제 예정: %s)"), *ShutdownAtUtc);
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
						if (Voice->IsConfiguredForSurvivor())
						{
							if (Params.Event == IE_Pressed) Voice->SetPushToTalkHeld(true);
							else if (Params.Event == IE_Released) Voice->SetPushToTalkHeld(false);
						}
					}
				}
			}
			else if (Params.Event == IE_Pressed && Params.Key == Settings->GetVoiceToggleKey())
			{
				if (UGameInstance* GI = GetGameInstance())
				{
					if (UProjectProject01VoiceChatSubsystem* Voice = GI->GetSubsystem<UProjectProject01VoiceChatSubsystem>();
						IsValid(Voice) && Voice->IsConfiguredForSurvivor())
					{
						Voice->ToggleOpenMic();
						return true;
					}
				}
			}
			else if (SurvivorState == EMultiplayTestSurvivorState::Active &&
				Params.Event == IE_Pressed && Params.Key == Settings->GetHelpPingKey())
			{
				RequestTeamPing(EProjectProject01PingType::Help); return true;
			}
			else if (SurvivorState == EMultiplayTestSurvivorState::Active &&
				Params.Event == IE_Pressed && Params.Key == Settings->GetDangerPingKey())
			{
				RequestTeamPing(EProjectProject01PingType::Danger); return true;
			}
			else if (SurvivorState == EMultiplayTestSurvivorState::Active &&
				Params.Event == IE_Pressed && Params.Key == Settings->GetLocationPingKey())
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
	const bool bAllowSurvivorVoice = ProjectProject01SurvivorVoice::IsAllowedRole(InRole);
	SetSurvivorVoiceStatusVisible(bAllowSurvivorVoice);
	SetMannequinStatusVisible(InRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase));
	if (bAllowSurvivorVoice || InRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
	{
		bLocalMultiplayerRoleKnown = true;
		bLocalMultiplayerMannequinRole = !bAllowSurvivorVoice;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UProjectProject01VoiceChatSubsystem* Voice = GI->GetSubsystem<UProjectProject01VoiceChatSubsystem>())
		{
			Voice->ConfigureForMatch(UserId, MatchId, InRole, this);
		}
	}
	TryRevealInitialGameplayFade();
}

void AMultiplayTestPlayerController::SetSurvivorVoiceStatusVisible(const bool bVisible)
{
	if (!IsLocalController())
	{
		return;
	}

	if (!bVisible)
	{
		if (IsValid(VoiceStatusWidget))
		{
			VoiceStatusWidget->RemoveFromParent();
			VoiceStatusWidget = nullptr;
		}
		return;
	}

	if (!IsValid(VoiceStatusWidget))
	{
		VoiceStatusWidget = CreateWidget<UProjectProject01VoiceStatusWidget>(
			this, UProjectProject01VoiceStatusWidget::StaticClass());
	}
	if (IsValid(VoiceStatusWidget) && !VoiceStatusWidget->IsInViewport())
	{
		VoiceStatusWidget->AddToViewport(940);
	}
}

void AMultiplayTestPlayerController::SetMannequinStatusVisible(const bool bVisible)
{
	if (!IsLocalController())
	{
		return;
	}
	if (!bVisible)
	{
		if (IsValid(MannequinStatusWidget))
		{
			MannequinStatusWidget->RemoveFromParent();
			MannequinStatusWidget = nullptr;
		}
		return;
	}
	if (!IsValid(MannequinStatusWidget))
	{
		MannequinStatusWidget = CreateWidget<UProjectProject01MannequinStatusWidget>(
			this, UProjectProject01MannequinStatusWidget::StaticClass());
	}
	if (IsValid(MannequinStatusWidget) && !MannequinStatusWidget->IsInViewport())
	{
		MannequinStatusWidget->AddToViewport(939);
	}
}

void AMultiplayTestPlayerController::TryRevealInitialGameplayFade()
{
	if (!IsLocalController() || !bInitialGameplayFadePending || bLocalLevelTransitionFadeHeld ||
		!bLocalMultiplayerRoleKnown || !IsValid(PlayerCameraManager))
	{
		return;
	}

	const bool bViewReady = bLocalMultiplayerMannequinRole
		? IsValid(GetViewedMannequin())
		: IsValid(GetPawn());
	if (!bViewReady)
	{
		return;
	}

	bInitialGameplayFadePending = false;
	PlayerCameraManager->StartCameraFade(
		1.0f, 0.0f, 1.5f, FLinearColor::Black, false, false);
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
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinManualControl);
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinPatrolCommand);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinRangeChaseCommand);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinFollowCommand);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AMultiplayTestPlayerController::RequestResetFollowingMannequins);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AMultiplayTestPlayerController::RequestToggleMannequinCatchDamage);
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

void AMultiplayTestPlayerController::PrepareLevelTransitionCarryover(
	const bool bSpawnInNextLevel)
{
	if (!HasAuthority())
	{
		return;
	}

	bLevelTransitionCarryover = true;
	bSpawnAfterLevelTransition = bSpawnInNextLevel;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	ClientPrepareForLevelTransition();
}

void AMultiplayTestPlayerController::CompleteLevelTransitionCarryover(
	AActor* InitialViewTarget,
	const bool bCanPlay)
{
	if (!HasAuthority() || !bLevelTransitionCarryover)
	{
		return;
	}

	SetIgnoreMoveInput(!bCanPlay);
	SetIgnoreLookInput(false);
	if (IsValid(InitialViewTarget))
	{
		SetViewTarget(InitialViewTarget);
	}
	ClientCompleteLevelTransition(InitialViewTarget, bCanPlay);
	bLevelTransitionCarryover = false;
	bSpawnAfterLevelTransition = false;
}

void AMultiplayTestPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	ApplyViewedMannequinCamera(GetViewedMannequin());
	TryRevealInitialGameplayFade();
}

void AMultiplayTestPlayerController::ClientApplyViewedMannequin_Implementation(AMannequinAICharacter* Mannequin)
{
	ApplyViewedMannequinCamera(Mannequin);
	TryRevealInitialGameplayFade();
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
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		Settings->FadeGameplaySFX(0.12f, 1.5f);
	}
	if (IsValid(SessionMenuWidget))
	{
		SessionMenuWidget->RemoveFromParent();
		SessionMenuWidget = nullptr;
	}
	SetSurvivorVoiceStatusVisible(false);
	SetMannequinStatusVisible(false);
	if (IsValid(MatchResultWidget) && MatchResultWidget->IsInViewport())
	{
		// 백엔드 기록 검증이 끝난 최종 결과는 이미 열린 화면만 갱신한다.
		// 화면을 다시 만들면 암전과 입력 포커스가 두 번 시작되어 보이는 문제를 막는다.
		MatchResultWidget->ConfigureResult(Result);
	}
	else
	{
		MatchResultWidget = CreateWidget<UProjectProject01MatchResultWidget>(
			this, UProjectProject01MatchResultWidget::StaticClass());
		if (!ensureMsgf(IsValid(MatchResultWidget), TEXT("Unable to create the multiplayer match result widget.")))
		{
			return;
		}
		MatchResultWidget->ConfigureResult(Result);
		MatchResultWidget->AddToViewport(700);
	}
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

void AMultiplayTestPlayerController::ClientPrepareForLevelTransition_Implementation()
{
	if (!IsLocalController())
	{
		return;
	}

	bInitialGameplayFadePending = false;
	bLocalLevelTransitionFadeHeld = true;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	if (IsValid(SessionMenuWidget))
	{
		SessionMenuWidget->RemoveFromParent();
		SessionMenuWidget = nullptr;
	}
	if (IsValid(PlayerCameraManager))
	{
		PlayerCameraManager->StartCameraFade(
			0.0f, 1.0f, 0.5f, FLinearColor::Black, false, true);
	}
}

void AMultiplayTestPlayerController::ClientCompleteLevelTransition_Implementation(
	AActor* InitialViewTarget,
	const bool bCanPlay)
{
	if (!IsLocalController())
	{
		return;
	}

	if (IsValid(InitialViewTarget))
	{
		SetViewTarget(InitialViewTarget);
	}
	SetIgnoreMoveInput(!bCanPlay);
	SetIgnoreLookInput(false);
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	bInitialGameplayFadePending = false;
	bLocalLevelTransitionFadeHeld = false;
	if (IsValid(PlayerCameraManager))
	{
		// 새 맵에서 Pawn과 관전 대상이 준비된 다음에만 검은 화면을 걷어낸다.
		PlayerCameraManager->StartCameraFade(
			1.0f, 0.0f, 0.6f, FLinearColor::Black, false, false);
	}
}

void AMultiplayTestPlayerController::OnRep_ViewedMannequin()
{
	ApplyViewedMannequinCamera(GetViewedMannequin());
	TryRevealInitialGameplayFade();
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

	// 이 로컬 컨트롤러가 마네킹 역할임이 확정된 시점에 모든 번호 표기를 한 번 갱신한다.
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It)
	{
		It->RefreshControlSlotLabelVisibility();
	}

	// 마네킹 역할 가이드는 생존자 가이드와 별도로 최초 한 번만 자동 표시한다.
	bool bShouldShowMannequinGuide = false;
#if WITH_EDITOR
	UProjectProject01GameUserSettings* EditorSettings = UProjectProject01GameUserSettings::Get();
	// PIE만 로컬 테스트 상태를 사용하며, 패키징된 클라이언트는 아래 계정 상태만 사용한다.
	bShouldShowMannequinGuide = IsValid(EditorSettings) &&
		(!EditorSettings->HasSeenMannequinGuide() ||
			(GetWorld() && GetWorld()->WorldType == EWorldType::PIE));
#else
	UProjectProject01AuthSubsystem* Auth = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	bShouldShowMannequinGuide = IsValid(Auth) && Auth->IsSignedIn() &&
		!Auth->HasSeenMannequinGuideForAccount();
#endif
	if (bShouldShowMannequinGuide && !bRoleGuideShownForThisController)
	{
		UProjectProject01GameGuideWidget* Guide = CreateWidget<UProjectProject01GameGuideWidget>(
			this, UProjectProject01GameGuideWidget::StaticClass());
		if (ensureMsgf(IsValid(Guide), TEXT("Could not create the first mannequin guide.")))
		{
			bRoleGuideShownForThisController = true;
#if WITH_EDITOR
			EditorSettings->MarkMannequinGuideSeen();
#else
			Auth->MarkRoleGuideSeenForAccount(true);
#endif
			Guide->Configure(true, true, false);
			Guide->AddToViewport(700);
			bShowMouseCursor = true;
			FInputModeGameAndUI GuideInput;
			GuideInput.SetWidgetToFocus(Guide->TakeWidget());
			GuideInput.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			GuideInput.SetHideCursorDuringCapture(false);
			SetInputMode(GuideInput);
		}
	}
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
	if (IsLocalController() && SurvivorState == EMultiplayTestSurvivorState::NotApplicable)
	{
		ServerRequestMannequinManualControl();
	}
}

void AMultiplayTestPlayerController::RequestMannequinPatrolCommand()
{
	if (IsLocalController() && SurvivorState == EMultiplayTestSurvivorState::NotApplicable) ServerRequestMannequinCommand(EMultiplayMannequinCommand::Patrol);
}

void AMultiplayTestPlayerController::RequestMannequinRangeChaseCommand()
{
	if (IsLocalController() && SurvivorState == EMultiplayTestSurvivorState::NotApplicable) ServerRequestMannequinCommand(EMultiplayMannequinCommand::ChaseInDetectionRange);
}

void AMultiplayTestPlayerController::RequestMannequinFollowCommand()
{
	if (IsLocalController() && SurvivorState == EMultiplayTestSurvivorState::NotApplicable) ServerRequestMannequinCommand(EMultiplayMannequinCommand::FollowMannequinPlayer);
}

void AMultiplayTestPlayerController::RequestResetFollowingMannequins()
{
	if (IsLocalController() && SurvivorState == EMultiplayTestSurvivorState::NotApplicable) ServerResetFollowingMannequins();
}

void AMultiplayTestPlayerController::RequestToggleMannequinCatchDamage()
{
	if (IsLocalController() && SurvivorState == EMultiplayTestSurvivorState::NotApplicable) ServerToggleMannequinCatchDamage();
}

void AMultiplayTestPlayerController::SelectPreviousSpectatorTarget()
{
	if (IsLocalController() &&
		(SurvivorState == EMultiplayTestSurvivorState::Eliminated ||
		 SurvivorState == EMultiplayTestSurvivorState::Escaped ||
		 SurvivorState == EMultiplayTestSurvivorState::TransitionWaiting))
	{
		ServerCycleSurvivorSpectator(-1);
	}
}

void AMultiplayTestPlayerController::SelectNextSpectatorTarget()
{
	if (IsLocalController() &&
		(SurvivorState == EMultiplayTestSurvivorState::Eliminated ||
		 SurvivorState == EMultiplayTestSurvivorState::Escaped ||
		 SurvivorState == EMultiplayTestSurvivorState::TransitionWaiting))
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

void AMultiplayTestPlayerController::ServerRequestMannequinCommand_Implementation(
	const EMultiplayMannequinCommand Command)
{
	if (!AllowServerRequest(6, 8, 1.0f)) return;
	if (AMultiplayTestGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>() : nullptr; IsValid(GameMode))
	{
		GameMode->TryIssueMannequinCommand(this, Command);
	}
}

bool AMultiplayTestPlayerController::ServerRequestMannequinCommand_Validate(
	const EMultiplayMannequinCommand Command)
{
	return Command == EMultiplayMannequinCommand::Patrol ||
		Command == EMultiplayMannequinCommand::ChaseInDetectionRange ||
		Command == EMultiplayMannequinCommand::FollowMannequinPlayer;
}

void AMultiplayTestPlayerController::ServerResetFollowingMannequins_Implementation()
{
	if (!AllowServerRequest(7, 3, 1.0f)) return;
	if (AMultiplayTestGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>() : nullptr; IsValid(GameMode))
	{
		GameMode->TryResetFollowingMannequins(this);
	}
}

bool AMultiplayTestPlayerController::ServerResetFollowingMannequins_Validate()
{
	return true;
}

void AMultiplayTestPlayerController::ServerToggleMannequinCatchDamage_Implementation()
{
	if (!AllowServerRequest(8, 2, 1.0f)) return;
	if (AMultiplayTestGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>() : nullptr; IsValid(GameMode))
	{
		ClientNotifyMannequinCatchDamage(GameMode->ToggleMannequinCatchDamage(this));
	}
}

bool AMultiplayTestPlayerController::ServerToggleMannequinCatchDamage_Validate()
{
	return true;
}

void AMultiplayTestPlayerController::ClientNotifyMannequinCatchDamage_Implementation(const bool bEnabled)
{
	ClientMessage(bEnabled
		? TEXT("Mannequin catch damage: ON")
		: TEXT("Mannequin catch damage: OFF"));
}

void AMultiplayTestPlayerController::ClientPlayMannequinCaptureFeedback_Implementation(
	const bool bElimination)
{
	if (!IsLocalController())
	{
		return;
	}

	float VFXScale = 1.0f;
	float ShakeScale = 1.0f;
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		VFXScale = Settings->GetLocalFlashScale();
		ShakeScale = Settings->GetScreenShakeScale();
		if (IsValid(MannequinCaptureSFXComponent))
		{
			MannequinCaptureSFXComponent->SoundClass = Settings->GetSFXSoundClass();
		}
	}
	if (IsValid(MannequinCaptureSFXComponent))
	{
		MannequinCaptureSFXComponent->SetVolumeMultiplier(bElimination ? 1.0f : 0.82f);
		MannequinCaptureSFXComponent->TriggerImpact(bElimination);
	}

	if (IsValid(MannequinCaptureFeedbackWidget))
	{
		MannequinCaptureFeedbackWidget->RemoveFromParent();
	}
	MannequinCaptureFeedbackWidget = CreateWidget<UProjectProject01MannequinCaptureFeedbackWidget>(
		this, UProjectProject01MannequinCaptureFeedbackWidget::StaticClass());
	if (IsValid(MannequinCaptureFeedbackWidget))
	{
		MannequinCaptureFeedbackWidget->Configure(bElimination, VFXScale);
		MannequinCaptureFeedbackWidget->AddToViewport(875);
	}

	if (IsValid(PlayerCameraManager) && ShakeScale > UE_KINDA_SMALL_NUMBER)
	{
		if (!IsValid(MannequinCaptureCameraModifier))
		{
			MannequinCaptureCameraModifier = Cast<UProjectProject01MannequinCaptureCameraModifier>(
				PlayerCameraManager->FindCameraModifierByClass(
					UProjectProject01MannequinCaptureCameraModifier::StaticClass()));
			if (!IsValid(MannequinCaptureCameraModifier))
			{
				MannequinCaptureCameraModifier = Cast<UProjectProject01MannequinCaptureCameraModifier>(
					PlayerCameraManager->AddNewCameraModifier(
						UProjectProject01MannequinCaptureCameraModifier::StaticClass()));
			}
		}
		if (IsValid(MannequinCaptureCameraModifier))
		{
			MannequinCaptureCameraModifier->PlayCaptureShake(
				bElimination ? 0.24f : 0.18f,
				(bElimination ? 7.0f : 5.2f) * ShakeScale);
		}
	}
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
