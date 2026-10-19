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
#include "PlayerCharacter.h"
#include "PlayerFeedbackWidgets.h"
#include "PlayerHeartbeatComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SynthComponent.h"
#include "InputKeyEventArgs.h"
#include "EngineUtils.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "ProjectProject01TuningData.h"
#include "UObject/ConstructorHelpers.h"

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
	static ConstructorHelpers::FObjectFinder<UInputAction> Look(TEXT("/Game/MyProject/Input/IA_Player_Look.IA_Player_Look"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> Mapping(TEXT("/Game/MyProject/Input/IMC_PlayerControl.IMC_PlayerControl"));
	MannequinCameraLookAction=Look.Object;MannequinCameraInputContext=Mapping.Object;
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
	InitialGameplayReadyStableSeconds = 0.0f;
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->HoldEntireScreenBlack();
	}
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
		// 역할 RPC, Pawn 복제, 마네킹 시점 복제는 도착 순서가 보장되지 않는다.
		// 어느 알림이 먼저 와도 영구 암전에 빠지지 않도록 준비 완료까지 짧게 재확인한다.
		World->GetTimerManager().SetTimer(
			InitialGameplayReadinessTimer, this,
			&AMultiplayTestPlayerController::TryRevealInitialGameplayFade,
			0.1f, true, 0.1f);
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
	AuthenticatedDisplayName = Previous->AuthenticatedDisplayName;
	AuthenticatedMatchId = Previous->AuthenticatedMatchId;
	AuthenticatedRole = Previous->AuthenticatedRole;
	MatchPhase = Previous->MatchPhase;
	SurvivorState = Previous->SurvivorState;
	bLevelTransitionCarryover = Previous->bLevelTransitionCarryover;
	bSpawnAfterLevelTransition = Previous->bSpawnAfterLevelTransition;
	bLocalLevelTransitionFadeHeld = Previous->bLocalLevelTransitionFadeHeld;
	bHadAuthenticatedSession = Previous->bHadAuthenticatedSession;
	SurvivorSpectatorView = Previous->SurvivorSpectatorView;
}

void AMultiplayTestPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	if (IsLocalController() && bLocalLevelTransitionFadeHeld)
	{
		if (UProjectProject01GameInstance* ProjectGameInstance =
			Cast<UProjectProject01GameInstance>(GetGameInstance()))
		{
			ProjectGameInstance->HoldEntireScreenBlack();
		}
	}
	if (IsLocalController() && bLocalLevelTransitionFadeHeld && IsValid(PlayerCameraManager))
	{
		// 새 맵의 첫 프레임이 노출되지 않도록 카메라가 만들어지는 즉시 완전 암전을 다시 고정한다.
		PlayerCameraManager->StartCameraFade(
			1.0f, 1.0f, 0.0f, FLinearColor::Black, false, true);
	}
	TryCompleteClientArrivalView();
}

void AMultiplayTestPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) return;
	TryCompleteClientArrivalView();
	if (SurvivorSpectatorView.bEnabled) ApplySurvivorSpectatorView();
}

void AMultiplayTestPlayerController::OnRep_SurvivorSpectatorView()
{
	TryCompleteClientArrivalView();
	ApplySurvivorSpectatorView();
}

void AMultiplayTestPlayerController::ApplySurvivorSpectatorView()
{
	if (bLocalLevelTransitionFadeHeld || bClientArrivalViewPending ||
		GetWorldTimerManager().IsTimerActive(SpectatorFadeTimer)) return;
	if (SurvivorSpectatorView.bEnabled)
	{
		bAutoManageActiveCameraTarget=false;
		auto* Target=SurvivorSpectatorView.Target.Get();
		if (IsValid(Target) && Target->GetWorld()==GetWorld() && !Target->IsGameOver() && !Target->IsHidden())
			if (GetViewTarget()!=Target) SetViewTarget(Target);
	}
	else if (SurvivorState==EMultiplayTestSurvivorState::Active)
	{
		bAutoManageActiveCameraTarget=true;
		auto* OwnPawn=Cast<APlayerCharacter>(GetPawn());
		if (IsValid(OwnPawn) && OwnPawn->GetWorld()==GetWorld() && !OwnPawn->IsGameOver() && !OwnPawn->IsHidden())
			if (GetViewTarget()!=OwnPawn) SetViewTarget(OwnPawn);
	}
}

void AMultiplayTestPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveMannequinLookBinding();
	if (IsLocalController())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ServiceStatusTimer);
			World->GetTimerManager().ClearTimer(InitialGameplayRevealTimer);
			World->GetTimerManager().ClearTimer(InitialGameplayReadinessTimer);
			World->GetTimerManager().ClearTimer(MatchGameplayAudioStopTimer);
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
				if(bLocalLevelTransitionFadeHeld && EndPlayReason!=EEndPlayReason::Quit && EndPlayReason!=EEndPlayReason::EndPlayInEditor)Voice->SuspendForLevelTravel();
				else Voice->LeaveMatch();
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
	if (!InRole.Equals(TEXT("Mannequin"),ESearchCase::IgnoreCase)) RemoveMannequinLookBinding();
	if (bAllowSurvivorVoice || InRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
	{
		bLocalMultiplayerRoleKnown = true;
		bLocalMultiplayerMannequinRole = !bAllowSurvivorVoice;
	}
	else
	{
		bLocalMultiplayerRoleKnown = false;
		bLocalMultiplayerMannequinRole = false;
	}
	SetSurvivorVoiceStatusVisible(bAllowSurvivorVoice);
	SetMannequinStatusVisible(InRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase));

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UProjectProject01VoiceChatSubsystem* Voice = GI->GetSubsystem<UProjectProject01VoiceChatSubsystem>())
		{
			Voice->ConfigureForMatch(UserId, MatchId, InRole, this);
		}
	}
	TryRevealInitialGameplayFade();
}

void AMultiplayTestPlayerController::ClientUpdateVoiceAudibility_Implementation(const FString& InMatchId,bool bAllowed,const TArray<FProjectVoiceAudibility>& Peers)
{
	if(IsLocalController() && GetGameInstance())
		if(auto* Voice=GetGameInstance()->GetSubsystem<UProjectProject01VoiceChatSubsystem>())
			Voice->UpdateAuthoritativeAudibility(InMatchId,bAllowed,Peers);
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
	// Client RPC가 BeginPlay보다 먼저 처리되거나 역할 RPC보다 시점/Pawn이 먼저 복제되는 경우도
	// 실제 준비된 액터 종류를 통해 역할을 복구할 수 있다.
	if (!bLocalMultiplayerRoleKnown)
	{
		if (IsValid(GetViewedMannequin()))
		{
			bLocalMultiplayerRoleKnown = true;
			bLocalMultiplayerMannequinRole = true;
		}
		else if (IsValid(Cast<APlayerCharacter>(GetPawn())))
		{
			bLocalMultiplayerRoleKnown = true;
			bLocalMultiplayerMannequinRole = false;
		}
	}
	UWorld* World = GetWorld();
	if (!IsLocalController() || !bInitialGameplayFadePending || bLocalLevelTransitionFadeHeld ||
		bInitialGameplayFadeScheduled || !bLocalMultiplayerRoleKnown ||
		!IsValid(PlayerCameraManager) || !IsValid(World))
	{
		return;
	}

	const bool bViewReady = bLocalMultiplayerMannequinRole
		? IsValid(GetViewedMannequin())
		: IsValid(GetPawn());
	if (!bViewReady)
	{
		InitialGameplayReadyStableSeconds = 0.0f;
		return;
	}

	// Pawn이 생긴 것만으로는 텍스처/서브 레벨/비동기 오브젝트 로딩까지 끝났다고 볼 수 없다.
	// 화면을 검게 유지한 채 실제 플레이 월드가 안정적으로 준비된 뒤에만 페이드 인한다.
	bool bStreamingReady = World->HasBegunPlay() && !IsAsyncLoading();
	if (bStreamingReady)
	{
		for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (!IsValid(StreamingLevel))
			{
				continue;
			}
			if ((StreamingLevel->ShouldBeLoaded() && !StreamingLevel->IsLevelLoaded()) ||
				(StreamingLevel->ShouldBeVisible() && !StreamingLevel->IsLevelVisible()))
			{
				bStreamingReady = false;
				break;
			}
		}
	}
	if (!bStreamingReady)
	{
		InitialGameplayReadyStableSeconds = 0.0f;
		return;
	}

	InitialGameplayReadyStableSeconds += 0.1f;
	if (InitialGameplayReadyStableSeconds < 0.5f)
	{
		return;
	}

	// 역할 RPC가 첫 화면이 실제로 그려지기 전에 도착할 수 있다. 즉시 해제를 시작하면
	// 로딩 중에 페이드 시간이 소모되므로 짧은 검은 화면 고정 뒤 해제를 시작한다.
	PlayerCameraManager->StartCameraFade(
		1.0f, 1.0f, 0.0f, FLinearColor::Black, false, true);
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->HoldEntireScreenBlack();
	}
	bInitialGameplayFadeScheduled = true;
	World->GetTimerManager().ClearTimer(InitialGameplayReadinessTimer);
	World->GetTimerManager().SetTimer(
		InitialGameplayRevealTimer,
		this,
		&AMultiplayTestPlayerController::CompleteInitialGameplayFade,
		0.15f,
		false);
}

