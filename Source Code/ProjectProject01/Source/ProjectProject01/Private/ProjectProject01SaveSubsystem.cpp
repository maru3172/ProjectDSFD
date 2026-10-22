#include "ProjectProject01SaveSubsystem.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "PlayerCharacter.h"
#include "HelperRearGuardCharacter.h"
#include "MannequinAICharacter.h"
#include "MannequinReactiveLight.h"
#include "ProjectProject01Checkpoint.h"
#include "ProjectProject01CheckpointSaveable.h"
#include "ProjectProject01SinglePlayerSaveGame.h"
#include "ProjectProject01CheckpointPreview.h"
#include "ProjectProject01CheckpointStore.h"
#include "ProjectProject01CheckpointAI.h"
#include "ProjectProject01TuningData.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "JsonObjectConverter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "TimerManager.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"

namespace ProjectProject01CheckpointSave
{
	const FName SaveActorTag(TEXT("ProjectProject01CheckpointState"));

	void SerializeSaveGameProperties(UObject* Object, TArray<uint8>& OutData)
	{
		OutData.Reset();
		if (!IsValid(Object)) return;
		FMemoryWriter Writer(OutData, true);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		Archive.ArIsSaveGame = true;
		Archive.ArNoDelta = true;
		Object->Serialize(Archive);
	}

	void DeserializeSaveGameProperties(UObject* Object, const TArray<uint8>& Data)
	{
		if (!IsValid(Object) || Data.IsEmpty()) return;
		TArray<uint8> MutableData = Data;
		FMemoryReader Reader(MutableData, true);
		FObjectAndNameAsStringProxyArchive Archive(Reader, true);
		Archive.ArIsSaveGame = true;
		Archive.ArNoDelta = true;
		Object->Serialize(Archive);
	}
}

void UProjectProject01SaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UProjectProject01SaveSubsystem::HandlePostLoadMap);
    if(GEngine)LoadTravelFailureHandle=GEngine->OnTravelFailure().AddUObject(this,&ThisClass::HandleLoadTravelFailure);
}

void UProjectProject01SaveSubsystem::Deinitialize()
{
    if(GEngine)GEngine->OnTravelFailure().Remove(LoadTravelFailureHandle);
    FTSTicker::GetCoreTicker().RemoveTicker(PendingLoadDeadline);
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	PendingLoad = nullptr;
	Super::Deinitialize();
}

bool UProjectProject01SaveSubsystem::SaveCheckpoint(
	APlayerCharacter* Player,
	const FName CheckpointId,
	const bool bResetDeathCount)
{
	return SaveCheckpointToSlot(Player, CurrentSlotIndex, CheckpointId, bResetDeathCount);
}

