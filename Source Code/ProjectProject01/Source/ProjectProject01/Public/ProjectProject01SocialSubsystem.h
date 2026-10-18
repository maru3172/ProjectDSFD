#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "ProjectProject01SocialSubsystem.generated.h"
DECLARE_MULTICAST_DELEGATE_OneParam(FProjectSocialWhisperSent,const FString&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FProjectSocialActionCompleted,const FString&,bool);

UCLASS()
class PROJECTPROJECT01_API UProjectProject01SocialSubsystem final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void Clear();
    void Refresh();
    void Act(const FString& Operation, const FString& Target=FString(), const FString& Text=FString());
    void SetLobbyVisible(bool bVisible);
    void SetPresence(const FString& InPresence);
    void SearchPlayers(const FString& Query);
    void LoadProfile(const FString& Account);
    void LoadRoomProfile(const FString& UserId);
    void LoadRoomContext(const FString& UserId);
    void SubmitRoomReport(const FString& UserId,const FString& Reason,const FString& Details);
    void SubmitAccountReport(const FString& Account,const FString& Reason,const FString& Details);
    void SaveProfile(const FString& Message,const FString& Bio,const FString& Accent,const FString& Title);
    void OpenConversation(const FString& Account);
    void MarkConversationRead(const FString& Id) { PendingReadId=Id; }
    bool IsAuthenticated() const;
    bool IsLobbyVisible() const { return bLobbyVisible; }
    const FString& GetPresence() const { return Presence; }
    TSharedPtr<class FJsonObject> Snapshot;
    TSharedPtr<class FJsonObject> SearchResults, Profile, Conversation;
    TSharedPtr<class FJsonObject> RoomContext;
    FString RoomContextUserId;
    FProjectSocialActionCompleted OnRoomContextReady;
    FString ConversationAccount;
    FString Status;
    FSimpleMulticastDelegate OnChanged;
    FProjectSocialWhisperSent OnWhisperSent;
    FProjectSocialActionCompleted OnActionCompleted;
private:
    friend class FProjectSocialWidgetLayoutTest;
    bool Tick(float);
    void Send(bool bRead, const FString& Body, bool bPresence=false,const FString& Path=FString(),const FString& Verb=FString());
    void Detail(int32 Kind,const FString& Path);
    FTSTicker::FDelegateHandle Ticker;
    TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ReadRequest;
    TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> WriteRequest;
    TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> PresenceRequest;
    TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> SearchRequest, ProfileRequest, ConversationRequest;
    TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> RoomContextRequest;
    FString SessionIdentity;
    FString Presence=TEXT("Online");
    bool bLobbyVisible=false;
    float UntilPoll=0;
    bool bRoomRefreshPending=false;
    FString PendingReadId;
};
