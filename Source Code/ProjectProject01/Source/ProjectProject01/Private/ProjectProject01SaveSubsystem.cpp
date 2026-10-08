#include "ProjectProject01SaveSubsystem.h"

#include "PlayerCharacter.h"
#include "ProjectProject01SinglePlayerSaveGame.h"
#include "ProjectProject01TuningData.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Crc.h"
#include "JsonObjectConverter.h"
#include "TimerManager.h"

const FString UProjectProject01SaveSubsystem::PrimarySlot(TEXT("SinglePlayerCheckpoint"));
const FString UProjectProject01SaveSubsystem::BackupSlot(TEXT("SinglePlayerCheckpoint_Backup"));

void UProjectProject01SaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UProjectProject01SaveSubsystem::HandlePostLoadMap);
}

void UProjectProject01SaveSubsystem::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	PendingLoad = nullptr;
	Super::Deinitialize();
}

bool UProjectProject01SaveSubsystem::SaveCheckpoint(APlayerCharacter* Player, const FName CheckpointId)
{
	UWorld* World = GetWorld();
	if (bSaveInProgress)
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("이미 저장 중입니다."));
		return false;
	}
	if (!IsValid(World) || World->GetNetMode() != NM_Standalone || !IsValid(Player))
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("싱글플레이 플레이어만 저장할 수 있습니다."));
		return false;
	}

	if (UGameplayStatics::DoesSaveGameExist(PrimarySlot, UserIndex))
	{
		FString ExistingError;
		if (UProjectProject01SinglePlayerSaveGame* Existing = LoadValidatedSlot(PrimarySlot, ExistingError))
		{
			UGameplayStatics::SaveGameToSlot(Existing, BackupSlot, UserIndex);
		}
	}

	UProjectProject01SinglePlayerSaveGame* Save = Cast<UProjectProject01SinglePlayerSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UProjectProject01SinglePlayerSaveGame::StaticClass()));
	if (!ensureMsgf(IsValid(Save), TEXT("Failed to allocate single-player save object.")))
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("저장 데이터를 만들지 못했습니다."));
		return false;
	}

	Save->SavedMapPackageName = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	Save->CheckpointId = CheckpointId;
	Save->PlayerTransform = Player->GetActorTransform();
	Save->RemainingDeathCount = Player->GetRemainingDeathCount();
	Save->CurrentStamina = Player->GetCurrentStamina();
	Save->ProgressState = RuntimeProgressState;
	Save->AppliedGameDataVersion = BuildAppliedGameDataVersion(World);
	Save->SavedAtUtc = FDateTime::UtcNow();
	Save->IntegrityHash = Save->CalculateIntegrityHash();

	bSaveInProgress = true;
	FAsyncSaveGameToSlotDelegate Delegate;
	Delegate.BindUObject(this, &UProjectProject01SaveSubsystem::HandleAsyncSaveComplete);
	UGameplayStatics::AsyncSaveGameToSlot(Save, PrimarySlot, UserIndex, Delegate);
	return true;
}

bool UProjectProject01SaveSubsystem::LoadLatestCheckpoint()
{
	if (bSaveInProgress)
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("저장이 끝난 뒤 불러올 수 있습니다."));
		return false;
	}

	FString Error;
	PendingLoad = LoadValidatedSlot(PrimarySlot, Error);
	if (!IsValid(PendingLoad))
	{
		const FString PrimaryError = Error;
		PendingLoad = LoadValidatedSlot(BackupSlot, Error);
		if (!IsValid(PendingLoad))
		{
			OnSaveOperationCompleted.Broadcast(false,
				FString::Printf(TEXT("저장 파일을 복구하지 못했습니다. 주 슬롯: %s / 백업: %s"),
					*PrimaryError, *Error));
			return false;
		}
	}

	RuntimeProgressState = PendingLoad->ProgressState;
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		PendingLoad = nullptr;
		OnSaveOperationCompleted.Broadcast(false, TEXT("현재 월드를 찾지 못했습니다."));
		return false;
	}

	const FString CurrentMap = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	if (!CurrentMap.Equals(PendingLoad->SavedMapPackageName, ESearchCase::IgnoreCase))
	{
		UGameplayStatics::OpenLevel(this, FName(*PendingLoad->SavedMapPackageName), true);
		return true;
	}

	if (!ApplyPendingSave(World, Error))
	{
		PendingLoad = nullptr;
		OnSaveOperationCompleted.Broadcast(false, Error);
		return false;
	}
	OnSaveOperationCompleted.Broadcast(true, TEXT("마지막 체크포인트를 불러왔습니다."));
	return true;
}

bool UProjectProject01SaveSubsystem::HasRecoverableSave() const
{
	FString Ignored;
	return IsValid(LoadValidatedSlot(PrimarySlot, Ignored)) || IsValid(LoadValidatedSlot(BackupSlot, Ignored));
}

void UProjectProject01SaveSubsystem::SetProgressValue(const FName Key, const int32 Value)
{
	if (!Key.IsNone())
	{
		RuntimeProgressState.FindOrAdd(Key) = Value;
	}
}

