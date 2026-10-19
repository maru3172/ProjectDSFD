#include "ProjectProject01CheckpointStore.h"
#include "ProjectProject01SinglePlayerSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

ProjectCheckpointStore::FSelection ProjectCheckpointStore::SelectNewest(const FString& Base, const FLoader& Load)
{
	FSelection Best;
	for (const auto& Slot : {Base, Base + TEXT("_Backup"), Base + TEXT("_GenerationA"), Base + TEXT("_GenerationB")})
	{
		auto* Candidate = Load(Slot);
		if (!Candidate) continue;
		if (!Best.Save || Candidate->SaveRevision > Best.Save->SaveRevision ||
			(Candidate->SaveRevision == Best.Save->SaveRevision && Candidate->SavedAtUtc > Best.Save->SavedAtUtc))
			Best = {Candidate, Slot};
	}
	return Best;
}

FString ProjectCheckpointStore::NextWriteSlot(const FString& Base, const FSelection& Current)
{
	return Base + (Current.Slot == Base + TEXT("_GenerationA") ? TEXT("_GenerationB") : TEXT("_GenerationA"));
}

TArray<FString> ProjectCheckpointStore::AllSlots(const FString& Base)
{
	return {Base, Base + TEXT("_Backup"), Base + TEXT("_Pending"), Base + TEXT("_GenerationA"),
		Base + TEXT("_GenerationB"), Base + TEXT("_Recent")};
}

bool ProjectCheckpointStore::Encode(USaveGame* Save, TArray<uint8>& OutData)
{
	if (!Save) return false;
	auto* Snapshot = Cast<UProjectProject01SinglePlayerSaveGame>(Save);
	TArray<uint8> Preview;
	if (Snapshot) Swap(Preview, Snapshot->PreviewPng);
	TArray<uint8> WorldBytes;
	const bool bSerialized = UGameplayStatics::SaveGameToMemory(Save, WorldBytes);
	if (Snapshot) Swap(Preview, Snapshot->PreviewPng);
	if (!bSerialized || WorldBytes.IsEmpty() || WorldBytes.Num() > 256 * 1024 * 1024) return false;
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = 0x50433150, Version = 1, Crc = FCrc::MemCrc32(WorldBytes.GetData(), WorldBytes.Num());
	int32 WorldSize = WorldBytes.Num(), PreviewSize = Snapshot ? Snapshot->PreviewPng.Num() : 0;
	Writer << Magic << Version << WorldSize << Crc << PreviewSize;
	Writer.Serialize(WorldBytes.GetData(), WorldSize);
	if (PreviewSize > 0) Writer.Serialize(Snapshot->PreviewPng.GetData(), PreviewSize);
	return !Writer.IsError();
}

USaveGame* ProjectCheckpointStore::Decode(const TArray<uint8>& Data)
{
	// Do not pass an interrupted SG buffer to UObject deserialization: corrupt
	// array/string lengths can fail before the object's own integrity check runs.
	if (Data.Num() < 20) return nullptr;
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0, Version = 0, Crc = 0;
	int32 WorldSize = 0, PreviewSize = 0;
	Reader << Magic << Version << WorldSize << Crc << PreviewSize;
	if (Magic != 0x50433150 || Version != 1 || WorldSize <= 0 || WorldSize > 256 * 1024 * 1024 ||
		static_cast<int64>(WorldSize) + 20 > Data.Num()) return nullptr;
	if (FCrc::MemCrc32(Data.GetData() + 20, WorldSize) != Crc) return nullptr;
	TArray<uint8> WorldBytes;
	WorldBytes.Append(Data.GetData() + 20, WorldSize);
	USaveGame* Save = UGameplayStatics::LoadGameFromMemory(WorldBytes);
	if (auto* Snapshot = Cast<UProjectProject01SinglePlayerSaveGame>(Save); Snapshot && PreviewSize > 0 && PreviewSize <= 512 * 1024 &&
		static_cast<int64>(20) + WorldSize + PreviewSize <= Data.Num())
	{
		// Optional image has its own CRC/PNG checks; a damaged/truncated preview
		// never invalidates an otherwise complete world snapshot.
		Snapshot->PreviewPng.Append(Data.GetData() + 20 + WorldSize, PreviewSize);
	}
	return Save;
}
