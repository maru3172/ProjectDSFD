#include "ProjectLightVariation.h"
#include "MannequinReactiveLight.h"
#include "Components/LocalLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Misc/Crc.h"

namespace ProjectLightVariation
{
	float Factor(const FString& LightName)
	{
		const uint32 Hash = FCrc::StrCrc32(*LightName) * 2654435761u; // Spread names that differ only by a number.
		return Lowest + (1.f - Lowest) * float((Hash >> 8) % 1001u) / 1000.f;
	}
	bool AppliesToMap(const FString& MapName)
	{
		for (const TCHAR* Menu : {TEXT("TitleLevel"), TEXT("LoginLevel"), TEXT("LobbyLevel")})
			if (MapName.EndsWith(Menu)) return false;
		return true;
	}
}

bool UProjectLightVariationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}
bool UProjectLightVariationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
void UProjectLightVariationSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!ProjectLightVariation::AppliesToMap(UWorld::RemovePIEPrefix(World.GetMapName()))) return;
	// Runs before the actors' BeginPlay, so code that remembers a light's brightness (the intro finale) sees the varied one.
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->IsA<APawn>() || Actor->IsA<AMannequinReactiveLight>()) continue; // Reactive lights vary their own base brightness.
		if (!Actor->IsNetStartupActor() && !Actor->HasAnyFlags(RF_WasLoaded)) continue; // Only lights placed in the map.
		TInlineComponentArray<ULocalLightComponent*> Lights(Actor);
		if (Lights.IsEmpty()) continue;
		const float Factor = ProjectLightVariation::Factor(Actor->GetName());
		for (ULocalLightComponent* Light : Lights) Light->SetIntensity(Light->Intensity * Factor);
	}
}
