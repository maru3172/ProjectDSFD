#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "ProjectCaseRecords.generated.h"

UCLASS()
class PROJECTPROJECT01_API UProjectCampaignAchievementSave final : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 Version=1;
    UPROPERTY(SaveGame) FString SaveId;
    UPROPERTY(SaveGame) TSet<FString> Earned;
    UPROPERTY(SaveGame) FString BoundAccountId;
    UPROPERTY(SaveGame) int64 Generation=0;
    UPROPERTY(SaveGame) FString PublicStoryArchivesJson;
};

/** Private wallet/mission state and independent offline campaign achievements. Never changes bot AI. */
UCLASS()
class PROJECTPROJECT01_API UProjectCaseRecords final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
    friend class FProjectCaseRecordsTest;
    friend class FProjectCaseUiRenderTest;
    friend class FProjectFrontendBehaviorTest;
    friend class FProjectPartyAdmissionStateTest;
    friend class FProjectPartyAdmissionWidgetTest;
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer)const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void Refresh();
    void DrawAction(const FString& Operation,int32 Count);
    void SaveFeatured(const TArray<FString>& Codes,const FString& Bio);
    TSharedPtr<class FJsonObject> DrawResult;
    void RefreshStoryArchives();
    void RefreshAchievementRates();
    TSharedPtr<class FJsonObject> GetAchievementRates()const;
    TSharedPtr<class FJsonObject> GetStoryArchives()const{return StoryArchives;}
    void Action(const FString& Action,const FString& Id,const FString& Mode=FString(),int32 ExpectedRerolls=-1);
    void PartyAction(const FString& Action,const FString& Target=FString(),const FString& Mode=FString(),const FString& Role=FString());
    void TrackPartyAdmission(const TSharedPtr<class FJsonObject>& View,TFunction<bool(const FString&)> Resume,TFunction<void()> Cancel);
    void AnswerPartyAdmission(const FString& Action,const FString& Id);
    bool HasPendingPartyAdmission()const;
    bool IsPartyAdmissionBusy()const{return AdmissionRequest.IsValid();}
    int32 PartyAdmissionSecondsLeft()const;
    void DismissPartyAdmission(const FString& Id);
    TSharedPtr<class FJsonObject> PartyAdmission;
    void ImportCampaign();
    UFUNCTION(BlueprintCallable,Category="Case Records|Campaign") bool RecordCampaignAchievement(FName Code);
    UFUNCTION(BlueprintPure,Category="Case Records|Campaign") bool HasCampaignAchievement(FName Code) const;
    bool IsAuthenticated()const;
    bool IsBusy()const{return Request.IsValid();} bool IsWriting()const;
    /** A write that has lasted a noticeable time. UI greys out only then, so short saves never flash the controls (clicks are still refused while any write runs). */
    bool IsWritingLong(double Seconds=.35)const{return IsWriting()&&FPlatformTime::Seconds()-WriteStartedAt>=Seconds;}
    bool CanImport()const;
    const TSet<FString>& GetLocalAchievements()const;
    TSharedPtr<class FJsonObject> Snapshot;
    FString Status;
    FSimpleMulticastDelegate OnChanged;
    FSimpleMulticastDelegate OnReceipt;
    TSharedPtr<class FJsonObject> LastReceipt;
    FString ConfirmationScope()const;
    bool ShouldConfirm(const FString& Category)const;
    void SkipConfirmationToday(const FString& Category);
    void MainQuestAction(const FString& Action,const FString& Role=FString());
    void ApplyMainQuestUpdate(const TSharedPtr<class FJsonObject>& Update);
    /** Reports a finished phone conversation (choices per choice line); retried until the server answers. */
    void MainQuestTalk(const FString& StepId,const TArray<int32>& Choices);
    /** An unlocked main quest conversation has not been read yet (red dot on the phone button). */
    bool HasUnreadTalk()const;