void AMultiplayTestPlayerController::CompleteInitialGameplayFade()
{
	bInitialGameplayFadeScheduled = false;
	if (!IsLocalController() || bLocalLevelTransitionFadeHeld || !IsValid(PlayerCameraManager))
	{
		return;
	}
	bInitialGameplayFadePending = false;
	InitialGameplayReadyStableSeconds = 0.0f;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialGameplayReadinessTimer);
	}
	// 전역 오버레이가 완전히 검은 동안 카메라 페이드를 먼저 제거해 중복 알파 합성을 막는다.
	PlayerCameraManager->StopCameraFade();
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->FadeEntireScreenFromBlack(1.5f);
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
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinManualControl);
	InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinPatrolCommand);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinRangeChaseCommand);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinFollowCommand);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AMultiplayTestPlayerController::RequestResetFollowingMannequins);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AMultiplayTestPlayerController::RequestToggleMannequinCatchDamage);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMultiplayTestPlayerController::RequestMannequinAttack);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AMultiplayTestPlayerController::ToggleSessionMenu);
	InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &AMultiplayTestPlayerController::SelectPreviousSpectatorTarget);
	InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &AMultiplayTestPlayerController::SelectNextSpectatorTarget);
	if (GetViewedMannequin()) EnsureMannequinLookBinding();
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

void AMultiplayTestPlayerController::EnsureMannequinLookBinding()
{
	if (!IsLocalController()) return;
	if (auto* Enhanced=Cast<UEnhancedInputComponent>(InputComponent); Enhanced && MannequinCameraLookAction && !MannequinLookBindingHandle)
		MannequinLookBindingHandle=Enhanced->BindAction(MannequinCameraLookAction,ETriggerEvent::Triggered,this,
			&AMultiplayTestPlayerController::HandleMannequinLook).GetHandle();
	if (ULocalPlayer* Local=GetLocalPlayer())
		if (auto* Input=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Local);Input && MannequinCameraInputContext)
			if (!Input->HasMappingContext(MannequinCameraInputContext)) Input->AddMappingContext(MannequinCameraInputContext,0);
}

void AMultiplayTestPlayerController::RemoveMannequinLookBinding()
{
	if (auto* Enhanced=Cast<UEnhancedInputComponent>(InputComponent);Enhanced && MannequinLookBindingHandle)
		Enhanced->RemoveBindingByHandle(MannequinLookBindingHandle);
	MannequinLookBindingHandle=0;
	LocalCameraMannequin.Reset();bCameraWasManual=false;bManualCameraAligning=false;
	// Shared IMC_PlayerControl is also used by survivors; do not remove its mapping context.
}

void AMultiplayTestPlayerController::HandleMannequinLook(const FInputActionValue& Value)
{
	AMannequinAICharacter* Mannequin=GetViewedMannequin();
	if (!IsLocalController() || !Mannequin || bShowMouseCursor || IsLookInputIgnored() ||
		MatchPhase==EMultiplayTestMatchPhase::Ending || MatchPhase==EMultiplayTestMatchPhase::Results ||
		MatchPhase==EMultiplayTestMatchPhase::ReturningToRoom) return;
	ResolveMannequinCameraRotation(Mannequin,0);
	if (bManualCameraAligning) return;
	const FVector2D Delta=Value.Get<FVector2D>();
	const auto* Settings=UProjectProject01GameUserSettings::Get();
	const float Sensitivity=Settings?Settings->GetMouseSensitivity():1;
	AddYawInput(Delta.X*Sensitivity);AddPitchInput(Delta.Y*Sensitivity);
}

