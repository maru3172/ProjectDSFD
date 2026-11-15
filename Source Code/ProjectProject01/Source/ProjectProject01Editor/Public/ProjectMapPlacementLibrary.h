#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectMapPlacementLibrary.generated.h"

/** Editor helpers the map placement script (Tools/PlaceFireShutterLayout.py) calls where Python has no collision. */
UCLASS()
class UProjectMapPlacementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
	 * Light fixtures the script placed (tag ShutterLayoutAuto) that touch modelled geometry at their own height (a lintel over
	 * a store front, a soffit) move to the nearest clear spot within 5 m over walkable floor; ones with none are removed.
	 * Builds pending meshes first so collision exists. Returns "moved,removed".
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Placement", meta = (WorldContext = "WorldContext"))
	static FString MoveFixturesOutOfWalls(UObject* WorldContext, float Clearance = 80.f);

	/**
	 * Shutters the script placed (tag ShutterLayoutAuto) that run into modelled geometry: a panel whose width reaches a
	 * wall or column is narrowed to the clear part, one under a lintel or beam is lowered to just below it. Panels left
	 * narrower than 60 cm are removed. Returns "narrowed,lowered,removed".
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Placement", meta = (WorldContext = "WorldContext"))
	static FString FitShuttersToWalls(UObject* WorldContext, float CeilingHeight = 600.f);
};
