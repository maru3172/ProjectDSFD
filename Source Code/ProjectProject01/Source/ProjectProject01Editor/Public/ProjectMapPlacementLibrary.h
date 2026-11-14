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
};