FRotator AMultiplayTestPlayerController::ResolveMannequinCameraRotation(AMannequinAICharacter* Mannequin,float DeltaSeconds)
{
	if (!IsLocalController() || !IsValid(Mannequin)) return GetControlRotation();
	if (LocalCameraMannequin.Get()!=Mannequin)
	{
		LocalCameraMannequin=Mannequin;
		LocalMannequinCameraRotation=Mannequin->GetActorRotation();LocalMannequinCameraRotation.Pitch=0;LocalMannequinCameraRotation.Roll=0;
		SetControlRotation(LocalMannequinCameraRotation);RotationInput=FRotator::ZeroRotator;
		bCameraWasManual=false;bManualCameraAligning=false;
	}
	const bool bManual=GetPawn()==Mannequin && Mannequin->IsManualControlEnabled();
	if (bManual && !bCameraWasManual)
	{
		ManualCameraAlignFrom=LocalMannequinCameraRotation;
		ManualCameraAlignTo=Mannequin->GetActorRotation();ManualCameraAlignTo.Pitch=0;ManualCameraAlignTo.Roll=0;
		ManualCameraAlignSeconds=0;bManualCameraAligning=true;
		SetControlRotation(ManualCameraAlignTo);RotationInput=FRotator::ZeroRotator;
	}
	if (!bManual) bManualCameraAligning=false;
	bCameraWasManual=bManual;
	if (bManualCameraAligning)
	{
		ManualCameraAlignSeconds+=FMath::Max(0.0f,DeltaSeconds);
		const float Alpha=FMath::Clamp(ManualCameraAlignSeconds/0.25f,0.0f,1.0f);
		const float Smooth=Alpha*Alpha*(3-2*Alpha);
		LocalMannequinCameraRotation=FQuat::Slerp(ManualCameraAlignFrom.Quaternion(),ManualCameraAlignTo.Quaternion(),Smooth).Rotator();
		if (Alpha>=1)
		{
			bManualCameraAligning=false;
			if (auto* Movement=Mannequin->GetCharacterMovement()) Movement->bUseControllerDesiredRotation=true;
		}
	}
	else LocalMannequinCameraRotation=GetControlRotation();
	return LocalMannequinCameraRotation;
}

AMannequinAICharacter* AMultiplayTestPlayerController::GetViewedMannequin() const
{
	if (IsValid(ViewedMannequin)) return ViewedMannequin.Get();
	return Cast<AMannequinAICharacter>(GetPawn());
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
	AuthenticatedDisplayName = Claim.DisplayName.Left(48);
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
	if (NewSurvivorState==EMultiplayTestSurvivorState::Active || NewSurvivorState==EMultiplayTestSurvivorState::NotApplicable)
		SurvivorSpectatorView={};
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
	SurvivorSpectatorView.bEnabled=true;
	SurvivorSpectatorView.Target=Cast<APlayerCharacter>(InitialTarget);
	bAutoManageActiveCameraTarget=false;
	if (IsValid(SurvivorSpectatorView.Target)) SetViewTarget(SurvivorSpectatorView.Target);
	ForceNetUpdate();
	ClientEnterSurvivorSpectator(InitialTarget, bFadeTransition);
}

void AMultiplayTestPlayerController::PrepareLevelTransitionCarryover(
	const bool bSpawnInNextLevel, const float FadeSeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bLevelTransitionCarryover)
	{
		// A late death/forfeit may revoke permission, but repeated prepare cannot revive or restart the fade.
		bSpawnAfterLevelTransition &= bSpawnInNextLevel;
		return;
	}
	CaptureLevelTransitionState();
	bLevelTransitionCarryover = true;
	bSpawnAfterLevelTransition = bSpawnInNextLevel;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	ClientPrepareForLevelTransition(FadeSeconds);
}