bool UProjectProject01SaveSubsystem::SaveCheckpointToSlot(
	APlayerCharacter* Player,
	const int32 SlotIndex,
	const FName CheckpointId,
	const bool bResetDeathCount)
{
	UWorld* World = GetWorld();
	if (!IsValidSlotIndex(SlotIndex))
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("잘못된 저장소입니다."));
		return false;
	}
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
    FString IdentityError;
    if(!ValidateActorIdentities(World,IdentityError)){OnSaveOperationCompleted.Broadcast(false,IdentityError);return false;}
	if (bResetDeathCount)
	{
		Player->ResetDeathCountAtCheckpoint();
	}
	const FString PrimarySlot = GetPrimarySlotName(SlotIndex);
	const auto Current = ProjectCheckpointStore::SelectNewest(PrimarySlot, [this](const FString& Slot)
	{
		FString Error;
		return LoadValidatedSlot(Slot, Error);
	});
	if (Current.Save && Current.Save->SaveRevision == MAX_int64)
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("저장 세대 번호를 갱신할 수 없습니다. 기존 저장은 보존했습니다."));
		return false;
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
	Save->PlayerVelocity = Player->GetVelocity();
	if (const AController* Controller = Player->GetController(); IsValid(Controller))
	{
		Save->PlayerControlRotation = Controller->GetControlRotation();
	}
	Save->SavedWorldTimeSeconds = World->GetTimeSeconds();
	Save->ProgressState = RuntimeProgressState;
	CaptureWorldState(World, Save);
	Save->AppliedGameDataVersion = BuildAppliedGameDataVersion(World);
	const FDateTime SaveTimeUtc = FDateTime::UtcNow();
	Save->SavedAtUtc = SaveTimeUtc;
	Save->LastPlayedAtUtc = SaveTimeUtc;
	Save->SaveRevision = Current.Save ? Current.Save->SaveRevision + 1 : 1;
	ProjectCheckpointPreview::Capture(World, Cast<APlayerController>(Player->GetController()), Save->PreviewPng);
	Save->PreviewCrc = Save->PreviewPng.IsEmpty() ? 0 : FCrc::MemCrc32(Save->PreviewPng.GetData(), Save->PreviewPng.Num());
	Save->IntegrityHash = Save->CalculateIntegrityHash();

	PreviousSlotIndexBeforeSave = CurrentSlotIndex;
	CurrentSlotIndex = SlotIndex;
	PendingSaveSuccessMessage = CheckpointId == TEXT("ManualPauseMenu")
		? TEXT("체크포인트가 수동 저장되었습니다.")
		: TEXT("체크포인트가 자동 저장되었습니다.");
	bSaveInProgress = true;
	FAsyncSaveGameToSlotDelegate Delegate;
	Delegate.BindUObject(this, &UProjectProject01SaveSubsystem::HandleAsyncSaveComplete);
	PendingWriteSlot = ProjectCheckpointStore::NextWriteSlot(PrimarySlot, Current);
	PendingSaveRevision = Save->SaveRevision;
	PendingSaveHash = Save->IntegrityHash;
	auto Bytes = MakeShared<TArray<uint8>>();
	auto* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (SaveSystem && ProjectCheckpointStore::Encode(Save, *Bytes))
	{
		SaveSystem->SaveGameAsync(false, *PendingWriteSlot, FPlatformMisc::GetPlatformUserForUserIndex(UserIndex), Bytes,
			[Delegate](const FString& Slot, FPlatformUserId, const bool bSucceeded)
			{
				check(IsInGameThread());
				Delegate.ExecuteIfBound(Slot, UserIndex, bSucceeded);
			});
	}
	else Delegate.ExecuteIfBound(PendingWriteSlot, UserIndex, false);
	return true;
}

bool UProjectProject01SaveSubsystem::LoadLatestCheckpoint()
{
	return LoadCheckpointFromSlot(CurrentSlotIndex);
}

bool UProjectProject01SaveSubsystem::LoadMostRecentlyPlayedCheckpoint()
{
	const int32 SlotIndex = GetMostRecentlyPlayedSaveSlot();
	return SlotIndex != INDEX_NONE && LoadCheckpointFromSlot(SlotIndex);
}

bool UProjectProject01SaveSubsystem::LoadCheckpointFromSlot(const int32 SlotIndex)
{
    if(PendingLoad){OnSaveOperationCompleted.Broadcast(false,TEXT("이미 저장 상태를 불러오는 중입니다."));return false;}
	if (!IsValidSlotIndex(SlotIndex))
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("잘못된 저장소입니다."));
		return false;
	}
	if (bSaveInProgress)
	{
		OnSaveOperationCompleted.Broadcast(false, TEXT("저장이 끝난 뒤 불러올 수 있습니다."));
		return false;
	}

	FString Error;
	PendingLoad = LoadBestSlot(SlotIndex, Error);
	if (!IsValid(PendingLoad))
	{
		OnSaveOperationCompleted.Broadcast(false, FString::Printf(TEXT("저장 파일을 복구하지 못했습니다. %s"), *Error));
		return false;
	}
	if (!FPackageName::IsValidLongPackageName(PendingLoad->SavedMapPackageName) ||
		!FPackageName::DoesPackageExist(PendingLoad->SavedMapPackageName))
	{
		PendingLoad = nullptr;
		OnSaveOperationCompleted.Broadcast(false, TEXT("저장된 맵을 찾지 못했습니다. 저장 파일은 변경하지 않았습니다."));
		return false;
	}
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		PendingLoad = nullptr;
		OnSaveOperationCompleted.Broadcast(false, TEXT("현재 월드를 찾지 못했습니다."));
		return false;
	}
    PreviousSlotBeforeLoad=CurrentSlotIndex;
	CurrentSlotIndex = SlotIndex;
    PendingLoadDeadline=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float)
    {CancelPendingLoad(TEXT("체크포인트 맵 불러오기 시간이 초과되었습니다. 기존 저장은 보존했습니다."));return false;}),30.0f);

	// 같은 맵이어도 다시 열어 파괴된 액터, AI 트리, 타이머를 깨끗한 기준 상태로 만든 뒤
	// 저장 스냅샷을 적용한다. 일부만 덮어써서 이전 플레이 상태가 섞이는 것을 막는다.
	UGameplayStatics::OpenLevel(this, FName(*PendingLoad->SavedMapPackageName), true);
	return true;
}