private:
    void ClearSession();
    bool Tick(float Delta);
    void Send(const FString& Path,const FString& Body=FString());
    bool PrepareWrite();
    void ApplyDrawResponse(const TSharedPtr<class FJsonObject>& Response);
    void PersistLocal();
    UPROPERTY() TObjectPtr<UProjectCampaignAchievementSave> Local;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> Request;
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void(const FString&,const FString&)> ReviewRequestSink;
#endif
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> ArchiveRequest;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> AchievementRateRequest;
    TSharedPtr<class FJsonObject> AchievementRates;
    FString AchievementRateBase;
    double LastAchievementRateFetchAt=-100;
    TSharedPtr<class FJsonObject> StoryArchives;
    double LastArchiveFetchAt=-100;
    FTSTicker::FDelegateHandle Ticker;
    float PollIn=0;
    double WriteStartedAt=0;
    FString CurrentLocalSlot;
    FString PendingDrawId,PendingDrawOperation;
    int32 PendingDrawCount=0;
    double PendingDrawRevision=0;
    bool bPersistence=true,bSaving=false,bSaveDirty=false;
    TMap<FString,FString> ConfirmationDays;
    bool bImportAttempted=false;
    FString AchievementAccount;
    TSet<FString> SeenOnlineAchievements;
    void ObserveAchievements();
    /** Shows "일일/주간 미션 완료" once when a mission reaches its target (not for missions already done at login). */
    void ObserveMissions();
    FString MissionAccount;
    TSet<FString> CompletedMissions;
    void ObserveMainQuest(const TSharedPtr<class FJsonObject>& Response=nullptr);
    FString QuestAccount;
    int64 SeenQuestRevision=0;
    /** Chain quest lines (캘린, 아테아) open when that NPC's main quest talk is done; a notice shows once when that happens. */
    void ObserveChainUnlocks();
    FString ChainAccount;
    TSet<FString> OpenChains;
    FString PendingTutorialRole;
    uint64 PendingTutorialGeneration=0;
    double TutorialRetryAt=0;
    FString PendingTalkStep;
    TArray<int32> PendingTalkChoices;
    uint64 PendingTalkGeneration=0;
    double TalkRetryAt=0;
    bool bTalkInFlight=false;
    void TickPartyAdmission(float Delta);
    void ReadPartyAdmission(const TSharedPtr<class FJsonObject>& Json);
    void SendPartyAdmission(const FString& Body=FString());
    void ClearPartyAdmission();
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> AdmissionRequest;
    TFunction<bool(const FString&)> AdmissionResume;
    TFunction<void()> AdmissionCancel;
    FString TrackedAdmissionId,DismissedAdmissionId,CompletedAdmissionId;
    double AdmissionDeadline=0;
    float AdmissionPollIn=0;
    UPROPERTY(Transient) TObjectPtr<class UProjectPartyAdmissionWidget> AdmissionWidget;
};

