// Fill out your copyright notice in the Description page of Project Settings.

#include "PlayerHeartbeatComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"

UPlayerHeartbeatComponent::UPlayerHeartbeatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPlayerHeartbeatComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetHeartbeatState();
	RefreshMannequinCache();

	if (bEnableHeartbeatLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[Heartbeat] Component initialized | Owner=%s"), *GetNameSafe(GetOwner()));
	}
}

void UPlayerHeartbeatComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	// 마네킹 검색과 시야 판정은 각각의 설정 주기에만 실행한다.
	RefreshAccumulator += DeltaTime;
	if (RefreshAccumulator >= FMath::Max(MannequinRefreshInterval, 0.1f))
	{
		RefreshAccumulator = 0.0f;
		RefreshMannequinCache();
	}

	VisionCheckAccumulator += DeltaTime;
	if (VisionCheckAccumulator >= FMath::Max(VisionCheckInterval, 0.01f))
	{
		VisionCheckAccumulator = 0.0f;
		UpdateDetectionStates(*OwnerPawn);
	}

	UpdateBPM(DeltaTime, *OwnerPawn);
	UpdateBeatLog(DeltaTime);
}

void UPlayerHeartbeatComponent::ResetHeartbeatState()
{
	CachedMannequins.Reset();
	MannequinStates.Reset();
	CurrentBPM = 0.0f;
	DistanceBPM = 0.0f;
	EncounterBPM = 0.0f;
	bHeartbeatActive = false;
	BeatAccumulator = 0.0f;
	VisionCheckAccumulator = 0.0f;
	RefreshAccumulator = 0.0f;
	LastLoggedBPM = 0.0f;
}

int32 UPlayerHeartbeatComponent::GetKnownMannequinCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState>& Entry : MannequinStates)
	{
		if (Entry.Key.IsValid() && Entry.Value.bHasEverBeenRecognized)
		{
			++Count;
		}
	}
	return Count;
}

int32 UPlayerHeartbeatComponent::GetActiveMannequinCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState>& Entry : MannequinStates)
	{
		if (Entry.Key.IsValid() && Entry.Value.bActiveForHeartbeat)
		{
			++Count;
		}
	}
	return Count;
}

void UPlayerHeartbeatComponent::RefreshMannequinCache()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, AMannequinAICharacter::StaticClass(), FoundActors);
	CachedMannequins.Reset(FoundActors.Num());
	for (AActor* Actor : FoundActors)
	{
		if (AMannequinAICharacter* Mannequin = Cast<AMannequinAICharacter>(Actor); IsValid(Mannequin))
		{
			CachedMannequins.Add(Mannequin);
		}
	}
	RemoveInvalidMannequins();
}