bool UProjectProject01SaveSubsystem::HasRecoverableSave() const
{
	return HasRecoverableSaveInSlot(CurrentSlotIndex);
}

bool UProjectProject01SaveSubsystem::HasAnyRecoverableSave() const
{
	return GetMostRecentlyPlayedSaveSlot() != INDEX_NONE;
}

int32 UProjectProject01SaveSubsystem::GetMostRecentlyPlayedSaveSlot() const
{
	int32 MostRecentSlot = INDEX_NONE;
	FDateTime MostRecentPlayTime = FDateTime::MinValue();
	FDateTime MostRecentSaveTime = FDateTime::MinValue();
	for (int32 SlotIndex = 1; SlotIndex <= SaveSlotCount; ++SlotIndex)
	{
		const FProjectProject01SaveSlotInfo Info = GetSaveSlotInfo(SlotIndex);
		if (!Info.bHasSave) continue;
		const bool bIsNewer = MostRecentSlot == INDEX_NONE ||
			Info.LastPlayedAtUtc > MostRecentPlayTime ||
			(Info.LastPlayedAtUtc == MostRecentPlayTime && Info.SavedAtUtc > MostRecentSaveTime);
		if (bIsNewer)
		{
			MostRecentSlot = SlotIndex;
			MostRecentPlayTime = Info.LastPlayedAtUtc;
			MostRecentSaveTime = Info.SavedAtUtc;
		}
	}
	return MostRecentSlot;
}

bool UProjectProject01SaveSubsystem::HasRecoverableSaveInSlot(const int32 SlotIndex) const
{
	// Same answer as IsValid(LoadBestSlot(...)): GetSaveSlotInfo sets bHasSave exactly when that load succeeds.
	return IsValidSlotIndex(SlotIndex) && GetSaveSlotInfo(SlotIndex).bHasSave;
}

bool UProjectProject01SaveSubsystem::BuildSlotFileFingerprint(const int32 SlotIndex, FString& OutFingerprint) const
{
#if PLATFORM_DESKTOP
	// FGenericSaveGameSystem path: <ProjectSaved>/SaveGames/<Slot>.sav (Engine/Public/SaveGameSystem.h).
	OutFingerprint.Reset();
	IFileManager& FileManager = IFileManager::Get();
	for (const FString& Slot : ProjectCheckpointStore::AllSlots(GetPrimarySlotName(SlotIndex)))
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".sav"));
		const int64 Size = FileManager.FileSize(*Path);
		const int64 Ticks = Size >= 0 ? FileManager.GetTimeStamp(*Path).GetTicks() : 0;
		// A slot the save system reports but this path does not show means another save backend: do not cache.
		if (Size < 0 && UGameplayStatics::DoesSaveGameExist(Slot, UserIndex)) return false;
		OutFingerprint += FString::Printf(TEXT("%s=%lld:%lld;"), *Slot, Size, Ticks);
	}
	return true;
#else
	return false;
#endif
}

FProjectProject01SaveSlotInfo UProjectProject01SaveSubsystem::GetSaveSlotInfo(const int32 SlotIndex, const bool bIncludePreview) const
{
	FProjectProject01SaveSlotInfo Info;
	Info.SlotIndex = SlotIndex;
	if (!IsValidSlotIndex(SlotIndex)) return Info;
	FString Fingerprint;
	const bool bCacheable = !bIncludePreview && BuildSlotFileFingerprint(SlotIndex, Fingerprint);
	if (bCacheable)
	{
		if (const FSlotInfoCacheEntry* Cached = SlotInfoCache.Find(SlotIndex); Cached && Cached->Fingerprint == Fingerprint)
		{
			return Cached->Info;
		}
	}
	FString Ignored;
	UProjectProject01SinglePlayerSaveGame* Save = LoadBestSlot(SlotIndex, Ignored);
	if (!IsValid(Save))
	{
		if (bCacheable) SlotInfoCache.Add(SlotIndex, {Fingerprint, Info});
		return Info;
	}
	Info.bHasSave = true;
	Info.CheckpointId = Save->CheckpointId;
	Info.SavedMapPackageName = Save->SavedMapPackageName;
	Info.RemainingDeathCount = Save->RemainingDeathCount;
	Info.SavedAtUtc = Save->SavedAtUtc;
	Info.LastPlayedAtUtc = ReadLastPlayed(SlotIndex, Save);
	if (bIncludePreview) Info.PreviewTexture = ProjectCheckpointPreview::Decode(Save->PreviewPng, Save->PreviewCrc);
	if (bCacheable) SlotInfoCache.Add(SlotIndex, {Fingerprint, Info});
	return Info;
}

