#pragma once
#include "CoreMinimal.h"
class SWidget;
namespace ProjectAchievementToast
{
    TSharedRef<SWidget> Create();
    void Show(TSharedRef<SWidget> Layer,const FString& Code);
}