void UPlayerHeartbeatComponent::UpdateDetectionStates(APawn& OwnerPawn)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	FVector ViewLocation;
	FVector ViewForward;
	if (!GetPlayerViewPoint(OwnerPawn, ViewLocation, ViewForward))
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const float SafeRetentionRange = FMath::Max(0.0f, RetentionRange);
	const double SafeRearmDelay = FMath::Max(0.0f, SurpriseRearmDelay);
	const double SafeCooldown = FMath::Max(0.0f, SurpriseCooldown);
	const double SafeGraceSeconds = FMath::Max(0.0f, LostSightGraceSeconds);

	// 미인지 대상은 실제 시야로만 등록하고, 인지 대상만 넓은 유지 범위를 적용한다.
	for (const TWeakObjectPtr<AMannequinAICharacter>& WeakMannequin : CachedMannequins)
	{
		AMannequinAICharacter* Mannequin = WeakMannequin.Get();
		if (!IsValid(Mannequin))
		{
			continue;
		}

		FHeartbeatMannequinState& State = MannequinStates.FindOrAdd(WeakMannequin);
		const float Distance = FVector::Dist(OwnerPawn.GetActorLocation(), Mannequin->GetActorLocation());
		const bool bActuallyVisible = IsMannequinActuallyVisible(
			OwnerPawn, *Mannequin, ViewLocation, ViewForward);
		// 현재 방식은 부채꼴, 장애물, 거리 조건을 모두 만족해야 발견 상태로 인정한다.
		const bool bVisibleNow = bActuallyVisible && Distance <= FMath::Max(HeartbeatRange, 0.0f);

		if (bVisibleNow)
		{
			// 보이지 않던 마네킹이 실제 시야에 다시 들어온 순간만 놀람을 검사한다.
			if (!State.bCurrentlyVisible)
			{
				const bool bFirstEncounter = !State.bHasEverBeenRecognized;
				const double CooldownElapsed = CurrentTime - State.LastSurpriseTime;
				const bool bCooldownFinished = CooldownElapsed >= SafeCooldown;

				if (bEnableHeartbeatLog && bEnableRediscovery && !bFirstEncounter)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Heartbeat][VisibilityRestored] Mannequin=%s | Armed=%s | CooldownElapsed=%.2fs | CooldownRequired=%.2fs"),
						*GetNameSafe(Mannequin),
						State.bSurpriseArmed ? TEXT("true") : TEXT("false"),
						CooldownElapsed,
						SafeCooldown);
				}

				if (bFirstEncounter ||
					(bEnableRediscovery && State.bSurpriseArmed && bCooldownFinished))
				{
					TriggerSurpriseBPM(*Mannequin, Distance, bFirstEncounter);
					State.LastSurpriseTime = CurrentTime;
				}
				else if (bEnableHeartbeatLog && bEnableRediscovery)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Heartbeat][RediscoveryBlocked] Mannequin=%s | Reason=%s%s"),
						*GetNameSafe(Mannequin),
						!State.bSurpriseArmed ? TEXT("NotRearmed") : TEXT(""),
						!bCooldownFinished ? TEXT(" CooldownActive") : TEXT(""));
				}
			}

			State.bHasEverBeenRecognized = true;
			State.bCurrentlyVisible = true;
			State.bSurpriseArmed = false;
			// 신체 일부라도 실제로 보였다면 이번 프레임은 활성 대상으로 인정한다.
			State.LastRetentionTime = CurrentTime;
		}
		else
		{
			// 시야를 잃은 최초 시각부터 재발견 준비 시간을 측정한다.
			if (State.bCurrentlyVisible)
			{
				State.bCurrentlyVisible = false;
				State.HiddenStartTime = CurrentTime;
				if (bEnableHeartbeatLog && bEnableRediscovery)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Heartbeat][VisibilityLost] Mannequin=%s | RearmAfter=%.2fs"),
						*GetNameSafe(Mannequin), SafeRearmDelay);
				}
			}

			if (bEnableRediscovery && State.bHasEverBeenRecognized && !State.bSurpriseArmed &&
				CurrentTime - State.HiddenStartTime >= SafeRearmDelay)
			{
				State.bSurpriseArmed = true;
				if (bEnableHeartbeatLog)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Heartbeat][SurpriseArmed] Mannequin=%s | HiddenDuration=%.2fs"),
						*GetNameSafe(Mannequin), CurrentTime - State.HiddenStartTime);
				}
			}
		}

		bool bRetentionQualified = false;
		if (bUseRetentionRules)
		{
			bRetentionQualified = State.bHasEverBeenRecognized &&
				Distance <= SafeRetentionRange &&
				IsInsideRetentionCone(*Mannequin, ViewLocation, ViewForward) &&
				HasClearLineOfSight(OwnerPawn, *Mannequin, ViewLocation);
			if (bRetentionQualified)
			{
				State.LastRetentionTime = CurrentTime;
			}
		}

		// 기존 유지 영역 코드는 보존하고 설정이 켜진 경우에만 사용한다.
		const bool bWasActiveForHeartbeat = State.bActiveForHeartbeat;
		State.bActiveForHeartbeat = bUseRetentionRules
			? State.bHasEverBeenRecognized && CurrentTime - State.LastRetentionTime <= SafeGraceSeconds
			: State.bHasEverBeenRecognized && bVisibleNow;

		if (bEnableHeartbeatLog && bWasActiveForHeartbeat != State.bActiveForHeartbeat)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Heartbeat][ActiveChanged] Mannequin=%s | Active=%s | RetentionQualified=%s | SinceLastRetention=%.2fs"),
				*GetNameSafe(Mannequin),
				State.bActiveForHeartbeat ? TEXT("true") : TEXT("false"),
				bRetentionQualified ? TEXT("true") : TEXT("false"),
				CurrentTime - State.LastRetentionTime);
		}
	}

	RemoveInvalidMannequins();
}

