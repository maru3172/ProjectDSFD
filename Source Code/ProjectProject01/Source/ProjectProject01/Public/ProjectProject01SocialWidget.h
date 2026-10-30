#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectProject01SocialWidget.generated.h"

/** An overlay: opening friends, notifications or whispers never replaces the lobby/room screen. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01SocialWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    void ConfigureBlockList() { bBlocksOnly=true; }
    void OpenFriends() { Open(TEXT("Friends")); }
    void OpenPartyInvite() { Open(TEXT("PartyInvite")); }
    bool ShowRoomMemberProfile(const FString& UserId);
    bool ShowRoomMemberMenu(const FString& UserId,const FVector2D& Position);
    bool ShowPartyMemberMenu(const FString& UserId,const FVector2D& Position);
    void CloseRoomMemberProfile();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
    friend class FProjectSocialWidgetLayoutTest;
    friend class FProjectPartyAdmissionMenuTest;
    /** Null while the widget has no owning GameInstance (teardown, design-time preview). */
    class UProjectProject01SocialSubsystem* SocialSubsystem() const;
    void RefreshView();
    void Open(const FString& PageName);
    void BuildPanel();
    void OpenThread(const FString& Account);
    void ShowProfile(const FString& Account);
    void ShowModal(const FString& Kind,const FString& Target=FString(),const FString& Description=FString());
    void CloseModal();
    void FriendMenu(const FString& Account,const struct FPointerEvent& Event);
    void RoomContextReady(const FString& UserId,bool Ok);
    bool IsRoomMember(const FString& UserId) const;
    static TArray<TPair<FString,FString>> RoomMenuActions(bool bSelf,bool bFriend,bool bBlocked);
    static TArray<TPair<FString,FString>> PartyMenuActions(bool bSelf,bool bLeader);
    /** Party kick and leader transfer ask first (yes/no). */
    void AskThen(const FString& Question,TFunction<void()> OnYes,const FString& SkipKey=FString());
    /** Social action; removing a friend, blocking and deleting a conversation ask first. */
    void ActConfirmed(const FString& Op,const FString& Account);
    UPROPERTY(Transient) TObjectPtr<class UProjectConfirmDialogWidget> ConfirmDialog;
    const FJsonObject* PartyNoticeSnapshot=nullptr; // Snapshot already checked for a party removal notice.
    FString PendingRoomMenu, ReportReason;
    FVector2D RoomMenuPosition=FVector2D::ZeroVector;
    bool bBlocksOnly=false;
    bool bRoomMemberProfile=false;
    FString Page;
    FString Filter, PartyInviteDraft, ThreadAccount, ModalKind, ModalTarget, DraftAccent=TEXT("Slate"), DraftTitle=TEXT("Rookie"), LastMessageId;
    TSet<FString> SeenInvites;
    TSet<FString> SeenFriendRequests;
    FString ModalRequestKey;
    TSharedPtr<class SVerticalBox> Panel;
    TSharedPtr<class SEditableTextBox> TargetInput;
    TSharedPtr<class SEditableTextBox> MessageInput;
    TSharedPtr<class STextBlock> Feedback;
    TSharedPtr<class STextBlock> MessageLog;
    TSharedPtr<class STextBlock> BellDot;
    TSharedPtr<class SVerticalBox> List;
    TSharedPtr<class SVerticalBox> Modal;
    TSharedPtr<class SScrollBox> MessageScroll;
};
