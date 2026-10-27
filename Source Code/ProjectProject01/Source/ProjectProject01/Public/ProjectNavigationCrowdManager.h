#pragma once

#include "Navigation/CrowdManager.h"
#include "ProjectNavigationCrowdManager.generated.h"

/** Retains Detour behaviour while stopping nav-data reacquisition during world teardown. */
UCLASS(Config=Engine)
class PROJECTPROJECT01_API UProjectNavigationCrowdManager final : public UCrowdManager
{
    GENERATED_BODY()
public:
    virtual void OnNavDataUnregistered(ANavigationData& NavData) override;
};