bool UProjectProject01SaveSubsystem::DeleteSaveSlot(const int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex) || bSaveInProgress) return false;
	bool bDeleted = true;
	for (const FString& Slot : ProjectCheckpointStore::AllSlots(GetPrimarySlotName(SlotIndex)))
	{
		if (UGameplayStatics::DoesSaveGameExist(Slot, UserIndex) && !UGameplayStatics::DeleteGameInSlot(Slot, UserIndex)) bDeleted = false;
	}
	if (SlotIndex == CurrentSlotIndex && bDeleted)
	{
		PendingLoad = nullptr;
		RuntimeProgressState.Reset();
	}
	return bDeleted;
}

bool UProjectProject01SaveSubsystem::StartNewGameInSlot(const int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex) || !DeleteSaveSlot(SlotIndex)) return false;
	CurrentSlotIndex = SlotIndex;
	PendingLoad = nullptr;
	RuntimeProgressState.Reset();
	return true;
}

bool UProjectProject01SaveSubsystem::IsValidSlotIndex(const int32 SlotIndex)
{
	return SlotIndex >= 1 && SlotIndex <= SaveSlotCount;
}

FString UProjectProject01SaveSubsystem::GetPrimarySlotName(const int32 SlotIndex) const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!TestSlotNamespace.IsEmpty()) return FString::Printf(TEXT("ReviewCheckpoint_%s_%d"), *TestSlotNamespace, SlotIndex);
#endif
	// 1번은 기존 단일 저장 파일과 호환되도록 이름을 유지한다.
	return SlotIndex == 1 ? TEXT("SinglePlayerCheckpoint") :
		FString::Printf(TEXT("SinglePlayerCheckpoint_%d"), SlotIndex);
}

UProjectProject01SinglePlayerSaveGame* UProjectProject01SaveSubsystem::LoadBestSlot(const int32 SlotIndex, FString& OutError) const
{
	const auto Best = ProjectCheckpointStore::SelectNewest(GetPrimarySlotName(SlotIndex), [this, &OutError](const FString& Slot)
	{
		return LoadValidatedSlot(Slot, OutError);
	});
	if (Best.Save) OutError.Reset();
	return Best.Save;
}

FDateTime UProjectProject01SaveSubsystem::ReadLastPlayed(const int32 SlotIndex, const UProjectProject01SinglePlayerSaveGame* Save) const
{
	TArray<uint8> Bytes;
	UGameplayStatics::LoadDataFromSlot(Bytes, GetPrimarySlotName(SlotIndex) + TEXT("_Recent"), UserIndex);
	auto* Metadata = Cast<UProjectProject01CheckpointRecency>(ProjectCheckpointStore::Decode(Bytes));
	return Metadata && Metadata->SnapshotHash == Save->IntegrityHash && Metadata->Checksum == Metadata->CalculateChecksum()
		? FMath::Max(Metadata->LastPlayedAtUtc, Save->LastPlayedAtUtc) : Save->LastPlayedAtUtc;
}

