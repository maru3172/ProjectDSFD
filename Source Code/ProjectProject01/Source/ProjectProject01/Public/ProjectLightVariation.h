#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectLightVariation.generated.h"

/**
 * Placed lights in the game maps are not all equally bright: each one is dimmed by its own amount (72-100 %).
 * The amount comes from the light's name, so every player in a match sees the same lights.
 * Menus (title, login, lobby), characters' lights, the sun and the sky are left alone.
 */
namespace ProjectLightVariation
{
	inline constexpr float Lowest = .72f;
	/** The brightness multiplier for this placed light (same name, same result). */
	PROJECTPROJECT01_API float Factor(const FString& LightName);
	/** Whether the lights of this map are varied (not the menu maps). */
	PROJECTPROJECT01_API bool AppliesToMap(const FString& MapName);
}

UCLASS()
class PROJECTPROJECT01_API UProjectLightVariationSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
