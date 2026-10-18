#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectProject01SaveSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectProject01SaveOperationCompleted, bool, bSucceeded, const FString&, Message);

USTRUCT(BlueprintType)
struct FProjectProject01SaveSlotInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 SlotIndex = 1;

	UPROPERTY(BlueprintReadOnly)
	bool bHasSave = false;

	UPROPERTY(BlueprintReadOnly)
	FName CheckpointId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	FString SavedMapPackageName;

	UPROPERTY(BlueprintReadOnly)
	int32 RemainingDeathCount = 0;

	UPROPERTY(BlueprintReadOnly)
	FDateTime SavedAtUtc;

	UPROPERTY(BlueprintReadOnly)
	FDateTime LastPlayedAtUtc;

	/** Decoded on demand, not part of replicated or saved gameplay state. */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<class UTexture2D> PreviewTexture;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01SaveSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SaveSlotCount = 3;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool SaveCheckpoint(class APlayerCharacter* Player, FName CheckpointId = NAME_None,
		bool bResetDeathCount = false);

	/** 수동 저장용입니다. 선택한 슬롯을 현재 플레이 슬롯으로 바꾸고 저장합니다. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool SaveCheckpointToSlot(class APlayerCharacter* Player, int32 SlotIndex,
		FName CheckpointId = NAME_None, bool bResetDeathCount = false);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool LoadLatestCheckpoint();

	/** 마지막으로 실제 플레이한 유효 저장소를 찾아 이어합니다. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool LoadMostRecentlyPlayedCheckpoint();

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool LoadCheckpointFromSlot(int32 SlotIndex);

	/** 슬롯 저장을 모두 지우고 이후 자동 저장 대상 슬롯으로 선택합니다. */
	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool StartNewGameInSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool DeleteSaveSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	bool HasRecoverableSave() const;

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	bool HasAnyRecoverableSave() const;

	/** 유효 저장소 중 마지막 플레이 시각이 가장 최근인 슬롯입니다. 없으면 INDEX_NONE입니다. */
	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	int32 GetMostRecentlyPlayedSaveSlot() const;

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	bool HasRecoverableSaveInSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	FProjectProject01SaveSlotInfo GetSaveSlotInfo(int32 SlotIndex, bool bIncludePreview = false) const;

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	int32 GetCurrentSlotIndex() const { return CurrentSlotIndex; }

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	void SetProgressValue(FName Key, int32 Value);

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	int32 GetProgressValue(FName Key, int32 DefaultValue = 0) const;

	UPROPERTY(BlueprintAssignable)
	FProjectProject01SaveOperationCompleted OnSaveOperationCompleted;

private:
	static constexpr int32 UserIndex = 0;
	static bool IsValidSlotIndex(int32 SlotIndex);
	static FString GetPrimarySlotName(int32 SlotIndex);
	static FString GetBackupSlotName(int32 SlotIndex);

	class UProjectProject01SinglePlayerSaveGame* LoadValidatedSlot(const FString& SlotName, FString& OutError) const;
	void HandleAsyncSaveComplete(const FString& SlotName, int32 InUserIndex, bool bSucceeded);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	bool ApplyPendingSave(UWorld* World, FString& OutError);
	FString BuildAppliedGameDataVersion(UWorld* World) const;
	void CaptureWorldState(UWorld* World, class UProjectProject01SinglePlayerSaveGame* Save);
	void RestoreWorldState(UWorld* World, const class UProjectProject01SinglePlayerSaveGame* Save);
	void CaptureSaveActorBaseline(UWorld* World);
	static bool ShouldCaptureActor(const AActor* Actor);
	static FName BuildActorSaveId(const AActor* Actor);

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01SinglePlayerSaveGame> PendingLoad;

	UPROPERTY()
	TMap<FName, int32> RuntimeProgressState;

	bool bSaveInProgress = false;
	FString PendingSaveSuccessMessage;
    FString PendingPrimarySaveSlot;
	int32 CurrentSlotIndex = 1;
	int32 PreviousSlotIndexBeforeSave = 1;
	FDelegateHandle PostLoadMapHandle;
	FString BaselineMapPackageName;
	TSet<FName> BaselineSaveActorIds;
};