void UPlayerHeartbeatComponent::UpdateBPM(float DeltaTime, const APawn& OwnerPawn)
{
	// 여러 활성 대상 중 가장 높은 거리 기반 BPM을 하한으로 사용한다.
	float NewDistanceBPM = 0.0f;
	int32 ActiveCount = 0;
	for (const TPair<TWeakObjectPtr<AMannequinAICharacter>, FHeartbeatMannequinState>& Entry : MannequinStates)
	{
		const AMannequinAICharacter* Mannequin = Entry.Key.Get();
		if (IsValid(Mannequin) && Entry.Value.bActiveForHeartbeat)
		{
			const float Distance = FVector::Dist(OwnerPawn.GetActorLocation(), Mannequin->GetActorLocation());
			NewDistanceBPM = FMath::Max(NewDistanceBPM, CalculateDistanceBPM(Distance));
			++ActiveCount;
		}
	}

	const bool bWasActive = bHeartbeatActive;
	bHeartbeatActive = NewDistanceBPM > 0.0f;
	if (!bHeartbeatActive)
	{
		CurrentBPM = 0.0f;
		DistanceBPM = 0.0f;
		EncounterBPM = 0.0f;
		BeatAccumulator = 0.0f;
		if (bWasActive && bEnableHeartbeatLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[Heartbeat] STOP | No recognized mannequin in retention range"));
		}
		return;
	}

	// 최초 조우 BPM은 거리 기반 하한 아래로 내려가지 않는다.
	DistanceBPM = NewDistanceBPM;
	const float TargetFloorBPM = DistanceBPM > 0.0f ? DistanceBPM : FMath::Max(MinBPM, 1.0f);
	EncounterBPM = FMath::Max(TargetFloorBPM, FMath::FInterpConstantTo(
		EncounterBPM, TargetFloorBPM, DeltaTime, FMath::Max(BPMDecayPerSecond, 0.0f)));
	CurrentBPM = FMath::Max(DistanceBPM, EncounterBPM);

	if (bEnableHeartbeatLog &&
		FMath::Abs(CurrentBPM - LastLoggedBPM) >= FMath::Max(BPMLogThreshold, 0.0f))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Heartbeat] BPM=%.1f | DistanceFloor=%.1f | Encounter=%.1f | Active=%d"),
			CurrentBPM, DistanceBPM, EncounterBPM, ActiveCount);
		LastLoggedBPM = CurrentBPM;
	}
}

void UPlayerHeartbeatComponent::UpdateBeatLog(float DeltaTime)
{
	if (!bHeartbeatActive || CurrentBPM <= 0.0f)
	{
		return;
	}

	// BPM을 박동 간격(60 / BPM)으로 변환한다.
	BeatAccumulator += DeltaTime;
	const float BeatIntervalSeconds = 60.0f / FMath::Max(CurrentBPM, 1.0f);
	if (BeatAccumulator >= BeatIntervalSeconds)
	{
		BeatAccumulator = FMath::Fmod(BeatAccumulator, BeatIntervalSeconds);
		if (bEnableHeartbeatLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Heartbeat] THUMP | BPM=%.1f | Interval=%.3fs"),
				CurrentBPM, BeatIntervalSeconds);
		}
	}
}

void UPlayerHeartbeatComponent::RemoveInvalidMannequins()
{
	for (auto It = MannequinStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UPlayerHeartbeatComponent::GetPlayerViewPoint(
	const APawn& OwnerPawn, FVector& OutLocation, FVector& OutForward) const
{
	const AController* Controller = OwnerPawn.GetController();
	if (!IsValid(Controller))
	{
		return false;
	}
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(OutLocation, ViewRotation);
	OutForward = ViewRotation.Vector().GetSafeNormal();
	return !OutForward.IsNearlyZero();
}

bool UPlayerHeartbeatComponent::IsMannequinActuallyVisible(
	const APawn& OwnerPawn, const AMannequinAICharacter& Mannequin,
	const FVector& ViewLocation, const FVector& ViewForward) const
{
	// 몸 일부만 보여도 인지할 수 있도록 캡슐과 주요 뼈 위치를 검사한다.
	TArray<FVector, TInlineAllocator<12>> TestPoints;
	TestPoints.Add(Mannequin.GetActorLocation());
	if (const UCapsuleComponent* Capsule = Mannequin.GetCapsuleComponent(); IsValid(Capsule))
	{
		const FVector Center = Capsule->GetComponentLocation();
		const FVector Up = Capsule->GetUpVector();
		const FVector Right = Capsule->GetRightVector();
		TestPoints.Add(Center + Up * Capsule->GetScaledCapsuleHalfHeight());
		TestPoints.Add(Center - Up * Capsule->GetScaledCapsuleHalfHeight());
		TestPoints.Add(Center + Right * Capsule->GetScaledCapsuleRadius());
		TestPoints.Add(Center - Right * Capsule->GetScaledCapsuleRadius());
	}
	if (const USkeletalMeshComponent* Mesh = Mannequin.GetMesh(); IsValid(Mesh))
	{
		static const FName BoneNames[] =
		{
			TEXT("head"), TEXT("pelvis"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r")
		};
		for (const FName BoneName : BoneNames)
		{
			if (Mesh->GetBoneIndex(BoneName) != INDEX_NONE)
			{
				TestPoints.Add(Mesh->GetBoneLocation(BoneName));
			}
		}
	}

	const float MinimumViewDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(RecognitionHalfAngleDegrees, 0.0f, 180.0f)));
	FVector HorizontalViewForward = ViewForward;
	HorizontalViewForward.Z = 0.0f;
	HorizontalViewForward.Normalize();
	if (HorizontalViewForward.IsNearlyZero())
	{
		return false;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlayerHeartbeatRecognition), true, &OwnerPawn);

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	// 시야각 안의 검사점까지 추적해 마네킹이 직접 맞은 경우만 인정한다.
	for (const FVector& TestPoint : TestPoints)
	{
		FVector HorizontalViewToPoint = TestPoint - ViewLocation;
		HorizontalViewToPoint.Z = 0.0f;
		HorizontalViewToPoint.Normalize();
		if (HorizontalViewToPoint.IsNearlyZero() ||
			FVector::DotProduct(HorizontalViewForward, HorizontalViewToPoint) < MinimumViewDot)
		{
			continue;
		}
		FHitResult HitResult;
		if (World->LineTraceSingleByChannel(HitResult, ViewLocation, TestPoint,
			ECC_GameTraceChannel1, QueryParams) && HitResult.GetActor() == &Mannequin)
		{
			return true;
		}
	}
	return false;
}

