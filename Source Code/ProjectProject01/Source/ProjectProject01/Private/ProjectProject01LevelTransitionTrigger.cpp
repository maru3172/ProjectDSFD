// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectProject01LevelTransitionTrigger.h"

#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectProject01GameInstance.h"
#include "Components/BoxComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"

AProjectProject01LevelTransitionTrigger::AProjectProject01LevelTransitionTrigger()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	TransitionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TransitionVolume"));
	SetRootComponent(TransitionVolume);
	TransitionVolume->InitBoxExtent(FVector(200.0f, 200.0f, 150.0f));
	TransitionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TransitionVolume->SetCollisionObjectType(ECC_WorldDynamic);
	TransitionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	TransitionVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TransitionVolume->SetGenerateOverlapEvents(true);
}

void AProjectProject01LevelTransitionTrigger::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority() || !IsValid(TransitionVolume))
	{
		return;
	}

	TransitionVolume->OnComponentBeginOverlap.AddUniqueDynamic(
		this, &AProjectProject01LevelTransitionTrigger::HandleBeginOverlap);
	TransitionVolume->OnComponentEndOverlap.AddUniqueDynamic(
		this, &AProjectProject01LevelTransitionTrigger::HandleEndOverlap);
	if (GEngine) TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(
		this, &AProjectProject01LevelTransitionTrigger::HandleTravelFailure);

	if (DestinationLevel.IsNull())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Level transition trigger '%s' has no DestinationLevel and will remain disabled."),
			*GetActorNameOrLabel());
	}
}

void AProjectProject01LevelTransitionTrigger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(EvaluationTimer);
		World->GetTimerManager().ClearTimer(TravelTimer);
		World->GetTimerManager().ClearTimer(TravelWatchdogTimer);
	}
	if (GEngine) GEngine->OnTravelFailure().Remove(TravelFailureHandle);

	if (IsValid(TransitionVolume))
	{
		TransitionVolume->OnComponentBeginOverlap.RemoveDynamic(
			this, &AProjectProject01LevelTransitionTrigger::HandleBeginOverlap);
		TransitionVolume->OnComponentEndOverlap.RemoveDynamic(
			this, &AProjectProject01LevelTransitionTrigger::HandleEndOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void AProjectProject01LevelTransitionTrigger::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const int32 OtherBodyIndex,
	const bool bFromSweep,
	const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComponent;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	APlayerCharacter* Survivor = Cast<APlayerCharacter>(OtherActor);
	if (bTransitionStarted || !IsEligibleLivingPlayer(Survivor) || !IsDestinationAvailable())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (IsValid(World) && World->GetNetMode() != NM_Standalone)
	{
		AMultiplayTestGameMode* GameMode = World->GetAuthGameMode<AMultiplayTestGameMode>();
		AMultiplayTestPlayerController* Controller =
			Cast<AMultiplayTestPlayerController>(Survivor->GetController());
		if (!IsValid(GameMode) || !IsValid(Controller) ||
			!GameMode->MarkSurvivorWaitingForLevelTransition(Survivor))
		{
			return;
		}
		AcceptedSurvivors.Add(Controller);
		if (!bRequireAllLivingSurvivors)
		{
			BeginTransition();
			return;
		}
	}

	EvaluateTransition();
	if (!bTransitionStarted)
	{
		GetWorldTimerManager().SetTimer(
			EvaluationTimer,
			this,
			&AProjectProject01LevelTransitionTrigger::EvaluateTransition,
			FMath::Max(0.05f, EvaluationIntervalSeconds),
			true);
	}
}

void AProjectProject01LevelTransitionTrigger::HandleEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const int32 OtherBodyIndex)
{
	(void)OverlappedComponent;
	(void)OtherActor;
	(void)OtherComponent;
	(void)OtherBodyIndex;

	if (bTransitionStarted || !AcceptedSurvivors.IsEmpty())
	{
		return;
	}

	int32 PlayersInside = 0;
	int32 RequiredPlayers = 0;
	GetTransitionProgress(PlayersInside, RequiredPlayers);
	if (PlayersInside <= 0)
	{
		GetWorldTimerManager().ClearTimer(EvaluationTimer);
	}
}

