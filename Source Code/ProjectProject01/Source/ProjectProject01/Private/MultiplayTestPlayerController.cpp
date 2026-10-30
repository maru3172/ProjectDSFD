// File: Source/ProjectProject01/Private/MultiplayTestPlayerController.cpp
// Build target: ProjectProject01Server / ProjectProject01

#include "MultiplayTestPlayerController.h"
#include "ProjectInstantReplay.h"

#include "InputCoreTypes.h"
#include "MannequinAICharacter.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01Tutorial.h"
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
#include "ProjectSpectatorCameraManager.h"
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

bool AMultiplayTestPlayerController::IsLocalController()const
{return !bServerControlledBot&&Super::IsLocalController();}

AMultiplayTestPlayerController::AMultiplayTestPlayerController()
{
	PlayerCameraManagerClass=AProjectSpectatorCameraManager::StaticClass();
	CreateDefaultSubobject<UProjectProject01ObjectiveComponent>(TEXT("MultiplayerObjective"));
	static ConstructorHelpers::FObjectFinder<UInputAction> Look(TEXT("/Game/MyProject/Input/IA_Player_Look.IA_Player_Look"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> Mapping(TEXT("/Game/MyProject/Input/IMC_PlayerControl.IMC_PlayerControl"));
	MannequinCameraLookAction=Look.Object;MannequinCameraInputContext=Mapping.Object;
	MannequinCaptureSFXComponent = CreateDefaultSubobject<UMannequinCaptureSynthComponent>(
		TEXT("Mannequin Capture SFX Component"));
}

void AMultiplayTestPlayerController::BeginPlay()
{
	Super::BeginPlay();
    InitializeLocalPresentation();
}
void AMultiplayTestPlayerController::ReceivedPlayer()
{
    Super::ReceivedPlayer();
    if(HasActorBegunPlay())InitializeLocalPresentation();
}
void AMultiplayTestPlayerController::InitializeLocalPresentation()
{
    if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
    // Seamless travel creates the new controller before transferring its UPlayer.
    // Build local UI only once a real viewport owner is attached, including late client arrival.
	if (!IsLocalController()||!GetLocalPlayer()||bLocalPresentationInitialized)
	{
		return;
	}
    bLocalPresentationInitialized=true;

	// 로비의 UIOnly 입력 상태가 클라이언트 트래블 뒤에도 남지 않도록 게임 입력을 명시적으로 복구한다.
	if(GetNetMode()!=NM_Standalone && !AProjectProject01TutorialGameMode::IsTutorialWorld(GetWorld()) && LoadingGateEpoch.IsEmpty())bLoadingGateRequired=true;
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
	bServerControlledBot = Previous->bServerControlledBot;
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
	SpectatorRequestSerial=Previous->SpectatorRequestSerial;
	LastSpectatorFadeRequest=Previous->LastSpectatorFadeRequest;
	TransitionFadeSerial=Previous->TransitionFadeSerial;
	LocalTransitionFadeSerial=Previous->LocalTransitionFadeSerial;
	bServerTransitionBlackReady=Previous->bServerTransitionBlackReady;
	bLocalTransitionBlackConfirmed=Previous->bLocalTransitionBlackConfirmed;
	ClientDepartureWorld=Previous->ClientDepartureWorld;
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
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
	if (!IsLocalController() || bLocalMatchResultRequested) return;
    if(bSendObserverView&&GetWorld()->GetTimeSeconds()>=NextObserverViewAt)
    {NextObserverViewAt=GetWorld()->GetTimeSeconds()+.1;ServerObserverViewRotation(GetControlRotation());}
	TryConfirmLevelTransitionBlack();
	if(bLoadingGateRequired && !LoadingGateEpoch.IsEmpty())
		if(auto* GI=GetGameInstance())if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())
		{
			const uint8 Stage=Loading->GetLocalProgress();
			if(Stage>LastReportedLoadingStage){LastReportedLoadingStage=Stage;ServerReportLoadingStage(LoadingGateEpoch,Stage);}
		}
	TryCompleteClientArrivalView(DeltaTime);
	TryFinishSpectatorFade();
	if (SurvivorSpectatorView.bEnabled) ApplySurvivorSpectatorView();
}

void AMultiplayTestPlayerController::OnRep_SurvivorSpectatorView()
{
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
	if (bLocalMatchResultRequested || MatchPhase==EMultiplayTestMatchPhase::Ending || MatchPhase==EMultiplayTestMatchPhase::Results) return;
	// Keep the server's view target for network relevancy, but let this owner
	// control the visible handoff. Implicit ClientSetViewTarget must not jump early.
	if (IsLocalController() && PlayerCameraManager)
		PlayerCameraManager->bClientSimulatingViewTarget=SurvivorSpectatorView.bEnabled;
	// Property replication can beat the reliable RPC. Both paths start the same fade only once.
	if (SurvivorSpectatorView.bEnabled && SurvivorSpectatorView.RequestId != LastSpectatorFadeRequest)
	{
		LastSpectatorFadeRequest = SurvivorSpectatorView.RequestId;
		const auto* Previous=Cast<APlayerCharacter>(GetViewTarget());
		const auto* OwnSurvivor=Cast<APlayerCharacter>(GetPawn());
		const bool bDeathHandoff=(Previous && Previous->HasLocalDeathBlackout(this)) || (OwnSurvivor && OwnSurvivor->HasLocalDeathBlackout(this));
		if ((SurvivorSpectatorView.bFadeTransition || bDeathHandoff) && IsValid(SurvivorSpectatorView.Target) &&
			!bLocalLevelTransitionFadeHeld && !bClientArrivalViewPending)
		{
			bSpectatorFadePending = true;
			SpectatorSwitchReadyAt = GetWorld()->GetTimeSeconds() + 0.5;
			if (auto* GI = GetGameInstance<UProjectProject01GameInstance>()) GI->FadeEntireScreenToBlack(0.5f);
			else if (PlayerCameraManager) PlayerCameraManager->StartCameraFade(0,1,0.5f,FLinearColor::Black,false,true);
			GetWorldTimerManager().SetTimer(SpectatorFadeTimer,this,&ThisClass::TryFinishSpectatorFade,0.5f,false);
		}
	}
	TryCompleteClientArrivalView();
	ApplySurvivorSpectatorView();
}

void AMultiplayTestPlayerController::ApplySurvivorSpectatorView()
{
	if (bLocalMatchResultRequested || bLocalLevelTransitionFadeHeld || bClientArrivalViewPending || bSpectatorFadePending ||
		GetWorldTimerManager().IsTimerActive(SpectatorFadeTimer)) return;
	if (SurvivorSpectatorView.bEnabled)
	{
		EnsureMannequinLookBinding();
		bAutoManageActiveCameraTarget=false;
		auto* Target=SurvivorSpectatorView.Target.Get();
		if (IsValid(Target) && Target->GetWorld()==GetWorld() && (!Target->IsGameOver() || Target->IsDeathPresentationPending()) && !Target->IsHidden())
		{
			if (LocalSpectatorLookTarget.Get()!=Target)
			{LocalSpectatorLookTarget=Target;SetControlRotation(Target->GetViewRotation());RotationInput=FRotator::ZeroRotator;}
			if (GetViewTarget()!=Target) SetViewTarget(Target);
		}
	}
	else if (SurvivorState==EMultiplayTestSurvivorState::Active)
	{
		if(LocalSpectatorLookTarget.IsValid()){LocalSpectatorLookTarget.Reset();RemoveMannequinLookBinding();}
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
			World->GetTimerManager().ClearTimer(LocalMatchResultHandoffTimer);
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
	if (AProjectProject01TutorialGameMode::IsTutorialWorld(GetWorld())) return;
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
	if (GameMode->TryBroadcastSurvivorPing(this, Type, bHit ? Hit.ImpactPoint : End))
		if (auto* Tutorial=Cast<AProjectProject01TutorialGameMode>(GameMode)) Tutorial->RecordAction(this,EProjectTutorialCondition::Ping);
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
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
	if(bServerControlledBot)return;
	const bool bTutorial=AProjectProject01TutorialGameMode::IsTutorialWorld(GetWorld());
	const bool bAllowSurvivorVoice = !bTutorial && ProjectProject01SurvivorVoice::IsAllowedRole(InRole);
	if (!InRole.Equals(TEXT("Mannequin"),ESearchCase::IgnoreCase)) RemoveMannequinLookBinding();
	if (ProjectProject01SurvivorVoice::IsAllowedRole(InRole) || InRole.Equals(TEXT("Mannequin"), ESearchCase::IgnoreCase))
	{
		bLocalMultiplayerRoleKnown = true;
		bLocalMultiplayerMannequinRole = InRole.Equals(TEXT("Mannequin"),ESearchCase::IgnoreCase);
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
			Voice->ConfigureForMatch(bTutorial ? FString() : UserId, bTutorial ? FString() : MatchId, bTutorial ? TEXT("Disabled") : InRole, this);
		}
	}
	TryRevealInitialGameplayFade();
}

void AMultiplayTestPlayerController::ClientUpdateVoiceAudibility_Implementation(const FString& InMatchId,bool bAllowed,const TArray<FProjectVoiceAudibility>& Peers)
{
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
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

float AMultiplayTestPlayerController::GetMannequinStatusBottom() const
{
	return IsValid(MannequinStatusWidget) ? MannequinStatusWidget->GetContentBottom() : 240.f;
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
	if (!IsLocalController() || bLocalMatchResultRequested || !bInitialGameplayFadePending || bLocalLevelTransitionFadeHeld ||
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
	if(!TryPassLoadingGate())return;

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
	if (!IsLocalController() || bLocalMatchResultRequested || bLocalLevelTransitionFadeHeld || !IsValid(PlayerCameraManager))
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
	if (auto* Tutorial=GetWorld()->GetAuthGameMode<AProjectProject01TutorialGameMode>())
	{
		Tutorial->ToggleTutorialMenu(); return;
	}
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
	const bool bSpectating=IsFreeSpectatingTarget(GetViewTarget());
	if (!IsLocalController() || (!Mannequin && !bSpectating) || bShowMouseCursor || IsLookInputIgnored() ||
		MatchPhase==EMultiplayTestMatchPhase::Ending || MatchPhase==EMultiplayTestMatchPhase::Results ||
		MatchPhase==EMultiplayTestMatchPhase::ReturningToRoom) return;
	if(!bSpectating){ResolveMannequinCameraRotation(Mannequin,0);if (bManualCameraAligning) return;}
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
	if (auto* Owned = Cast<AMannequinAICharacter>(GetPawn()); IsValid(Owned)) return Owned;
	// View-only players have no possessed pawn. A resolved camera is still a valid
	// identity while the owner-only actor property is being mapped/replicated.
	if (IsLocalMannequinPlayer())
		if (auto* Viewed = Cast<AMannequinAICharacter>(GetViewTarget()); IsValid(Viewed) && Viewed->GetWorld()==GetWorld()) return Viewed;
	return nullptr;
}

void AMultiplayTestPlayerController::SetViewedMannequin(AMannequinAICharacter* Mannequin)
{
	if (!HasAuthority() || !IsValid(Mannequin))
	{
		return;
	}

	ViewedMannequin = Mannequin;
	Mannequin->ForceNetUpdate(); // A camera switch must not wait for an offscreen transport budget.
	// Network relevancy uses the SERVER view target, not the owner RPC's local camera.
	SetViewTarget(Mannequin);
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
    if(const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>():nullptr)
        bLeavingForfeits=!bServerControlledBot&&NewPhase==EMultiplayTestMatchPhase::InProgress&&Mode->CountsAsDesertion()&&
            (NewSurvivorState==EMultiplayTestSurvivorState::Active||NewSurvivorState==EMultiplayTestSurvivorState::TransitionWaiting||
             (NewSurvivorState==EMultiplayTestSurvivorState::NotApplicable&&(AuthenticatedRole==TEXT("Mannequin")||IsValid(GetViewedMannequin()))));
	if (NewSurvivorState==EMultiplayTestSurvivorState::Active || NewSurvivorState==EMultiplayTestSurvivorState::NotApplicable)
		SurvivorSpectatorView={};
	ForceNetUpdate();
	if (!bServerControlledBot && PreviousSurvivorState == EMultiplayTestSurvivorState::Active &&
		NewSurvivorState != EMultiplayTestSurvivorState::Active &&
		NewSurvivorState != EMultiplayTestSurvivorState::Eliminated)
	{
		ClientConfigureSurvivorCommunication(FString(), FString(), TEXT("Disabled"));
	}
}

void AMultiplayTestPlayerController::DeliverMatchResultToOwner(const FMultiplayTestMatchResult& Result)
{
	if(bServerControlledBot)return;
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
	SurvivorSpectatorView.bFadeTransition=bFadeTransition;
	SurvivorSpectatorView.RequestId=++SpectatorRequestSerial;
	if(bServerControlledBot)return; // No owner connection, camera or voice endpoint.
	bAutoManageActiveCameraTarget=false;
	// SetViewTarget on the server sends an implicit ClientSetViewTarget RPC, which used
	// to expose the observer camera BEFORE ClientEnterSurvivorSpectator could fade it.
	ForceNetUpdate();
	ClientEnterSurvivorSpectator(SurvivorSpectatorView);
	// Queue our owner intent before the engine's implicit view RPC. Remote clients
	// ignore that RPC while simulating the fade, but the server still follows the
	// observed survivor for relevancy even when the survivor is far from the corpse.
	if (!IsLocalController() && IsValid(SurvivorSpectatorView.Target)) SetViewTarget(SurvivorSpectatorView.Target);
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
	bServerTransitionBlackReady = false;
	bSpawnAfterLevelTransition = bSpawnInNextLevel;
	if(bServerControlledBot){bServerTransitionBlackReady=true;return;}
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	ClientPrepareForLevelTransition(FadeSeconds, ++TransitionFadeSerial);
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
	if(!bServerControlledBot)ClientCompleteLevelTransition(InitialViewTarget, bCanPlay);
	bLevelTransitionCarryover = false;
	bServerTransitionBlackReady = false;
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
	bServerTransitionBlackReady = false;
	bSpawnAfterLevelTransition = false;
	ClientCancelLevelTransition(bRestoreGameplay);
}

void AMultiplayTestPlayerController::ClientCancelLevelTransition_Implementation(const bool bRestoreGameplay)
{
	bLoadingGateRequired=false;bLoadingGateReleased=false;LoadingGateEpoch.Reset();
	if(auto* GI=GetGameInstance())if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->CancelLoading();
	if (!IsLocalController()) return;
	bSpectatorFadePending=false;
	bClientArrivalViewPending=false;
	ClientArrivalViewTarget.Reset();
	if (!LocalTransitionSnapshot.bCaptured && !bLocalLevelTransitionFadeHeld) return;
	GetWorldTimerManager().ClearTimer(SpectatorFadeTimer);
	bLocalLevelTransitionFadeHeld = false;
	bLocalTransitionBlackConfirmed = false;
	ClientDepartureWorld.Reset();
	ClientArrivalStableTarget.Reset();
	ClientArrivalStableSeconds = 0;
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
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
	bLoadingGateRequired=false;LoadingGateEpoch.Reset();
	if(auto* GI=GetGameInstance())if(auto* Loading=GI->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->CancelLoading();
	if (!IsLocalController())
	{
		return;
	}
	PendingLocalMatchResult=Result;
	bLocalMatchResultRequested=true;
	// A verified-result update must not replay fades, audio ramps or input handoff.
	if (IsValid(MatchResultWidget) && MatchResultWidget->IsInViewport())
	{
		MatchResultWidget->ConfigureResult(Result);
		return;
	}
	GetWorldTimerManager().ClearTimer(InitialGameplayRevealTimer);
	GetWorldTimerManager().ClearTimer(InitialGameplayReadinessTimer);
	GetWorldTimerManager().ClearTimer(SpectatorFadeTimer);
	bInitialGameplayFadePending=false;
	bInitialGameplayFadeScheduled=false;
	bSpectatorFadePending=false;
	float Remaining=0;
	for (const AActor* Candidate : {GetViewTarget(), static_cast<AActor*>(GetPawn())})
		if (const auto* Survivor=Cast<APlayerCharacter>(Candidate)) Remaining=FMath::Max(Remaining,Survivor->GetLocalDeathPresentationRemaining(this));
	const double Now=GetWorld()->GetTimeSeconds();
	if (Remaining>0.001f && LocalMatchResultHandoffDeadline==0) LocalMatchResultHandoffDeadline=Now+FMath::Min(Remaining,5.f);
	if (Remaining>0.001f && Now+0.001<LocalMatchResultHandoffDeadline)
	{
		// Reliable results can beat the observed actor's final animation frames. Finish
		// those frames once, then show the newest verification without exposing gameplay.
		GetWorldTimerManager().SetTimer(LocalMatchResultHandoffTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
			{ClientPresentMatchResult_Implementation(PendingLocalMatchResult);}),float(LocalMatchResultHandoffDeadline-Now),false);
		return;
	}
	GetWorldTimerManager().ClearTimer(LocalMatchResultHandoffTimer);
	if(auto* Replay=GetGameInstance()->GetSubsystem<UProjectInstantReplay>();Replay&&Replay->IsDeathCameraPending())
	{
		if(auto* GI=GetGameInstance<UProjectProject01GameInstance>())GI->HoldEntireScreenBlack();
		if(LocalDeathCameraDeadline==0)LocalDeathCameraDeadline=Now+10;
		if(Now<LocalDeathCameraDeadline)
		{
			GetWorldTimerManager().SetTimer(LocalMatchResultHandoffTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
				{ClientPresentMatchResult_Implementation(PendingLocalMatchResult);}),.1f,false);
			return;
		}
		Replay->CancelPresentation();
	}

	// Blackout -> release -> results: if the screen is not fully black yet (e.g. a spectator fade was
	// still running when the match ended), black it out first, then hand over to the result screen.
	if (auto* FadeOwner=GetGameInstance<UProjectProject01GameInstance>(); FadeOwner && !bResultBlackoutStarted && !FadeOwner->IsEntireScreenBlack())
	{
		bResultBlackoutStarted=true;
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
		FadeOwner->FadeEntireScreenToBlack(0.6f);
		GetWorldTimerManager().SetTimer(LocalMatchResultHandoffTimer,FTimerDelegate::CreateWeakLambda(this,[this]()
			{ClientPresentMatchResult_Implementation(PendingLocalMatchResult);}),0.65f,false);
		return;
	}

	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	if ((bSpectatorFadePending || bLocalLevelTransitionFadeHeld) && PlayerCameraManager)
		PlayerCameraManager->StartCameraFade(1,1,0,FLinearColor::Black,false,true);
	bSpectatorFadePending=false;
	GetWorldTimerManager().ClearTimer(SpectatorFadeTimer);
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
	// The opaque result root now owns the viewport; only now may old black layers go.
	ReleaseLocalDeathBlackouts();
	if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->ClearEntireScreenFade();
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
	const FMultiplaySurvivorSpectatorView& View)
{
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
	if (!IsLocalController() || bLocalMatchResultRequested || MatchPhase==EMultiplayTestMatchPhase::Ending || MatchPhase==EMultiplayTestMatchPhase::Results)
	{
		return;
	}
	if (View.RequestId < SurvivorSpectatorView.RequestId) return;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(false);
	SurvivorSpectatorView=View;
	if(auto* Instance=GetGameInstance())if(auto* Voice=Instance->GetSubsystem<UProjectProject01VoiceChatSubsystem>())Voice->StopSpeaking();
	bAutoManageActiveCameraTarget=false;
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	OnRep_SurvivorSpectatorView();
	if (!bSpectatorFadePending && !bLocalLevelTransitionFadeHeld && IsValid(PlayerCameraManager)) PlayerCameraManager->StopCameraFade();
}

void AMultiplayTestPlayerController::TryFinishSpectatorFade()
{
	if (bLocalMatchResultRequested || MatchPhase==EMultiplayTestMatchPhase::Ending || MatchPhase==EMultiplayTestMatchPhase::Results ||
		!bSpectatorFadePending || bLocalLevelTransitionFadeHeld || bClientArrivalViewPending ||
		GetWorld()->GetTimeSeconds()+0.001 < SpectatorSwitchReadyAt) return;
	auto* Target=SurvivorSpectatorView.Target.Get();
	if (!IsValid(Target) || Target->GetWorld()!=GetWorld() || Target->IsHidden() || Target->IsGameOver()) return;
	auto* GI=GetGameInstance<UProjectProject01GameInstance>();
	if (GI) GI->HoldEntireScreenBlack();
	SetViewTarget(Target);
	ReleaseLocalDeathBlackouts();
	bSpectatorFadePending=false;
	if (PlayerCameraManager) PlayerCameraManager->StopCameraFade();
	if (GI) GI->FadeEntireScreenFromBlack(0.5f);
	else if (PlayerCameraManager) PlayerCameraManager->StartCameraFade(1,0,0.5f,FLinearColor::Black,false,false);
}

void AMultiplayTestPlayerController::ReleaseLocalDeathBlackouts()
{
	for (TActorIterator<APlayerCharacter> It(GetWorld());It;++It) It->ReleaseLocalDeathBlackout(this);
}

void AMultiplayTestPlayerController::ClientPrepareForLevelTransition_Implementation(const float FadeSeconds, const uint32 RequestId)
{
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
	if(auto* GI=GetGameInstance())if(auto* Replay=GI->GetSubsystem<UProjectInstantReplay>())Replay->CancelPresentation();
	if (bLocalMatchResultRequested) return;
	if (!IsLocalController())
	{
		return;
	}
	// A duplicate prepare must not restart a nearly finished fade.
	if (bLocalLevelTransitionFadeHeld && LocalTransitionFadeSerial == RequestId) return;
	bLoadingGateRequired=GetNetMode()!=NM_Standalone;bLoadingGateReleased=false;LoadingGateEpoch.Reset();LastReportedLoadingStage=0;
	LocalTransitionFadeSerial = RequestId;
	bLocalTransitionBlackConfirmed = false;
	LocalTransitionBlackSince = 0;
	ClientDepartureWorld = GetWorld();
	ClientArrivalStableTarget.Reset();
	ClientArrivalStableSeconds = 0;

	bInitialGameplayFadePending = false;
	bInitialGameplayFadeScheduled = false;
	bClientArrivalViewPending=false;
	ClientArrivalViewTarget.Reset();
	bLocalLevelTransitionFadeHeld = true;
	bSpectatorFadePending = false;
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
	else if (PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraFade(PlayerCameraManager->FadeAmount,1,
			FMath::Clamp(FadeSeconds,0.1f,10.0f),FLinearColor::Black,false,true);
	}
}

void AMultiplayTestPlayerController::TryConfirmLevelTransitionBlack()
{
	if (!IsLocalController() || !bLocalLevelTransitionFadeHeld || bLocalTransitionBlackConfirmed) return;
	const auto* GI=GetGameInstance<UProjectProject01GameInstance>();
	const bool bBlack=GI ? GI->IsEntireScreenBlack() :
		(PlayerCameraManager && PlayerCameraManager->FadeAmount >= 1.0f);
	if (!bBlack) { LocalTransitionBlackSince=0; return; }
	// Let at least one fully black frame reach the viewport before allowing travel.
	if (LocalTransitionBlackSince==0) { LocalTransitionBlackSince=FPlatformTime::Seconds(); return; }
	if (FPlatformTime::Seconds()-LocalTransitionBlackSince<0.05) return;
	bLocalTransitionBlackConfirmed=true;
	if(auto* Instance=GetGameInstance())if(auto* Loading=Instance->GetSubsystem<UProjectLoadingScreenSubsystem>())Loading->BeginLoading(GetNetMode()!=NM_Standalone);
	ServerConfirmLevelTransitionBlack(LocalTransitionFadeSerial);
}

void AMultiplayTestPlayerController::ServerConfirmLevelTransitionBlack_Implementation(const uint32 RequestId)
{
	// An old/delayed acknowledgement cannot unlock a new transition.
	if (bLevelTransitionCarryover && RequestId==TransitionFadeSerial)
	{
		bServerTransitionBlackReady=true;
		UE_LOG(LogTemp,Log,TEXT("Travel blackout confirmed: Controller=%s Request=%u"),*GetName(),RequestId);
	}
}

void AMultiplayTestPlayerController::ClientCompleteLevelTransition_Implementation(
	AActor* InitialViewTarget,
	const bool bCanPlay)
{
	if(GetWorld()&&GetWorld()->IsPlayingReplay())return;
	if (!IsLocalController() || bLocalMatchResultRequested)
	{
		return;
	}

	bClientArrivalViewPending=true;
	ClientArrivalStableTarget.Reset();
	ClientArrivalStableSeconds=0;
	bClientArrivalCanPlay=bCanPlay;
	ClientArrivalViewTarget=InitialViewTarget;
	SurvivorSpectatorView.bEnabled=!bCanPlay;
	if (bCanPlay) SurvivorSpectatorView.Target=nullptr;
	else if (auto* Target=Cast<APlayerCharacter>(InitialViewTarget)) SurvivorSpectatorView.Target=Target;
	else if (SurvivorSpectatorView.Target && SurvivorSpectatorView.Target->GetWorld()!=GetWorld()) SurvivorSpectatorView.Target=nullptr;
	TryCompleteClientArrivalView();
}

void AMultiplayTestPlayerController::TryCompleteClientArrivalView(const float ReadyDeltaSeconds)
{
	if (bLocalMatchResultRequested) return;
	if (!IsLocalController() || !bClientArrivalViewPending) return;
	UWorld* World=GetWorld();
	bool bWorldReady=IsValid(World) && World!=ClientDepartureWorld.Get() && World->HasBegunPlay() && !IsAsyncLoading();
	if (bWorldReady)
		for (const ULevelStreaming* Level:World->GetStreamingLevels())
			if (IsValid(Level) && ((Level->ShouldBeLoaded() && !Level->IsLevelLoaded()) ||
				(Level->ShouldBeVisible() && !Level->IsLevelVisible()))) { bWorldReady=false; break; }
	if (!bWorldReady) { ClientArrivalStableTarget.Reset(); ClientArrivalStableSeconds=0; return; }
	AActor* ReadyTarget=nullptr;
	if (!bClientArrivalCanPlay) ReadyTarget=SurvivorSpectatorView.Target;
	else if (bLocalMultiplayerMannequinRole)
	{
		ReadyTarget=GetViewedMannequin();
		if (!IsValid(ReadyTarget)) ReadyTarget=ClientArrivalViewTarget.Get();
	}
	else ReadyTarget=GetPawn();
	if (!IsValid(ReadyTarget) || ReadyTarget->GetWorld()!=World || ReadyTarget->IsHidden() ||
		(Cast<APlayerCharacter>(ReadyTarget) && Cast<APlayerCharacter>(ReadyTarget)->IsGameOver()))
	{
		ClientArrivalStableTarget.Reset(); ClientArrivalStableSeconds=0; return;
	}
	// Never expose a default/spectator camera while the new Pawn reference is still unmapped.
	if (bClientArrivalCanPlay && !bLocalMultiplayerMannequinRole && !ReadyTarget->IsA<APlayerCharacter>()) return;
	if (ClientArrivalStableTarget.Get()!=ReadyTarget)
	{
		ClientArrivalStableTarget=ReadyTarget; ClientArrivalStableSeconds=0; return;
	}
	// Do not spend the reveal in the loading hitch. Require several ready frames.
	// Only PlayerTick supplies delta: multiple replication notifications in one frame do not count as stability.
	ClientArrivalStableSeconds+=FMath::Clamp(ReadyDeltaSeconds,0.0f,0.05f);
	if (ClientArrivalStableSeconds<0.15f) return;
	if(!TryPassLoadingGate())return;
	if (auto* GI=GetGameInstance<UProjectProject01GameInstance>()) GI->HoldEntireScreenBlack();
	if (bClientArrivalCanPlay) SurvivorSpectatorView={};
	if (PlayerCameraManager) PlayerCameraManager->bClientSimulatingViewTarget=!bClientArrivalCanPlay;
	bClientArrivalViewPending=false;
	ClientArrivalViewTarget.Reset();
	bAutoManageActiveCameraTarget=bClientArrivalCanPlay && !bLocalMultiplayerMannequinRole;
	bSpectatorFadePending=false;
	SetViewTarget(ReadyTarget);
	UE_LOG(LogTemp,Log,TEXT("Travel view ready; revealing: World=%s Target=%s Observer=%d Mannequin=%d"),
		*World->GetName(),*ReadyTarget->GetName(),!bClientArrivalCanPlay,bLocalMultiplayerMannequinRole);
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	SetIgnoreMoveInput(!bClientArrivalCanPlay);
	LocalTransitionSnapshot = {};
	GetWorldTimerManager().ClearTimer(SpectatorFadeTimer);
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	bInitialGameplayFadePending = false;
	bLocalLevelTransitionFadeHeld = false;
	ClientDepartureWorld.Reset();
	ClientArrivalStableTarget.Reset();
	ClientArrivalStableSeconds=0;
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
	else if (PlayerCameraManager) PlayerCameraManager->StartCameraFade(1,0,0.6f,FLinearColor::Black,false,false);
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
		!EditorSettings->HasSeenMannequinGuide();
#else
	UProjectProject01AuthSubsystem* Auth = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>()
		: nullptr;
	bShouldShowMannequinGuide = IsValid(Auth) && Auth->IsSignedIn() &&
		!Auth->HasSeenMannequinGuideForAccount();
#endif
	if (bShouldShowMannequinGuide && !bRoleGuideShownForThisController && !AProjectProject01TutorialGameMode::IsTutorialWorld(GetWorld()))
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

	if (GameMode->TryPossessMannequin(this, Slot))
		if (auto* Tutorial=Cast<AProjectProject01TutorialGameMode>(GameMode)) Tutorial->RecordAction(this,EProjectTutorialCondition::SelectMannequin);
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

	if (GameMode->TryEnableMannequinManualControl(this))
		if (auto* Tutorial=Cast<AProjectProject01TutorialGameMode>(GameMode)) Tutorial->RecordAction(this,EProjectTutorialCondition::ManualControl);
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
		if (GameMode->TryIssueMannequinCommand(this, Command))
		{
			const TCHAR* Name=Command==EMultiplayMannequinCommand::Patrol?TEXT("정찰"):
				Command==EMultiplayMannequinCommand::ChaseInDetectionRange?TEXT("감지 범위 추격"):TEXT("마네킹 플레이어 추종");
			ClientNotifyMannequinCommand(FString::Printf(TEXT("%d번: %s 명령 지정"),GetViewedMannequin()->GetControlSlot(),Name));
			if (auto* Tutorial=Cast<AProjectProject01TutorialGameMode>(GameMode))
				Tutorial->RecordAction(this,Command==EMultiplayMannequinCommand::Patrol ? EProjectTutorialCondition::PatrolCommand :
					Command==EMultiplayMannequinCommand::ChaseInDetectionRange ? EProjectTutorialCondition::ChaseCommand : EProjectTutorialCondition::FollowCommand);
		}
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
		const bool bChanged=GameMode->TryResetFollowingMannequins(this);
		if (SurvivorState==EMultiplayTestSurvivorState::NotApplicable && GetViewedMannequin() && MatchPhase==EMultiplayTestMatchPhase::InProgress)
			ClientNotifyMannequinCommand(bChanged?TEXT("추종 해제: 따라오던 마네킹이 정찰 상태로 변경되었습니다."):TEXT("추종 중인 마네킹이 없습니다."));
		if (bChanged)
			if (auto* Tutorial=Cast<AProjectProject01TutorialGameMode>(GameMode)) Tutorial->RecordAction(this,EProjectTutorialCondition::ResetFollow);
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
		if (auto* Tutorial=Cast<AProjectProject01TutorialGameMode>(GameMode)) Tutorial->RecordAction(this,EProjectTutorialCondition::ToggleContactDamage);
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
	{
		Mode->TryMannequinAttack(this);
		// In the controls exercise an air swing is enough; the final capture still requires an actual hit.
		if (auto* Tutorial=Cast<AProjectProject01TutorialGameMode>(Mode)) Tutorial->RecordAction(this,EProjectTutorialCondition::DirectAttack);
	}
}

bool AMultiplayTestPlayerController::ServerRequestMannequinAttack_Validate() { return true; }

bool AMultiplayTestPlayerController::ServerToggleMannequinCatchDamage_Validate()
{
	return true;
}

void AMultiplayTestPlayerController::ClientNotifyMannequinCatchDamage_Implementation(const bool bEnabled)
{
	ClientNotifyMannequinCommand_Implementation(bEnabled?TEXT("전체 AI 데스카운트 소모: 활성"):TEXT("전체 AI 데스카운트 소모: 비활성"));
	ClientMessage(bEnabled
		? TEXT("전체 AI 마네킹의 데스카운트 소모: 활성")
		: TEXT("전체 AI 마네킹의 데스카운트 소모: 비활성"));
}

void AMultiplayTestPlayerController::ClientNotifyMannequinCommand_Implementation(const FString& Message)
{
	MannequinCommandNotice=Message;
	MannequinCommandNoticeUntil=GetWorld()?GetWorld()->GetTimeSeconds()+3.f:0.f;
}

FString AMultiplayTestPlayerController::GetMannequinCommandNotice() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds()<MannequinCommandNoticeUntil?MannequinCommandNotice:FString();
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
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, bLeavingForfeits, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, SurvivorSpectatorView, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMultiplayTestPlayerController, bManualMannequinCommandIssued, COND_OwnerOnly);
}