bool UPlayerHeartbeatComponent::IsInsideRetentionCone(
	const AMannequinAICharacter& Mannequin,
	const FVector& ViewLocation, const FVector& ViewForward) const
{
	const FVector ViewToMannequin = (Mannequin.GetActorLocation() - ViewLocation).GetSafeNormal();
	if (ViewToMannequin.IsNearlyZero())
	{
		return true;
	}
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(RetentionHalfAngleDegrees, 0.0f, 180.0f)));
	return FVector::DotProduct(ViewForward, ViewToMannequin) >= MinimumDot;
}

bool UPlayerHeartbeatComponent::HasClearLineOfSight(
	const APawn& OwnerPawn, const AMannequinAICharacter& Mannequin, const FVector& ViewLocation) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PlayerHeartbeatRetention), true, &OwnerPawn);
	FHitResult HitResult;
	return World->LineTraceSingleByChannel(HitResult, ViewLocation, Mannequin.GetActorLocation(),
		ECC_GameTraceChannel1, QueryParams) && HitResult.GetActor() == &Mannequin;
}

float UPlayerHeartbeatComponent::CalculateDistanceAlpha(float Distance) const
{
	const float SafeRange = FMath::Max(HeartbeatRange, UE_SMALL_NUMBER);
	return 1.0f - FMath::Clamp(Distance / SafeRange, 0.0f, 1.0f);
}

float UPlayerHeartbeatComponent::CalculateDistanceBPM(float Distance) const
{
	const float SafeMinBPM = FMath::Max(MinBPM, 1.0f);
	const float SafeMaxBPM = FMath::Max(MaxDistanceBPM, SafeMinBPM);
	return FMath::Lerp(SafeMinBPM, SafeMaxBPM, FMath::Square(CalculateDistanceAlpha(Distance)));
}

void UPlayerHeartbeatComponent::TriggerSurpriseBPM(
	AMannequinAICharacter& Mannequin, float Distance, bool bFirstEncounter)
{
	const float NewDistanceBPM = CalculateDistanceBPM(Distance);
	const float DistanceAlpha = CalculateDistanceAlpha(Distance);
	const float SafeMinBoost = FMath::Max(MinEncounterBoost, 0.0f);
	const float SafeMaxBoost = FMath::Max(MaxEncounterBoost, SafeMinBoost);
	const float EncounterBoost = FMath::Lerp(SafeMinBoost, SafeMaxBoost, DistanceAlpha);
	const float NewEncounterBPM = FMath::Min(NewDistanceBPM + EncounterBoost,
		FMath::Max(MaxEncounterBPM, NewDistanceBPM));

	EncounterBPM = FMath::Max(EncounterBPM, NewEncounterBPM);
	bHeartbeatActive = true;
	if (bEnableHeartbeatLog)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Heartbeat] %s | Mannequin=%s | Distance=%.0fcm | DistanceBPM=%.1f | Boost=%.1f | EncounterBPM=%.1f"),
			bFirstEncounter ? TEXT("FIRST ENCOUNTER") : TEXT("REDISCOVERED"),
			*GetNameSafe(&Mannequin), Distance, NewDistanceBPM, EncounterBoost, NewEncounterBPM);
	}
}