bool UProjectProject01SaveSubsystem::RecordLastPlayed(const int32 SlotIndex, const UProjectProject01SinglePlayerSaveGame* Save)
{
	auto* Metadata = NewObject<UProjectProject01CheckpointRecency>();
	Metadata->SnapshotHash = Save->IntegrityHash;
	Metadata->LastPlayedAtUtc = FDateTime::UtcNow();
	Metadata->Checksum = Metadata->CalculateChecksum();
	TArray<uint8> Bytes;
	return ProjectCheckpointStore::Encode(Metadata, Bytes) && UGameplayStatics::SaveDataToSlot(Bytes, GetPrimarySlotName(SlotIndex) + TEXT("_Recent"), UserIndex);
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
	TArray<uint8> Bytes;
	UGameplayStatics::LoadDataFromSlot(Bytes, SlotName, UserIndex);
	const bool bGeneration = SlotName.EndsWith(TEXT("_GenerationA")) || SlotName.EndsWith(TEXT("_GenerationB"));
	UProjectProject01SinglePlayerSaveGame* Save = Cast<UProjectProject01SinglePlayerSaveGame>(
		bGeneration ? ProjectCheckpointStore::Decode(Bytes) : UGameplayStatics::LoadGameFromMemory(Bytes));
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
    TSet<FName> Ids;
    for(const auto& State:Save->ActorStates)
    {if(State.ActorId.IsNone()||Ids.Contains(State.ActorId)){OutError=TEXT("저장 액터 ID 중복");return nullptr;}Ids.Add(State.ActorId);}
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
	if (!bSaveInProgress || SlotName != PendingWriteSlot || InUserIndex != UserIndex) return;
	FString ValidationError;
	const auto* Verified = bSucceeded ? LoadValidatedSlot(SlotName, ValidationError) : nullptr;
	const bool bCommitted = Verified && Verified->SaveRevision == PendingSaveRevision && Verified->IntegrityHash == PendingSaveHash;
	PendingWriteSlot.Reset();
	PendingSaveRevision = 0;
	PendingSaveHash = 0;
	bSaveInProgress = false;
	if (!bCommitted)
	{
		CurrentSlotIndex = PreviousSlotIndexBeforeSave;
	}
	const FString CompletionMessage = bCommitted && !PendingSaveSuccessMessage.IsEmpty()
		? PendingSaveSuccessMessage
		: TEXT("체크포인트 저장에 실패했습니다.");
	PendingSaveSuccessMessage.Reset();
	OnSaveOperationCompleted.Broadcast(bCommitted, CompletionMessage);
}

void UProjectProject01SaveSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!IsValid(LoadedWorld) || LoadedWorld->GetGameInstance()!=GetGameInstance() || LoadedWorld->GetNetMode() != NM_Standalone)
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
		Self->RestoredActorSaveIds.Reset();
		Self->CaptureSaveActorBaseline(World);
		if (!IsValid(Self->PendingLoad)) return;
		FString Error;
		const bool bApplied = Self->ApplyPendingSave(World, Error);
        if(!bApplied){Self->CancelPendingLoad(Error);return;}
		Self->OnSaveOperationCompleted.Broadcast(bApplied,
			Error.IsEmpty()?TEXT("마지막 체크포인트를 불러왔습니다."):Error);
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
	// 체크포인트의 bTriggered 등 월드 상태를 먼저 되살린 뒤 플레이어를 영역 안으로 이동한다.
    TArray<FString> Blocked;
    if(GConfig)GConfig->GetArray(TEXT("ProjectProject01.CheckpointCompatibility"),TEXT("BlockedAppliedVersions"),Blocked,GGameIni);
    FString Notice;
    if(!CheckDataCompatibility(PendingLoad->AppliedGameDataVersion,BuildAppliedGameDataVersion(World),Blocked,Notice))
    {OutError=Notice;return false;}
    if(!ValidateActorIdentities(World,OutError))return false;
	// 반대 순서면 복원 이동 자체가 BeginOverlap을 발생시켜 저장/초기화가 한 번 더 실행될 수 있다.
	RestoreWorldState(World, PendingLoad);
	Player->RestoreSinglePlayerState(PendingLoad->PlayerTransform,
		PendingLoad->RemainingDeathCount, PendingLoad->CurrentStamina,
		PendingLoad->PlayerVelocity, PendingLoad->PlayerControlRotation);
	RestoreAIState(World, PendingLoad);
	RuntimeProgressState = PendingLoad->ProgressState;
	// Failure here only loses recency; it can never damage the world snapshot.
	if (!RecordLastPlayed(CurrentSlotIndex, PendingLoad))
		UE_LOG(LogTemp, Warning, TEXT("Could not persist last-played metadata for single-player slot %d."), CurrentSlotIndex);
	PendingLoad = nullptr;
    FTSTicker::GetCoreTicker().RemoveTicker(PendingLoadDeadline);PendingLoadDeadline.Reset();
	OutError=Notice;
	return true;
}