bool AProjectProject01LevelTransitionTrigger::IsEligibleLivingPlayer(
	const APlayerCharacter* Player) const
{
	if (!IsValid(Player) || Player->IsGameOver() || Player->IsHidden() ||
		!IsValid(Player->GetController()) || !Player->GetController()->IsPlayerController())
	{
		return false;
	}

	if (const AMultiplayTestPlayerController* MultiplayerController =
		Cast<AMultiplayTestPlayerController>(Player->GetController()))
	{
		const FString& AuthenticatedRoleName = MultiplayerController->GetAuthenticatedRole();
		if (!AuthenticatedRoleName.IsEmpty() &&
			!AuthenticatedRoleName.Equals(TEXT("Survivor"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		const EMultiplayTestSurvivorState State = MultiplayerController->GetSurvivorState();
		if (State == EMultiplayTestSurvivorState::TransitionWaiting ||
			State == EMultiplayTestSurvivorState::Eliminated ||
			State == EMultiplayTestSurvivorState::Escaped)
		{
			return false;
		}
	}

	return true;
}

void AProjectProject01LevelTransitionTrigger::GetTransitionProgress(
	int32& OutPlayersInside,
	int32& OutRequiredPlayers) const
{
	OutPlayersInside = 0;
	OutRequiredPlayers = 0;

	const UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(TransitionVolume))
	{
		return;
	}

	for (const TWeakObjectPtr<AMultiplayTestPlayerController>& Accepted : AcceptedSurvivors)
	{
		OutPlayersInside += Accepted.IsValid() ? 1 : 0;
	}

	for (TActorIterator<APlayerCharacter> It(World); It; ++It)
	{
		APlayerCharacter* Player = *It;
		if (!IsEligibleLivingPlayer(Player))
		{
			continue;
		}

		++OutRequiredPlayers;
		if (TransitionVolume->IsOverlappingActor(Player))
		{
			++OutPlayersInside;
		}
	}
	OutRequiredPlayers += OutPlayersInside;
}

void AProjectProject01LevelTransitionTrigger::EvaluateTransition()
{
	if (bTransitionStarted || !HasAuthority() || DestinationLevel.IsNull())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	if (World->GetNetMode() == NM_Standalone)
	{
		BeginTransition();
		return;
	}

	AMultiplayTestGameMode* GameMode = World->GetAuthGameMode<AMultiplayTestGameMode>();
	if (IsValid(GameMode) && !AcceptedSurvivors.IsEmpty() && GameMode->CanCommitLevelTransition())
	{
		BeginTransition();
	}
}

void AProjectProject01LevelTransitionTrigger::BeginTransition()
{
	if (bTransitionStarted)
	{
		return;
	}

	const FString DestinationPackage = GetDestinationPackageName();
	if (!IsDestinationAvailable())
	{
		RollbackTransition(TEXT("DestinationLevel is unset or its map package is unavailable"));
		return;
	}

	bTransitionStarted = true;
	GetWorldTimerManager().ClearTimer(EvaluationTimer);
	TransitionVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		if (World->GetNetMode() == NM_Standalone)
		{
			if (UProjectProject01GameInstance* GameInstance =
				Cast<UProjectProject01GameInstance>(GetGameInstance()))
			{
				GameInstance->FadeEntireScreenToBlack(0.6f);
			}
			if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
				IsValid(PlayerController))
			{
				SingleTransitionController = PlayerController;
				bSingleMoveIgnored = PlayerController->IsMoveInputIgnored();
				bSingleLookIgnored = PlayerController->IsLookInputIgnored();
				PlayerController->SetIgnoreMoveInput(true);
				PlayerController->SetIgnoreLookInput(true);
			}
		}
		else if (AMultiplayTestGameMode* GameMode = World->GetAuthGameMode<AMultiplayTestGameMode>();
			IsValid(GameMode))
		{
			GameMode->PrepareLevelTransitionTravel();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Level transition trigger '%s' accepted. Destination=%s"),
		*GetActorNameOrLabel(), *DestinationPackage);

	if (TravelDelaySeconds <= 0.0f)
	{
		ExecuteTransition();
		return;
	}

	GetWorldTimerManager().SetTimer(
		TravelTimer,
		this,
		&AProjectProject01LevelTransitionTrigger::ExecuteTransition,
		TravelDelaySeconds,
		false);
}

FString AProjectProject01LevelTransitionTrigger::GetDestinationPackageName() const
{
	if (DestinationLevel.IsNull())
	{
		return FString();
	}

	const FString ObjectPath = DestinationLevel.ToSoftObjectPath().ToString();
	return FPackageName::ObjectPathToPackageName(ObjectPath);
}

bool AProjectProject01LevelTransitionTrigger::IsDestinationAvailable() const
{
	const FString Package = GetDestinationPackageName();
	return FPackageName::IsValidLongPackageName(Package) && FPackageName::DoesPackageExist(Package);
}

void AProjectProject01LevelTransitionTrigger::HandleTravelFailure(
	UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	if (!bTransitionStarted || World != GetWorld()) return;
	// Engine broadcasts some failures before it cancels its async travel handler.
	// Restore on the next tick, never while that handler still owns the old world.
	const FString Reason = FString::Printf(TEXT("Travel failure %d: %s"), static_cast<int32>(FailureType), *Error);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Reason]()
	{
		const auto* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetWorld()) : nullptr;
		if (bTransitionStarted && (!Context || !Context->SeamlessTravelHandler.IsInTransition())) RollbackTransition(Reason);
	}));
}

