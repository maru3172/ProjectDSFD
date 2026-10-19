#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ProjectProject01SinglePlayerSaveGame.generated.h"

USTRUCT()
struct FProjectProject01SavedComponentState
{
	GENERATED_BODY()

	UPROPERTY()
	FName ComponentName = NAME_None;

	UPROPERTY()
	FTransform RelativeTransform = FTransform::Identity;

	UPROPERTY()
	bool bIsSceneComponent = false;

	UPROPERTY()
	bool bActive = true;

	UPROPERTY()
	bool bTickEnabled = true;

	UPROPERTY()
	bool bVisible = true;

	UPROPERTY()
	TEnumAsByte<ECollisionEnabled::Type> CollisionEnabled = ECollisionEnabled::QueryAndPhysics;

	UPROPERTY()
	TArray<uint8> SaveGameData;
};

USTRUCT()
struct FProjectProject01SavedActorState
{
	GENERATED_BODY()

	UPROPERTY()
	FName ActorId = NAME_None;

	UPROPERTY()
	FString ActorClassPath;

	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	FVector LinearVelocity = FVector::ZeroVector;

	UPROPERTY()
	FVector AngularVelocityDegrees = FVector::ZeroVector;

	UPROPERTY()
	bool bMapPlacedActor = true;

	UPROPERTY()
	bool bHidden = false;

	UPROPERTY()
	bool bCollisionEnabled = true;

	UPROPERTY()
	bool bTickEnabled = true;

	UPROPERTY()
	TArray<uint8> SaveGameData;

	UPROPERTY()
	TArray<FProjectProject01SavedComponentState> Components;

	UPROPERTY()
	TArray<uint8> SinglePlayerAIData;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01SinglePlayerSaveGame final : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentSchemaVersion = 7;
	static constexpr int32 MinimumSupportedSchemaVersion = 1;

	UPROPERTY()
	FString Magic = TEXT("ProjectProject01.SinglePlayerSave");

	UPROPERTY()
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY()
	FString SavedMapPackageName;

	UPROPERTY()
	FName CheckpointId = NAME_None;

	UPROPERTY()
	FTransform PlayerTransform = FTransform::Identity;

	UPROPERTY()
	int32 RemainingDeathCount = 3;

	UPROPERTY()
	float CurrentStamina = 100.0f;

	UPROPERTY()
	FVector PlayerVelocity = FVector::ZeroVector;

	UPROPERTY()
	FRotator PlayerControlRotation = FRotator::ZeroRotator;

	UPROPERTY()
	double SavedWorldTimeSeconds = 0.0;

	UPROPERTY()
	TArray<FProjectProject01SavedActorState> ActorStates;

	UPROPERTY()
	TArray<FName> DestroyedActorIds;

	UPROPERTY()
	TMap<FName, int32> ProgressState;

	UPROPERTY()
	FString AppliedGameDataVersion;

	UPROPERTY()
	FDateTime SavedAtUtc;

	/** 이 저장소에서 실제로 플레이를 시작하거나 저장한 마지막 시각입니다. */
	UPROPERTY()
	FDateTime LastPlayedAtUtc;

	/** Monotonic disk generation; independent of wall-clock changes. v1-v5 use zero. */
	UPROPERTY()
	int64 SaveRevision = 0;

	UPROPERTY()
	uint32 IntegrityHash = 0;

	/** Optional preview has independent integrity so a damaged image cannot invalidate gameplay state. */
	UPROPERTY()
	TArray<uint8> PreviewPng;

	UPROPERTY()
	uint32 PreviewCrc = 0;

	uint32 CalculateIntegrityHash() const;
	bool HasValidIntegrity() const;
	bool MigrateToCurrentVersion(FString& OutError);
};

/** Recency is expendable metadata, never a reason to rewrite a world snapshot. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01CheckpointRecency final : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() uint32 SnapshotHash = 0;
	UPROPERTY() FDateTime LastPlayedAtUtc;
	UPROPERTY() uint32 Checksum = 0;
	uint32 CalculateChecksum() const;
};