void AMultiplayTestPlayerController::CompleteLevelTransitionCarryover(
	AActor* InitialViewTarget,
	const bool bCanPlay)
{
	if (!HasAuthority() || !bLevelTransitionCarryover)
	{
		return;
	}

	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	SetIgnoreMoveInput(!bCanPlay);
	SurvivorSpectatorView.bEnabled=!bCanPlay;
	SurvivorSpectatorView.Target=bCanPlay ? nullptr : Cast<APlayerCharacter>(InitialViewTarget);
	if (!bCanPlay) bAutoManageActiveCameraTarget=false;
	else if (SurvivorState==EMultiplayTestSurvivorState::Active) bAutoManageActiveCameraTarget=true;
	ForceNetUpdate();
	if (IsValid(InitialViewTarget))
	{
		SetViewTarget(InitialViewTarget);
	}
	ClientCompleteLevelTransition(InitialViewTarget, bCanPlay);
	bLevelTransitionCarryover = false;
	bSpawnAfterLevelTransition = false;
	ServerTransitionSnapshot = {};
}

void AMultiplayTestPlayerController::FTransitionInputSnapshot::Capture(APlayerController* Controller)
{
	if (bCaptured) return;
	bCaptured = true;
	bMoveIgnored = Controller->IsMoveInputIgnored();
	bLookIgnored = Controller->IsLookInputIgnored();
	ViewTarget = Controller->GetViewTarget();
}

void AMultiplayTestPlayerController::FTransitionInputSnapshot::Restore(APlayerController* Controller)
{
	if (!bCaptured) return;
	// Spectating and preparing travel can each add an input lock. A single false
	// only decrements that stack and leaves the survivor permanently blocked.
	Controller->ResetIgnoreMoveInput();
	Controller->ResetIgnoreLookInput();
	Controller->SetIgnoreMoveInput(bMoveIgnored);
	Controller->SetIgnoreLookInput(bLookIgnored);
	if (ViewTarget.IsValid()) Controller->SetViewTarget(ViewTarget.Get());
	bCaptured = false;
}

void AMultiplayTestPlayerController::CaptureLevelTransitionState()
{
	if (!HasAuthority() || ServerTransitionSnapshot.bCaptured) return;
	ServerTransitionSnapshot.Capture(this);
	ClientCaptureLevelTransitionState();
}

void AMultiplayTestPlayerController::ClientCaptureLevelTransitionState_Implementation()
{
	if (IsLocalController()) LocalTransitionSnapshot.Capture(this);
}

void AMultiplayTestPlayerController::CancelLevelTransitionCarryover(const bool bRestoreGameplay)
{
	if (!HasAuthority()) return;
	if (bRestoreGameplay) ServerTransitionSnapshot.Restore(this);
	ServerTransitionSnapshot = {};
	bLevelTransitionCarryover = false;
	bSpawnAfterLevelTransition = false;
	ClientCancelLevelTransition(bRestoreGameplay);
}

void AMultiplayTestPlayerController::ClientCancelLevelTransition_Implementation(const bool bRestoreGameplay)
{
	if (!IsLocalController()) return;
	bClientArrivalViewPending=false;
	ClientArrivalViewTarget.Reset();
	if (!LocalTransitionSnapshot.bCaptured && !bLocalLevelTransitionFadeHeld) return;
	GetWorldTimerManager().ClearTimer(SpectatorFadeTimer);
	bLocalLevelTransitionFadeHeld = false;
	if (bRestoreGameplay)
	{
		LocalTransitionSnapshot.Restore(this);
		if (IsValid(PlayerCameraManager)) PlayerCameraManager->StopCameraFade();
		if (auto* Instance = GetGameInstance<UProjectProject01GameInstance>()) Instance->FadeEntireScreenFromBlack(0.6f);
	}
	LocalTransitionSnapshot = {};
}

void AMultiplayTestPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	if (Cast<APlayerCharacter>(GetPawn())) RemoveMannequinLookBinding();
	else ApplyViewedMannequinCamera(GetViewedMannequin());
	TryRevealInitialGameplayFade();
	TryCompleteClientArrivalView();
	ApplySurvivorSpectatorView();
}

