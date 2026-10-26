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
    friend class FProjectPartyAdmissionStateTest;
    friend class FProjectPartyAdmissionWidgetTest;
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer)const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void Refresh();
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
    bool IsBusy()const{return Request.IsValid();}
    bool CanImport()const;
    const TSet<FString>& GetLocalAchievements()const;
    TSharedPtr<class FJsonObject> Snapshot;
    FString Status;
    FSimpleMulticastDelegate OnChanged;
private:
    void ClearSession();
    bool Tick(float Delta);
    void Send(const FString& Path,const FString& Body=FString());
    bool PrepareWrite();
    void PersistLocal();
    UPROPERTY() TObjectPtr<UProjectCampaignAchievementSave> Local;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> Request;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> ArchiveRequest;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> AchievementRateRequest;
    TSharedPtr<class FJsonObject> AchievementRates;
    FString AchievementRateBase;
    double LastAchievementRateFetchAt=-100;
    TSharedPtr<class FJsonObject> StoryArchives;
    double LastArchiveFetchAt=-100;
    FTSTicker::FDelegateHandle Ticker;
    float PollIn=0;
    FString CurrentLocalSlot;
    bool bPersistence=true,bSaving=false,bSaveDirty=false;
    bool bImportAttempted=false;
    FString AchievementAccount;
    TSet<FString> SeenOnlineAchievements;
    void ObserveAchievements();
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
    void BuildStoryBook(const TSharedPtr<class FJsonObject>& Investigation,bool Archived);
    void Close();
    FString Page=TEXT("Achievements");
    FString RenderedFingerprint;
    int32 InvestigationStage=1,InvestigationPiece=1,ArchiveIndex=0;
    bool bOpenedOnline=false;
    bool bShopFundsMessageHandled=false;
    TSharedPtr<class SProjectShopFundsNotice> ShopFundsNotice;
    TSharedPtr<class SVerticalBox> Body;
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
