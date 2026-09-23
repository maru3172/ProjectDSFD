// File: Source/ProjectProject01/Private/ProjectProject01TuningData.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01TuningData.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HelperRearGuardCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "MannequinAICharacter.h"
#include "MultiplayTestGameMode.h"
#include "PlayerCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY(LogProjectProject01Tuning);

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectProject01TuningValidationTest,
	"ProjectProject01.Tuning.DataTableValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectProject01TuningValidationTest::RunTest(const FString& Parameters)
{
	FMannequinAITuningRow ValidMannequinRow;
	FString Error;
	TestTrue(TEXT("Default mannequin tuning is accepted"), ValidMannequinRow.IsValidForApplication(Error));

	FMannequinAITuningRow FlexibleMannequinRow = ValidMannequinRow;
	FlexibleMannequinRow.RoamingOuterRadius = 0.0f;
	TestTrue(TEXT("Non-negative mannequin values are accepted for tuning"), FlexibleMannequinRow.IsValidForApplication(Error));

	FHelperTuningRow ValidHelperRow;
	TestTrue(TEXT("Default helper tuning is accepted"), ValidHelperRow.IsValidForApplication(Error));

	FHelperTuningRow FlexibleHelperRow = ValidHelperRow;
	FlexibleHelperRow.FollowDistance = 500.0f;
	FlexibleHelperRow.MinimumFollowSeparation = 5000.0f;
	TestTrue(TEXT("Independent non-negative helper distances are accepted"), FlexibleHelperRow.IsValidForApplication(Error));

	FHelperTuningRow InvalidHelperRow = ValidHelperRow;
	InvalidHelperRow.FollowDistance = -1.0f;
	TestFalse(TEXT("Negative helper values are rejected"), InvalidHelperRow.IsValidForApplication(Error));
	return true;
}
#endif

namespace ProjectProject01Tuning
{
	const FName DefaultRowName(TEXT("Default"));
	const TCHAR* MannequinTablePath = TEXT("/Game/MyProject/Data/DT_AITuning.DT_AITuning");
	const TCHAR* HelperTablePath = TEXT("/Game/MyProject/Data/DT_HelperTuning.DT_HelperTuning");
}

bool FMannequinAITuningRow::IsValidForApplication(FString& OutError) const
{
	if (!FMath::IsFinite(PlayerWalkSpeed) || !FMath::IsFinite(MannequinWalkSpeed) ||
		!FMath::IsFinite(DirectChaseRadius) || !FMath::IsFinite(RoamingOuterRadius) ||
		!FMath::IsFinite(DirectChaseHalfAngleDegrees) ||
		!FMath::IsFinite(MannequinGatherRadius) || !FMath::IsFinite(SurvivorVisionCheckIntervalSeconds) ||
		!FMath::IsFinite(SurvivorVisionHalfAngleDegrees) || !FMath::IsFinite(DetectionMargin) ||
		!FMath::IsFinite(VisionFovMarginMultiplier) || PlayerWalkSpeed < 0.0f || MannequinWalkSpeed < 0.0f ||
		DirectChaseRadius < 0.0f ||
		RoamingOuterRadius < 0.0f || MannequinGatherRadius < 0.0f || RequiredMannequinCount < 0 ||
		DirectChaseHalfAngleDegrees < 0.0f || SurvivorVisionCheckIntervalSeconds < 0.0f || SurvivorVisionHalfAngleDegrees < 0.0f ||
		DetectionMargin < 0.0f || VisionFovMarginMultiplier < 0.0f)
	{
		OutError = TEXT("DT_AITuning Default row must contain finite, non-negative numeric values.");
		return false;
	}

	OutError.Reset();
	return true;
}

bool FHelperTuningRow::IsValidForApplication(FString& OutError) const
{
	if (!FMath::IsFinite(HelperWalkSpeed) || !FMath::IsFinite(FollowDistance) || !FMath::IsFinite(MinimumFollowSeparation) ||
		!FMath::IsFinite(GuardSightRadius) || !FMath::IsFinite(GuardHalfAngleDegrees) ||
		!FMath::IsFinite(RepathInterval) || !FMath::IsFinite(RepathDistance) ||
		!FMath::IsFinite(AcceptanceRadius) || !FMath::IsFinite(StuckTimeout) ||
		!FMath::IsFinite(StuckWaitTime) || !FMath::IsFinite(ProgressDistance) ||
		HelperWalkSpeed < 0.0f || FollowDistance < 0.0f || MinimumFollowSeparation < 0.0f || GuardSightRadius < 0.0f ||
		GuardHalfAngleDegrees < 0.0f || RepathInterval < 0.0f || RepathDistance < 0.0f ||
		AcceptanceRadius < 0.0f || StuckTimeout < 0.0f || StuckWaitTime < 0.0f || ProgressDistance < 0.0f)
	{
		OutError = TEXT("DT_HelperTuning Default row must contain finite, non-negative numeric values.");
		return false;
	}

	OutError.Reset();
	return true;
}