bool UProjectProject01SaveSubsystem::CheckDataCompatibility(const FString& Saved,const FString& Current,const TArray<FString>& Blocked,FString& Notice)
{
    if(Blocked.Contains(Saved)){Notice=TEXT("이 버전의 저장 데이터는 현재 콘텐츠와 호환되지 않습니다. 기존 파일은 보존했습니다.");return false;}
    Notice=Saved==Current?FString():TEXT("체크포인트를 불러왔습니다. 저장 당시와 게임 데이터 버전이 달라 일부 조정값이 다를 수 있습니다.");return true;
}
bool UProjectProject01SaveSubsystem::ValidateActorIdentities(UWorld* World,FString& Error) const
{
    TSet<FName> Ids;
    for(TActorIterator<AActor> It(World);It;++It)if(ShouldCaptureActor(*It))
    {
        FName Id=BuildActorSaveId(*It);
        if(Id.IsNone()||Ids.Contains(Id)){Error=TEXT("저장 액터 식별자가 중복되어 복원을 중단했습니다. 기존 저장은 보존했습니다.");return false;}
        Ids.Add(Id);
    }
    return true;
}
void UProjectProject01SaveSubsystem::CancelPendingLoad(const FString& Error)
{
    if(!PendingLoad)return;
    PendingLoad=nullptr;CurrentSlotIndex=PreviousSlotBeforeLoad;
    FTSTicker::GetCoreTicker().RemoveTicker(PendingLoadDeadline);PendingLoadDeadline.Reset();
    OnSaveOperationCompleted.Broadcast(false,Error);
}
void UProjectProject01SaveSubsystem::HandleLoadTravelFailure(UWorld* World,ETravelFailure::Type,const FString&)
{
    if(World&&World->GetGameInstance()==GetGameInstance())CancelPendingLoad(TEXT("저장된 맵 이동에 실패했습니다. 기존 저장은 보존했습니다."));
}

bool UProjectProject01SaveSubsystem::ShouldCaptureActor(const AActor* Actor)
{
	return IsValid(Actor) && !Actor->IsTemplate() && !Actor->IsA<APlayerCharacter>() &&
		(Actor->ActorHasTag(ProjectProject01CheckpointSave::SaveActorTag) ||
		 Actor->GetClass()->ImplementsInterface(UProjectProject01CheckpointSaveable::StaticClass()) ||
		 Actor->IsA<AMannequinAICharacter>() ||
		 Actor->IsA<AHelperRearGuardCharacter>() ||
		 Actor->IsA<AMannequinReactiveLight>());
}

FName UProjectProject01SaveSubsystem::BuildActorSaveId(const AActor* Actor) const
{
	if (!IsValid(Actor)) return NAME_None;
	if (Actor->IsA<APlayerCharacter>()) return TEXT("ProjectProject01.SinglePlayer");
	if (const auto* RestoredId = RestoredActorSaveIds.Find(Actor)) return *RestoredId;
	if (Actor->GetClass()->ImplementsInterface(UProjectProject01CheckpointSaveable::StaticClass()))
	{
		const FName InterfaceId = IProjectProject01CheckpointSaveable::Execute_GetCheckpointSaveId(
			const_cast<AActor*>(Actor));
		if (!InterfaceId.IsNone()) return InterfaceId;
	}
	if (Actor->IsA<AHelperRearGuardCharacter>())
	{
		return TEXT("ProjectProject01.SinglePlayerHelper");
	}
	return FName(*FString::Printf(TEXT("%s::%s"),
		*Actor->GetClass()->GetPathName(), *Actor->GetFName().ToString()));
}

void UProjectProject01SaveSubsystem::CaptureSaveActorBaseline(UWorld* World)
{
	BaselineSaveActorIds.Reset();
	BaselineMapPackageName.Reset();
	if (!IsValid(World) || World->GetNetMode() != NM_Standalone) return;
	BaselineMapPackageName = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (ShouldCaptureActor(*It))
		{
			BaselineSaveActorIds.Add(BuildActorSaveId(*It));
		}
	}
}

