#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "ProjectDesertionNotice.generated.h"

/** Lobby: a match this player left while still in play (a desertion, shown to players as "기권"), shown once. */
UCLASS()
class PROJECTPROJECT01_API UProjectDesertionNoticeWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Asks the backend for the oldest unseen desertion; stays hidden when there is none. */
    void Check();
    /** Result text for a desertion in this role ("Survivor" or "Mannequin"). */
    static FString Describe(const FString& Role);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;
private:
    void Acknowledge();
    FHttpRequestPtr Request;
    FString MatchId,Role;
};