void UProjectProject01TuningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FString IgnoredError;
	LoadAndValidateTables(IgnoredError);
}

bool UProjectProject01TuningSubsystem::LoadAndValidateTables(FString& OutError)
{
	MannequinTuningTable = LoadObject<UDataTable>(nullptr, ProjectProject01Tuning::MannequinTablePath);
	HelperTuningTable = LoadObject<UDataTable>(nullptr, ProjectProject01Tuning::HelperTablePath);
	bHasValidMannequinTuning = false;
	bHasValidHelperTuning = false;

	if (!IsValid(MannequinTuningTable) || !IsValid(HelperTuningTable))
	{
		OutError = TEXT("Tuning DataTables are missing. Use Tools > ProjectProject01 Tuning > Reimport and Validate Tuning DataTables first.");
		return false;
	}

	const FMannequinAITuningRow* MannequinRow = MannequinTuningTable->FindRow<FMannequinAITuningRow>(
		ProjectProject01Tuning::DefaultRowName, TEXT("ProjectProject01 tuning validation"), false);
	const FHelperTuningRow* HelperRow = HelperTuningTable->FindRow<FHelperTuningRow>(
		ProjectProject01Tuning::DefaultRowName, TEXT("ProjectProject01 tuning validation"), false);
	if (MannequinRow == nullptr || HelperRow == nullptr)
	{
		OutError = TEXT("Both tuning DataTables must contain a row named Default.");
		return false;
	}

	if (!MannequinRow->IsValidForApplication(OutError) || !HelperRow->IsValidForApplication(OutError))
	{
		return false;
	}

	CachedMannequinTuning = *MannequinRow;
	CachedHelperTuning = *HelperRow;
	bHasValidMannequinTuning = true;
	bHasValidHelperTuning = true;
	OutError.Reset();
	return true;
}

bool UProjectProject01TuningSubsystem::ReloadFromDataTables(FString& OutError)
{
	return LoadAndValidateTables(OutError);
}

bool UProjectProject01TuningSubsystem::ApplyToCurrentWorld(FString& OutError)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
	{
		OutError = TEXT("Tuning can only be applied by a server or standalone world.");
		return false;
	}

	if (!LoadAndValidateTables(OutError))
	{
		return false;
	}

	TArray<AActor*> PlayerActors;
	UGameplayStatics::GetAllActorsOfClass(World, APlayerCharacter::StaticClass(), PlayerActors);
	for (AActor* Actor : PlayerActors)
	{
		if (APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(Actor))
		{
			PlayerCharacter->ApplyMannequinTuning(CachedMannequinTuning);
		}
	}

	TArray<AActor*> MannequinActors;
	UGameplayStatics::GetAllActorsOfClass(World, AMannequinAICharacter::StaticClass(), MannequinActors);
	for (AActor* Actor : MannequinActors)
	{
		if (AMannequinAICharacter* MannequinCharacter = Cast<AMannequinAICharacter>(Actor))
		{
			MannequinCharacter->ApplyMannequinTuning(CachedMannequinTuning);
		}
	}

	TArray<AActor*> HelperActors;
	UGameplayStatics::GetAllActorsOfClass(World, AHelperRearGuardCharacter::StaticClass(), HelperActors);
	for (AActor* Actor : HelperActors)
	{
		if (AHelperRearGuardCharacter* HelperCharacter = Cast<AHelperRearGuardCharacter>(Actor))
		{
			HelperCharacter->ApplyHelperTuning(CachedHelperTuning);
		}
	}

	if (AMultiplayTestGameMode* MultiplayGameMode = Cast<AMultiplayTestGameMode>(World->GetAuthGameMode()))
	{
		MultiplayGameMode->ApplyMannequinTuning(CachedMannequinTuning);
	}

	OutError.Reset();
	return true;
}

bool UProjectProject01TuningSubsystem::GetMannequinTuning(FMannequinAITuningRow& OutTuning) const
{
	if (!bHasValidMannequinTuning)
	{
		return false;
	}

	OutTuning = CachedMannequinTuning;
	return true;
}

bool UProjectProject01TuningSubsystem::GetHelperTuning(FHelperTuningRow& OutTuning) const
{
	if (!bHasValidHelperTuning)
	{
		return false;
	}

	OutTuning = CachedHelperTuning;
	return true;
}
