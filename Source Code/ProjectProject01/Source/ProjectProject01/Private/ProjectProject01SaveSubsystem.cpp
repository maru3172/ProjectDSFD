#include "ProjectProject01SaveSubsystem.h"

#include "PlayerCharacter.h"
#include "HelperRearGuardCharacter.h"
#include "MannequinAICharacter.h"
#include "MannequinReactiveLight.h"
#include "ProjectProject01Checkpoint.h"
#include "ProjectProject01CheckpointSaveable.h"
#include "ProjectProject01SinglePlayerSaveGame.h"
#include "ProjectProject01TuningData.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Crc.h"
#include "JsonObjectConverter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "TimerManager.h"

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
	if (bResetDeathCount)
	{
		Player->ResetDeathCountAtCheckpoint();
	}
	const FString PrimarySlot = GetPrimarySlotName(SlotIndex);
	const FString BackupSlot = GetBackupSlotName(SlotIndex);

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
	Save->IntegrityHash = Save->CalculateIntegrityHash();

	PreviousSlotIndexBeforeSave = CurrentSlotIndex;
	CurrentSlotIndex = SlotIndex;
	PendingSaveSuccessMessage = CheckpointId == TEXT("ManualPauseMenu")
		? TEXT("체크포인트가 수동 저장되었습니다.")
		: TEXT("체크포인트가 자동 저장되었습니다.");
	bSaveInProgress = true;
	FAsyncSaveGameToSlotDelegate Delegate;
	Delegate.BindUObject(this, &UProjectProject01SaveSubsystem::HandleAsyncSaveComplete);
	UGameplayStatics::AsyncSaveGameToSlot(Save, PrimarySlot, UserIndex, Delegate);
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

	const FString PrimarySlot = GetPrimarySlotName(SlotIndex);
	const FString BackupSlot = GetBackupSlotName(SlotIndex);
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
	CurrentSlotIndex = SlotIndex;
	PendingLoad->LastPlayedAtUtc = FDateTime::UtcNow();
	PendingLoad->IntegrityHash = PendingLoad->CalculateIntegrityHash();
	if (!UGameplayStatics::SaveGameToSlot(PendingLoad, PrimarySlot, UserIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not persist last-played metadata for single-player slot %d."), SlotIndex);
	}
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		PendingLoad = nullptr;
		OnSaveOperationCompleted.Broadcast(false, TEXT("현재 월드를 찾지 못했습니다."));
		return false;
	}

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
	if (!IsValidSlotIndex(SlotIndex)) return false;
	FString Ignored;
	return IsValid(LoadValidatedSlot(GetPrimarySlotName(SlotIndex), Ignored)) ||
		IsValid(LoadValidatedSlot(GetBackupSlotName(SlotIndex), Ignored));
}

FProjectProject01SaveSlotInfo UProjectProject01SaveSubsystem::GetSaveSlotInfo(const int32 SlotIndex) const
{
	FProjectProject01SaveSlotInfo Info;
	Info.SlotIndex = SlotIndex;
	if (!IsValidSlotIndex(SlotIndex)) return Info;
	FString Ignored;
	UProjectProject01SinglePlayerSaveGame* Save = LoadValidatedSlot(GetPrimarySlotName(SlotIndex), Ignored);
	if (!IsValid(Save))
	{
		Save = LoadValidatedSlot(GetBackupSlotName(SlotIndex), Ignored);
	}
	if (!IsValid(Save)) return Info;
	Info.bHasSave = true;
	Info.CheckpointId = Save->CheckpointId;
	Info.SavedMapPackageName = Save->SavedMapPackageName;
	Info.RemainingDeathCount = Save->RemainingDeathCount;
	Info.SavedAtUtc = Save->SavedAtUtc;
	Info.LastPlayedAtUtc = Save->LastPlayedAtUtc;
	return Info;
}

bool UProjectProject01SaveSubsystem::DeleteSaveSlot(const int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex) || bSaveInProgress) return false;
	const FString PrimarySlot = GetPrimarySlotName(SlotIndex);
	const FString BackupSlot = GetBackupSlotName(SlotIndex);
	const bool bPrimaryDeleted = !UGameplayStatics::DoesSaveGameExist(PrimarySlot, UserIndex) ||
		UGameplayStatics::DeleteGameInSlot(PrimarySlot, UserIndex);
	const bool bBackupDeleted = !UGameplayStatics::DoesSaveGameExist(BackupSlot, UserIndex) ||
		UGameplayStatics::DeleteGameInSlot(BackupSlot, UserIndex);
	if (SlotIndex == CurrentSlotIndex && bPrimaryDeleted && bBackupDeleted)
	{
		PendingLoad = nullptr;
		RuntimeProgressState.Reset();
	}
	return bPrimaryDeleted && bBackupDeleted;
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

FString UProjectProject01SaveSubsystem::GetPrimarySlotName(const int32 SlotIndex)
{
	// 1번은 기존 단일 저장 파일과 호환되도록 이름을 유지한다.
	return SlotIndex == 1 ? TEXT("SinglePlayerCheckpoint") :
		FString::Printf(TEXT("SinglePlayerCheckpoint_%d"), SlotIndex);
}

FString UProjectProject01SaveSubsystem::GetBackupSlotName(const int32 SlotIndex)
{
	return GetPrimarySlotName(SlotIndex) + TEXT("_Backup");
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
	if (!bSucceeded)
	{
		CurrentSlotIndex = PreviousSlotIndexBeforeSave;
	}
	const FString CompletionMessage = bSucceeded && !PendingSaveSuccessMessage.IsEmpty()
		? PendingSaveSuccessMessage
		: TEXT("체크포인트 저장에 실패했습니다.");
	PendingSaveSuccessMessage.Reset();
	OnSaveOperationCompleted.Broadcast(bSucceeded, CompletionMessage);
}

void UProjectProject01SaveSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!IsValid(LoadedWorld) || LoadedWorld->GetNetMode() != NM_Standalone)
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
		Self->CaptureSaveActorBaseline(World);
		if (!IsValid(Self->PendingLoad)) return;
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
	// 체크포인트의 bTriggered 등 월드 상태를 먼저 되살린 뒤 플레이어를 영역 안으로 이동한다.
	// 반대 순서면 복원 이동 자체가 BeginOverlap을 발생시켜 저장/초기화가 한 번 더 실행될 수 있다.
	RestoreWorldState(World, PendingLoad);
	Player->RestoreSinglePlayerState(PendingLoad->PlayerTransform,
		PendingLoad->RemainingDeathCount, PendingLoad->CurrentStamina,
		PendingLoad->PlayerVelocity, PendingLoad->PlayerControlRotation);
	RuntimeProgressState = PendingLoad->ProgressState;
	PendingLoad = nullptr;
	OutError.Reset();
	return true;
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

FName UProjectProject01SaveSubsystem::BuildActorSaveId(const AActor* Actor)
{
	if (!IsValid(Actor)) return NAME_None;
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