void UProjectProject01SaveSubsystem::CaptureWorldState(
	UWorld* World,
	UProjectProject01SinglePlayerSaveGame* Save)
{
	if (!IsValid(World) || !IsValid(Save)) return;
	const FString MapPackageName = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	if (!BaselineMapPackageName.Equals(MapPackageName, ESearchCase::IgnoreCase) || BaselineSaveActorIds.IsEmpty())
	{
		CaptureSaveActorBaseline(World);
	}

	Save->ActorStates.Reset();
	TSet<FName> CurrentActorIds;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!ShouldCaptureActor(Actor)) continue;

		FProjectProject01SavedActorState& State = Save->ActorStates.AddDefaulted_GetRef();
		State.ActorId = BuildActorSaveId(Actor);
		if (State.ActorId.IsNone())
		{
			Save->ActorStates.Pop();
			continue;
		}
		CurrentActorIds.Add(State.ActorId);
		State.ActorClassPath = Actor->GetClass()->GetPathName();
		State.Transform = Actor->GetActorTransform();
		State.LinearVelocity = Actor->GetVelocity();
		State.bMapPlacedActor = Actor->HasAnyFlags(RF_WasLoaded);
		State.bHidden = Actor->IsHidden();
		State.bCollisionEnabled = Actor->GetActorEnableCollision();
		State.bTickEnabled = Actor->IsActorTickEnabled();
		if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
			IsValid(RootPrimitive) && RootPrimitive->IsSimulatingPhysics())
		{
			State.LinearVelocity = RootPrimitive->GetPhysicsLinearVelocity();
			State.AngularVelocityDegrees = RootPrimitive->GetPhysicsAngularVelocityInDegrees();
		}
		ProjectProject01CheckpointSave::SerializeSaveGameProperties(Actor, State.SaveGameData);
		if (auto* Pawn = Cast<APawn>(Actor))
			State.SinglePlayerAIData = FProjectCheckpointAI::Capture(Pawn, [this](AActor* Target){ return BuildActorSaveId(Target); });

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!IsValid(Component)) continue;
			FProjectProject01SavedComponentState& ComponentState = State.Components.AddDefaulted_GetRef();
			ComponentState.ComponentName = Component->GetFName();
			ComponentState.bActive = Component->IsActive();
			ComponentState.bTickEnabled = Component->IsComponentTickEnabled();
			if (const USceneComponent* Scene = Cast<USceneComponent>(Component); IsValid(Scene))
			{
				ComponentState.bIsSceneComponent = true;
				ComponentState.RelativeTransform = Scene->GetRelativeTransform();
				ComponentState.bVisible = Scene->IsVisible();
			}
			if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); IsValid(Primitive))
			{
				ComponentState.CollisionEnabled = Primitive->GetCollisionEnabled();
			}
			ProjectProject01CheckpointSave::SerializeSaveGameProperties(Component, ComponentState.SaveGameData);
		}
	}

	Save->ActorStates.Sort([](const FProjectProject01SavedActorState& Left,
		const FProjectProject01SavedActorState& Right)
	{
		return Left.ActorId.LexicalLess(Right.ActorId);
	});
	Save->DestroyedActorIds.Reset();
	for (const FName BaselineId : BaselineSaveActorIds)
	{
		if (!CurrentActorIds.Contains(BaselineId))
		{
			Save->DestroyedActorIds.Add(BaselineId);
		}
	}
	Save->DestroyedActorIds.Sort(FNameLexicalLess());
}