void AProjectProject01LevelTransitionTrigger::HandleTravelTimeout()
{
	// A valid async load can exceed this deadline on slow disks. Its engine-owned
	// world switch cannot safely be rolled back by simply revealing the old pawn.
	const auto* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetWorld()) : nullptr;
    const bool bAsync=Context&&Context->SeamlessTravelHandler.IsInTransition();
    if(ExtendAsyncTravelGrace(bAsync))
    {
        GetWorldTimerManager().SetTimer(TravelWatchdogTimer,this,&ThisClass::HandleTravelTimeout,30.0f,false);
        return;
    }
    if(bAsync){AbortUnresponsiveTravel();return;}
	RollbackTransition(TEXT("No level replacement within the 30-second travel deadline"));
}

bool AProjectProject01LevelTransitionTrigger::ExtendAsyncTravelGrace(bool bEngineOwnsTransition)
{
    return bEngineOwnsTransition&&++AsyncTravelWatchdogChecks<4; // total 120 seconds, never an endless black screen
}
void AProjectProject01LevelTransitionTrigger::AbortUnresponsiveTravel()
{
    if(auto* Instance=GetGameInstance<UProjectProject01GameInstance>())Instance->AbortStalledLevelTravel();
}

void AProjectProject01LevelTransitionTrigger::RollbackTransition(const FString& Reason)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World)) return;
    if(auto* Instance=GetGameInstance<UProjectProject01GameInstance>())Instance->CancelLevelTransitionWatchdog();
	World->GetTimerManager().ClearTimer(EvaluationTimer);
	World->GetTimerManager().ClearTimer(TravelTimer);
	World->GetTimerManager().ClearTimer(TravelWatchdogTimer);
	// Keep overlap acceptance blocked until pawn and volume collisions are restored.
	bTransitionStarted = true;
	AcceptedSurvivors.Reset();
	World->NextURL.Reset();
	if (GEngine)
	{
		if (auto* Context = GEngine->GetWorldContextFromWorld(World)) Context->TravelURL.Reset();
	}
	if (World->GetNetMode() == NM_Standalone)
	{
		if (auto* Controller = SingleTransitionController.Get())
		{
			Controller->ResetIgnoreMoveInput();
			Controller->ResetIgnoreLookInput();
			Controller->SetIgnoreMoveInput(bSingleMoveIgnored);
			Controller->SetIgnoreLookInput(bSingleLookIgnored);
		}
		SingleTransitionController.Reset();
		if (auto* Instance = GetGameInstance<UProjectProject01GameInstance>())
		{
			Instance->ConsumePendingSinglePlayerLevelTransitionFade();
			Instance->FadeEntireScreenFromBlack(0.6f);
		}
	}
	else if (auto* Mode = World->GetAuthGameMode<AMultiplayTestGameMode>()) Mode->CancelLevelTransitionTravel();
	if (IsValid(TransitionVolume)) TransitionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	bTransitionStarted = false;
    AsyncTravelWatchdogChecks=0;
	UE_LOG(LogTemp, Warning, TEXT("Level transition '%s' cancelled: %s"), *GetActorNameOrLabel(), *Reason);
}

void AProjectProject01LevelTransitionTrigger::ExecuteTransition()
{
	UWorld* World = GetWorld();
	const FString DestinationPackage = GetDestinationPackageName();
	if (!HasAuthority() || !IsValid(World))
	{
		return;
	}
	// Recheck after the fade delay (editor asset changes / uncooked destinations).
    AsyncTravelWatchdogChecks=0;
	if (!IsDestinationAvailable())
	{
		RollbackTransition(TEXT("Destination disappeared before travel"));
		return;
	}
	World->GetTimerManager().SetTimer(TravelWatchdogTimer, this,
		&AProjectProject01LevelTransitionTrigger::HandleTravelTimeout, 30.0f, false);
    if(auto* Instance=GetGameInstance<UProjectProject01GameInstance>())Instance->ArmLevelTransitionWatchdog();

	if (World->GetNetMode() == NM_Standalone)
	{
		if (UProjectProject01GameInstance* GameInstance =
			Cast<UProjectProject01GameInstance>(GetGameInstance()))
		{
			GameInstance->MarkPendingSinglePlayerLevelTransitionFade();
		}
		UGameplayStatics::OpenLevel(this, FName(*DestinationPackage), true);
		return;
	}

	// 서버 이동은 현재 접속한 모든 클라이언트를 같은 목적지로 함께 전환합니다.
    if(auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>())Mode->PrepareLevelTransitionTravel();
	if (!World->ServerTravel(DestinationPackage, false))
	{
		RollbackTransition(FString::Printf(TEXT("ServerTravel rejected destination '%s'"), *DestinationPackage));
	}
}
