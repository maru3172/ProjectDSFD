#pragma once
#include "CoreMinimal.h"

class SWidget;
class UGameInstance;

/** The phone's "오프라인 게임" folder (Docs/MiniGames.md): game list, the three games, their boards. */
namespace ProjectMiniGameUI
{
    /**
     * OnBack returns to the phone's messages; OnPlay runs when a game opens (the phone turns sideways). The folder
     * pauses and saves the playing game when it is destroyed.
     */
    PROJECTPROJECT01_API TSharedRef<SWidget> MakeFolder(TWeakObjectPtr<UGameInstance> Game,FSimpleDelegate OnBack,FSimpleDelegate OnPlay);
}