int32 UProjectProject01SaveSubsystem::GetProgressValue(const FName Key, const int32 DefaultValue) const
{
	const int32* Value = RuntimeProgressState.Find(Key);
	return Value != nullptr ? *Value : DefaultValue;
}

UProjectProject01SinglePlayerSaveGame* UProjectProject01SaveSubsystem::LoadValidatedSlot(
	const FString& SlotName,
	FString& OutError) const
{
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		OutError = TEXT("파일 없음");
		return nullptr;
	}
	UProjectProject01SinglePlayerSaveGame* Save = Cast<UProjectProject01SinglePlayerSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	if (!IsValid(Save))
	{
		OutError = TEXT("형식 손상");
		return nullptr;
	}
	if (!Save->HasValidIntegrity())
	{
		OutError = TEXT("무결성 검사 실패");
		return nullptr;
	}
	if (!Save->MigrateToCurrentVersion(OutError))
	{
		return nullptr;
	}
	if (Save->SavedMapPackageName.IsEmpty())
	{
		OutError = TEXT("저장된 맵 없음");
		return nullptr;
	}
	OutError.Reset();
	return Save;
}

void UProjectProject01SaveSubsystem::HandleAsyncSaveComplete(
	const FString& SlotName,
	const int32 InUserIndex,
	const bool bSucceeded)
{
	bSaveInProgress = false;
	OnSaveOperationCompleted.Broadcast(bSucceeded,
		bSucceeded ? TEXT("체크포인트를 저장했습니다.") : TEXT("체크포인트 저장에 실패했습니다."));
}

void UProjectProject01SaveSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!IsValid(PendingLoad) || !IsValid(LoadedWorld))
	{
		return;
	}
	const TWeakObjectPtr<UProjectProject01SaveSubsystem> WeakThis(this);
	const TWeakObjectPtr<UWorld> WeakWorld(LoadedWorld);
	LoadedWorld->GetTimerManager().SetTimerForNextTick([WeakThis, WeakWorld]()
	{
		UProjectProject01SaveSubsystem* Self = WeakThis.Get();
		UWorld* World = WeakWorld.Get();
		if (!IsValid(Self) || !IsValid(World)) return;
		FString Error;
		const bool bApplied = Self->ApplyPendingSave(World, Error);
		Self->OnSaveOperationCompleted.Broadcast(bApplied,
			bApplied ? TEXT("마지막 체크포인트를 불러왔습니다.") : Error);
	});
}

bool UProjectProject01SaveSubsystem::ApplyPendingSave(UWorld* World, FString& OutError)
{
	if (!IsValid(PendingLoad) || !IsValid(World) || World->GetNetMode() != NM_Standalone)
	{
		OutError = TEXT("싱글플레이 저장 상태를 적용할 수 없습니다.");
		return false;
	}
	APlayerCharacter* Player = Cast<APlayerCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0));
	if (!IsValid(Player))
	{
		OutError = TEXT("복구할 플레이어를 찾지 못했습니다.");
		return false;
	}
	Player->RestoreSinglePlayerState(PendingLoad->PlayerTransform,
		PendingLoad->RemainingDeathCount, PendingLoad->CurrentStamina);
	RuntimeProgressState = PendingLoad->ProgressState;
	PendingLoad = nullptr;
	OutError.Reset();
	return true;
}

FString UProjectProject01SaveSubsystem::BuildAppliedGameDataVersion(UWorld* World) const
{
	FString ProjectVersion;
	if (GConfig != nullptr)
	{
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"),
			TEXT("ProjectVersion"), ProjectVersion, GGameIni);
	}
	if (ProjectVersion.IsEmpty()) ProjectVersion = TEXT("Unversioned");
	if (!IsValid(World))
	{
		return ProjectVersion;
	}
	const UProjectProject01TuningSubsystem* Tuning = World->GetSubsystem<UProjectProject01TuningSubsystem>();
	FPlayerTuningRow PlayerRow;
	FMannequinAITuningRow AI;
	FHelperTuningRow Helper;
	if (!IsValid(Tuning) || !Tuning->GetPlayerTuning(PlayerRow) ||
		!Tuning->GetMannequinTuning(AI) || !Tuning->GetHelperTuning(Helper))
	{
		return ProjectVersion + TEXT(":TuningUnavailable");
	}
	FString PlayerJson;
	FString AIJson;
	FString HelperJson;
	FJsonObjectConverter::UStructToJsonObjectString(PlayerRow, PlayerJson);
	FJsonObjectConverter::UStructToJsonObjectString(AI, AIJson);
	FJsonObjectConverter::UStructToJsonObjectString(Helper, HelperJson);
	const uint32 PlayerCrc = FCrc::StrCrc32(*PlayerJson);
	const uint32 AICrc = FCrc::StrCrc32(*AIJson);
	const uint32 HelperCrc = FCrc::StrCrc32(*HelperJson);
	return FString::Printf(TEXT("%s:P%08X-A%08X-H%08X"), *ProjectVersion, PlayerCrc, AICrc, HelperCrc);
}
