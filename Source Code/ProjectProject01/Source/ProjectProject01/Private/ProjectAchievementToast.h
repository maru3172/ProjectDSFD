#pragma once
#include "CoreMinimal.h"
class SWidget;
namespace ProjectAchievementToast
{
    TSharedRef<SWidget> Create();
    void Show(TSharedRef<SWidget> Layer,const FString& Code);
    void ShowQuest(TSharedRef<SWidget> Layer,const FString& Name,const FString& Kind,const FString& Detail,const FString& EventId);
}