void AMultiplayTestPlayerController::ClientApplyViewedMannequin_Implementation(AMannequinAICharacter* Mannequin)
{
	// Owner RPC and owner-only property replication can arrive in either order.
	if (IsValid(Mannequin)) ViewedMannequin=Mannequin;
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
	bClientArrivalViewPending=false;
	ClientArrivalViewTarget.Reset();
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		// 결과 화면에서는 UI 음량은 유지하되 심박·발소리·신음 등 게임플레이 SFX만 완전히 내린다.
		Settings->FadeGameplaySFX(0.0f, 1.5f);
	}
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(MatchGameplayAudioStopTimer);
		World->GetTimerManager().SetTimer(
			MatchGameplayAudioStopTimer, this,
			&AMultiplayTestPlayerController::StopMatchGameplayAudio,
			1.5f, false);
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
		UE_LOG(LogTemp, Log, TEXT("Updated multiplayer result UI. Outcome=%s Success=%s."),
			*Result.Outcome, Result.bSuccess ? TEXT("true") : TEXT("false"));
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
		// 피격/사망 암전 위젯(최대 900)보다 항상 위에 두어 최종 결과가 가려지지 않게 한다.
		MatchResultWidget->AddToViewport(1200);
		UE_LOG(LogTemp, Log, TEXT("Presented multiplayer result UI. Outcome=%s Success=%s."),
			*Result.Outcome, Result.bSuccess ? TEXT("true") : TEXT("false"));
	}
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(MatchResultWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

void AMultiplayTestPlayerController::StopMatchGameplayAudio()
{
	if (!IsLocalController() || !IsValid(GetWorld()))
	{
		return;
	}

	auto StopActorSynths = [](AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return;
		}
		TInlineComponentArray<USynthComponent*> SynthComponents;
		Actor->GetComponents(SynthComponents);
		for (USynthComponent* Synth : SynthComponents)
		{
			if (IsValid(Synth) && Synth->IsPlaying())
			{
				Synth->Stop();
			}
		}
	};

	for (TActorIterator<APlayerCharacter> It(GetWorld()); It; ++It)
	{
		APlayerCharacter* Survivor = *It;
		if (!IsValid(Survivor))
		{
			continue;
		}
		if (UPlayerHeartbeatComponent* Heartbeat =
			Survivor->FindComponentByClass<UPlayerHeartbeatComponent>(); IsValid(Heartbeat))
		{
			Heartbeat->ResetHeartbeatState();
			Heartbeat->SetComponentTickEnabled(false);
		}
		StopActorSynths(Survivor);
	}
	for (TActorIterator<AMannequinAICharacter> It(GetWorld()); It; ++It)
	{
		StopActorSynths(*It);
	}
	if (IsValid(MannequinCaptureSFXComponent) && MannequinCaptureSFXComponent->IsPlaying())
	{
		MannequinCaptureSFXComponent->Stop();
	}
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
	SurvivorSpectatorView.bEnabled=true;
	InitialTarget=Cast<APlayerCharacter>(InitialTarget);
	SurvivorSpectatorView.Target=Cast<APlayerCharacter>(InitialTarget);
	bAutoManageActiveCameraTarget=false;
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	if (!bFadeTransition)
	{
		if (IsValid(InitialTarget))
		{
			SetViewTarget(InitialTarget);
			if (!bLocalLevelTransitionFadeHeld && IsValid(PlayerCameraManager)) PlayerCameraManager->StopCameraFade();
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
	GetWorldTimerManager().ClearTimer(SpectatorFadeTimer);
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

void AMultiplayTestPlayerController::ClientPrepareForLevelTransition_Implementation(const float FadeSeconds)
{
	if (!IsLocalController())
	{
		return;
	}

	bInitialGameplayFadePending = false;
	bInitialGameplayFadeScheduled = false;
	bClientArrivalViewPending=false;
	ClientArrivalViewTarget.Reset();
	bLocalLevelTransitionFadeHeld = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialGameplayRevealTimer);
		World->GetTimerManager().ClearTimer(SpectatorFadeTimer);
	}
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	if (IsValid(SessionMenuWidget))
	{
		SessionMenuWidget->RemoveFromParent();
		SessionMenuWidget = nullptr;
	}
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->FadeEntireScreenToBlack(FMath::Clamp(FadeSeconds,0.1f,10.0f));
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

	bClientArrivalViewPending=true;
	bClientArrivalCanPlay=bCanPlay;
	ClientArrivalViewTarget=InitialViewTarget;
	SurvivorSpectatorView.bEnabled=!bCanPlay;
	if (bCanPlay) SurvivorSpectatorView.Target=nullptr;
	else if (auto* Target=Cast<APlayerCharacter>(InitialViewTarget)) SurvivorSpectatorView.Target=Target;
	else if (SurvivorSpectatorView.Target && SurvivorSpectatorView.Target->GetWorld()!=GetWorld()) SurvivorSpectatorView.Target=nullptr;
	TryCompleteClientArrivalView();
}

void AMultiplayTestPlayerController::TryCompleteClientArrivalView()
{
	if (!IsLocalController() || !bClientArrivalViewPending) return;
	AActor* ReadyTarget=nullptr;
	if (!bClientArrivalCanPlay) ReadyTarget=SurvivorSpectatorView.Target;
	else if (bLocalMultiplayerMannequinRole)
	{
		ReadyTarget=GetViewedMannequin();
		if (!IsValid(ReadyTarget)) ReadyTarget=ClientArrivalViewTarget.Get();
	}
	else ReadyTarget=GetPawn();
	if (!IsValid(ReadyTarget) || ReadyTarget->GetWorld()!=GetWorld() || ReadyTarget->IsHidden()) return;
	if (const auto* Survivor=Cast<APlayerCharacter>(ReadyTarget); Survivor && Survivor->IsGameOver()) return;
	// Never expose a default/spectator camera while the new Pawn reference is still unmapped.
	if (bClientArrivalCanPlay && !bLocalMultiplayerMannequinRole && !ReadyTarget->IsA<APlayerCharacter>()) return;
	if (bClientArrivalCanPlay) SurvivorSpectatorView={};
	bClientArrivalViewPending=false;
	ClientArrivalViewTarget.Reset();
	bAutoManageActiveCameraTarget=bClientArrivalCanPlay && !bLocalMultiplayerMannequinRole;
	SetViewTarget(ReadyTarget);
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	SetIgnoreMoveInput(!bClientArrivalCanPlay);
	LocalTransitionSnapshot = {};
	GetWorldTimerManager().ClearTimer(SpectatorFadeTimer);
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	bInitialGameplayFadePending = false;
	bLocalLevelTransitionFadeHeld = false;
	if (IsValid(PlayerCameraManager))
	{
		// 전역 오버레이가 새 맵을 가린 상태에서 기존 카메라 검정만 먼저 제거한다.
		PlayerCameraManager->StopCameraFade();
	}
	if (UProjectProject01GameInstance* ProjectGameInstance =
		Cast<UProjectProject01GameInstance>(GetGameInstance()))
	{
		ProjectGameInstance->FadeEntireScreenFromBlack(0.6f);
	}
}

void AMultiplayTestPlayerController::OnRep_ViewedMannequin()
{
	ApplyViewedMannequinCamera(GetViewedMannequin());
	TryRevealInitialGameplayFade();
	TryCompleteClientArrivalView();
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
	EnsureMannequinLookBinding();
	ResolveMannequinCameraRotation(Mannequin,0);

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

void AMultiplayTestPlayerController::RequestMannequinAttack()
{
	if (IsLocalController() && !bShowMouseCursor && SurvivorState == EMultiplayTestSurvivorState::NotApplicable)
		ServerRequestMannequinAttack();
}

void AMultiplayTestPlayerController::ServerRequestMannequinAttack_Implementation()
{
	if (!AllowServerRequest(9, 3, 1.0f)) return;
	if (AMultiplayTestGameMode* Mode = GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>(); IsValid(Mode))
		Mode->TryMannequinAttack(this);
}

bool AMultiplayTestPlayerController::ServerRequestMannequinAttack_Validate() { return true; }

bool AMultiplayTestPlayerController::ServerToggleMannequinCatchDamage_Validate()
{
	return true;
}

void AMultiplayTestPlayerController::ClientNotifyMannequinCatchDamage_Implementation(const bool bEnabled)
{
	ClientMessage(bEnabled
		? TEXT("전체 AI 마네킹의 데스카운트 소모: 활성")
		: TEXT("전체 AI 마네킹의 데스카운트 소모: 비활성"));
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
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, SurvivorSpectatorView, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, bManualMannequinCommandIssued, COND_OwnerOnly);
}
