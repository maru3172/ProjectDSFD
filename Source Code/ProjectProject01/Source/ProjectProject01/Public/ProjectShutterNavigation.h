#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "ProjectShutterNavigation.generated.h"

/**
 * Navmesh area under a working fire shutter that is down (Docs/FireShutterLayout.md).
 * Survivor bots walk through it (they open the shutter on arrival, a little extra cost); mannequins never do,
 * so a lowered shutter sends them around another way.
 */
UCLASS()
class PROJECTPROJECT01_API UNavArea_ProjectShutterDoor : public UNavArea
{
	GENERATED_BODY()
public:
	UNavArea_ProjectShutterDoor();
};

/** Mannequin pathfinding: everything as before, except a lowered working shutter is a wall. */
UCLASS()
class PROJECTPROJECT01_API UProjectMannequinNavFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()
public:
	UProjectMannequinNavFilter();
};