void UProjectProject01SaveSubsystem::RestoreWorldState(
	UWorld* World,
	const UProjectProject01SinglePlayerSaveGame* Save)
{
	if (!IsValid(World) || !IsValid(Save)) return;

	TMap<FName, AActor*> ActorsById;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (ShouldCaptureActor(*It))
		{
			ActorsById.Add(BuildActorSaveId(*It), *It);
		}
	}
	for (const FName DestroyedId : Save->DestroyedActorIds)
	{
		if (AActor** Existing = ActorsById.Find(DestroyedId); Existing != nullptr && IsValid(*Existing))
		{
			(*Existing)->Destroy();
			ActorsById.Remove(DestroyedId);
		}
	}

	for (const FProjectProject01SavedActorState& State : Save->ActorStates)
	{
		AActor* Actor = ActorsById.FindRef(State.ActorId);
		if (!IsValid(Actor) && !State.bMapPlacedActor && !State.ActorClassPath.IsEmpty())
		{
			const FSoftClassPath ClassPath(State.ActorClassPath);
			if (UClass* ActorClass = ClassPath.TryLoadClass<AActor>(); IsValid(ActorClass))
			{
				FActorSpawnParameters Parameters;
				Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Actor = World->SpawnActor<AActor>(ActorClass, State.Transform.GetLocation(),
					State.Transform.Rotator(), Parameters);
				if (IsValid(Actor))
				{
					Actor->SetActorScale3D(State.Transform.GetScale3D());
				}
			}
		}
		if (!IsValid(Actor))
		{
			UE_LOG(LogTemp, Warning, TEXT("Checkpoint actor could not be restored: %s"), *State.ActorId.ToString());
			continue;
		}
		RestoredActorSaveIds.Add(Actor, State.ActorId);

		if (APawn* Pawn = Cast<APawn>(Actor); IsValid(Pawn))
		{
			if (AController* Controller = Pawn->GetController(); IsValid(Controller))
			{
				Controller->StopMovement();
			}
		}
		Actor->SetActorTransform(State.Transform, false, nullptr, ETeleportType::TeleportPhysics);
		ProjectProject01CheckpointSave::DeserializeSaveGameProperties(Actor, State.SaveGameData);
		Actor->SetActorHiddenInGame(State.bHidden);
		Actor->SetActorEnableCollision(State.bCollisionEnabled);
		Actor->SetActorTickEnabled(State.bTickEnabled);

		TMap<FName, UActorComponent*> ComponentsByName;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (IsValid(Component)) ComponentsByName.Add(Component->GetFName(), Component);
		}
		for (const FProjectProject01SavedComponentState& ComponentState : State.Components)
		{
			UActorComponent* Component = ComponentsByName.FindRef(ComponentState.ComponentName);
			if (!IsValid(Component)) continue;
			ProjectProject01CheckpointSave::DeserializeSaveGameProperties(Component, ComponentState.SaveGameData);
			if (USceneComponent* Scene = Cast<USceneComponent>(Component); IsValid(Scene) && ComponentState.bIsSceneComponent)
			{
				Scene->SetRelativeTransform(ComponentState.RelativeTransform, false, nullptr,
					ETeleportType::TeleportPhysics);
				Scene->SetVisibility(ComponentState.bVisible, true);
			}
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); IsValid(Primitive))
			{
				Primitive->SetCollisionEnabled(ComponentState.CollisionEnabled);
			}
			if (ComponentState.bActive) Component->Activate(true);
			else Component->Deactivate();
			Component->SetComponentTickEnabled(ComponentState.bTickEnabled);
		}

		if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
			IsValid(RootPrimitive) && RootPrimitive->IsSimulatingPhysics())
		{
			RootPrimitive->SetPhysicsLinearVelocity(State.LinearVelocity);
			RootPrimitive->SetPhysicsAngularVelocityInDegrees(State.AngularVelocityDegrees);
		}
		else if (UMovementComponent* Movement = Actor->FindComponentByClass<UMovementComponent>(); IsValid(Movement))
		{
			Movement->Velocity = State.LinearVelocity;
		}

		if (AMannequinReactiveLight* ReactiveLight = Cast<AMannequinReactiveLight>(Actor); IsValid(ReactiveLight))
		{
			ReactiveLight->RefreshAfterCheckpointRestore();
		}
		if (Actor->GetClass()->ImplementsInterface(UProjectProject01CheckpointSaveable::StaticClass()))
		{
			IProjectProject01CheckpointSaveable::Execute_OnCheckpointStateRestored(Actor);
		}
	}
}

void UProjectProject01SaveSubsystem::RestoreAIState(UWorld* World, const UProjectProject01SinglePlayerSaveGame* Save)
{
	TMap<FName, AActor*> Actors;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (ShouldCaptureActor(*It) || It->IsA<APlayerCharacter>()) Actors.Add(BuildActorSaveId(*It), *It);
	// Resolve object references only after every map/dynamic actor and the player
	// have their saved transforms. Never restore pointers into the previous world.
	for (const auto& State : Save->ActorStates)
		if (auto* Pawn = Cast<APawn>(Actors.FindRef(State.ActorId)); Pawn && !State.SinglePlayerAIData.IsEmpty())
			if (!FProjectCheckpointAI::Restore(Pawn, State.SinglePlayerAIData, [&Actors](FName Id){return Actors.FindRef(Id);}))
				UE_LOG(LogTemp, Warning, TEXT("Checkpoint AI state rejected: %s"), *State.ActorId.ToString());
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
