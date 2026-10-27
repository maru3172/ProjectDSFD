#include "ProjectNavigationCrowdManager.h"
#include "Engine/World.h"
#include "NavigationData.h"

void UProjectNavigationCrowdManager::OnNavDataUnregistered(ANavigationData& NavData)
{
    const UWorld* World = GetWorld();
    if (World && (World->bIsTearingDown || World->IsBeingCleanedUp()))
    {
        if (GetNavData() == &NavData)
        {
            // NavigationSystem is removing its data. Release agents and the old
            // mesh binding, without trying to create a new crowd for a dying world.
#if WITH_RECAST
            DestroyCrowdManager();
#endif
            SetNavData(nullptr, false);
        }
        return;
    }
    // Live nav-data replacement/rebuild still uses the engine's normal recovery.
    Super::OnNavDataUnregistered(NavData);
}