UCLASS()
class PROJECTPROJECT01_API UProjectCaseRecordWidget final : public UUserWidget
{
    GENERATED_BODY()
    friend class FProjectCaseUiRenderTest;
public:
    static UProjectCaseRecordWidget* OpenFor(class APlayerController* Player,const FString& Page=TEXT("Achievements"),UUserWidget* Return=nullptr);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void ReleaseSlateResources(bool Children) override;
private:
    void RefreshPanel();
    void BuildStoryBook(const TSharedPtr<class FJsonObject>& Investigation,bool Archived,TSharedPtr<class SVerticalBox> Into=nullptr);
    // The battle pass board and the archive season book redraw on their own when paged (see BuildStoryBook).
    TSharedPtr<class SBox> StoryBookHost;TFunction<void()> RebuildStoryBook,RebuildArchiveSeason;FString StoryBookCaption;
    void BuildCollection(const TSharedPtr<class FJsonObject>& State);
    void BuildDraw(const TSharedPtr<class FJsonObject>& State);
    void BuildMainQuest(const TSharedPtr<class FJsonObject>& State);
    void BuildLore(const TSharedPtr<class FJsonObject>& State);
    void RequestDrawAction(const FString& Operation,int32 Count);
    void ExecuteDrawAction(const FString& Operation,int32 Count);
    void RequestPurchase(const FString& Id);
    void RequestCosmeticAction(const FString& Action,const FString& Id);
    void RequestMissionReroll(const FString& Id,const FString& Mode,int32 Count,const FString& Period);
    void ConfirmAction(const FString& Category,const FString& Title,TFunction<void()> Action);
    void ResolveConfirmation(bool Accept,bool SkipToday);
    void ShowReceipt();
    void OpenDrawParcel(int32 Index);
    void OpenAllDrawParcels();
    void FinishRarePresentation();
    void Close();
    FString Page=TEXT("Achievements");
    FString RenderedFingerprint;
    int32 InvestigationStage=1,InvestigationPiece=1,ArchiveIndex=0;
    bool bOpenedOnline=false;
    bool bShopFundsMessageHandled=false;
    bool bShowDrawRates=false,bEditingProfile=false,bDrawResultOpen=false;
    bool bDrawRequestedHere=false;
    FString CollectionCategory=TEXT("All"),DrawFeedback;
    double DrawFeedbackAt=-100;
    int32 CollectionPage=0;
    // Paging the collection redraws only its cards: the buttons stay, so they keep their hover and press look.
    TSharedPtr<class SBox> CollectionGridHost;TFunction<void()> RebuildCollectionGrid;int32 CollectionPages=1;
    int32 DrawExchangeCount=1;
    int32 SelectedQuestStage=-1;
    FString FocusAchievement; // Set by a profile link; that achievement is highlighted and scrolled to.
    FString QuestCategory=TEXT("Main"); // Main, Chain or Sub.
    FString LoreId,LoreCategory; // 기본 기록: the open entry (empty = the list) and the category filter (empty = 전체).
    int32 SelectedChainQuest=0;
    int32 SelectedQuestChapter=0; // Which main quest line (탐사, ...) is open.
    TMap<int32,int32> DrawShowcase;
    FString LastDrawReceipt;
    TMap<int32,double> ParcelOpenTimes;
    TArray<int32> RareRevealQueue;
    double RareRevealBeganAt=0;
    TSharedPtr<class SOverlay> RarePresentationLayer;
    TSharedPtr<class SOverlay> TransactionLayer;
    TFunction<void()> ConfirmedAction;
    FString ConfirmationCategory,ConfirmationAccount;
    uint64 ConfirmationGeneration=0;
    TArray<FString> FeaturedDraft;
    FString BioDraft;
    TSharedPtr<class SProjectShopFundsNotice> ShopFundsNotice;
    TSharedPtr<class SVerticalBox> Body;
    // Page changes build the new body hidden behind the shown one for two frames (text wrap and heights settle), then swap,
    // so the page never jumps or scrolls while it is rebuilt.
    TSharedPtr<class SOverlay> BodyHost;TSharedPtr<class SVerticalBox> RetiringBody;int32 BodySwapFrames=0;bool bBodySwapTimer=false;
    void BeginBodyRebuild();
    TSharedPtr<class SBorder> RequestShield;
    TSharedPtr<class SScrollBox> RecordsScroll;
    TWeakObjectPtr<UUserWidget> ReturnWidget;
};

/** Bottom-left account/party chrome, independent from the bottom-right friends/whisper UI. */
UCLASS()
class PROJECTPROJECT01_API UProjectCaseRecordDock final : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetSocialOverlay(class UProjectProject01SocialWidget* Social);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<class UProjectProject01SocialWidget> SocialOverlay;
};

UCLASS()
class PROJECTPROJECT01_API UProjectCaseOpenButton final : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UUserWidget> ReturnWidget;
    UFUNCTION() void OpenRecords();
};

UCLASS()
class PROJECTPROJECT01_API UProjectCaseBanner final : public UWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) FString ItemId;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
