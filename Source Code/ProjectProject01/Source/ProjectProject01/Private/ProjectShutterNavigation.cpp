#include "ProjectShutterNavigation.h"

UNavArea_ProjectShutterDoor::UNavArea_ProjectShutterDoor()
{
	DefaultCost = 3.f; // Opening takes a moment: bots prefer an open way when it is about as short.
	DrawColor = FColor(80, 190, 110);
}

UProjectMannequinNavFilter::UProjectMannequinNavFilter()
{
	FNavigationFilterArea Door;
	Door.AreaClass = UNavArea_ProjectShutterDoor::StaticClass();
	Door.bIsExcluded = true;
	Areas.Add(Door);
}
