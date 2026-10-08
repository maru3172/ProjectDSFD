#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectProject01SaveSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectProject01SaveOperationCompleted, bool, bSucceeded, const FString&, Message);

UCLASS()
class PROJECTPROJECT01_API UProjectProject01SaveSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool SaveCheckpoint(class APlayerCharacter* Player, FName CheckpointId = NAME_None);

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	bool LoadLatestCheckpoint();

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	bool HasRecoverableSave() const;

	UFUNCTION(BlueprintCallable, Category="ProjectProject01|Save")
	void SetProgressValue(FName Key, int32 Value);

	UFUNCTION(BlueprintPure, Category="ProjectProject01|Save")
	int32 GetProgressValue(FName Key, int32 DefaultValue = 0) const;

	UPROPERTY(BlueprintAssignable)
	FProjectProject01SaveOperationCompleted OnSaveOperationCompleted;

private:
	static const FString PrimarySlot;
	static const FString BackupSlot;
	static constexpr int32 UserIndex = 0;

	class UProjectProject01SinglePlayerSaveGame* LoadValidatedSlot(const FString& SlotName, FString& OutError) const;
	void HandleAsyncSaveComplete(const FString& SlotName, int32 InUserIndex, bool bSucceeded);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	bool ApplyPendingSave(UWorld* World, FString& OutError);
	FString BuildAppliedGameDataVersion(UWorld* World) const;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectProject01SinglePlayerSaveGame> PendingLoad;

	UPROPERTY()
	TMap<FName, int32> RuntimeProgressState;

	bool bSaveInProgress = false;
	FDelegateHandle PostLoadMapHandle;
};
