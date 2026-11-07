#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectCustomMatchRules.h"
#include "ProjectCustomRoomRulesWidget.generated.h"
template<typename OptionType> class SComboBox;

UCLASS()
class PROJECTPROJECT01_API UProjectCustomRoomRulesWidget final:public UUserWidget
{
    GENERATED_BODY()
    friend class FProjectCustomRulesRenderTest;
public:
    static UProjectCustomRoomRulesWidget* OpenFor(class APlayerController* Player,UUserWidget* Return);
protected:
    virtual TSharedRef<SWidget> RebuildWidget()override;
    virtual void NativeConstruct()override;
    virtual void NativeDestruct()override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta)override;
    virtual void ReleaseSlateResources(bool Children)override;
private:
    void Close();
    void Send(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body=nullptr,TFunction<void(const TSharedPtr<FJsonObject>&)> OnJson=nullptr);
    void ReadPresets(const TSharedPtr<FJsonObject>& Json);
    bool CanEdit()const;
    /** The draft as it will be sent: with the custom rules off, every playground switch is cleared. */
    FProjectCustomMatchRules Outgoing()const;
    bool CanPickAffix(const FString& Id)const;
    void ToggleAffix(const FString& Id);
    FString MixDetail()const;
    FProjectCustomMatchRules Draft;
    int32 Tab=0;          // 0 기본, 1 섞기, 2 자유 규칙, 3 환경, 4 놀이 방식, 5 프리셋·코드, 6 공유 게시판. Built once; switching never rebuilds.
    /** Shared preset board (Docs/CustomGamePlayground.md 13): one page of four posts, read into fixed rows. */
    struct FBoardItem{int64 Id=0;FString Code,ShareCode,Name,Summary,Author,Stats;int32 Likes=0,Dislikes=0,MyVote=0;bool bMine=false,bCleared=false,bReported=false,bHidden=false,bPublished=true;FProjectCustomMatchRules Rules;};
    TArray<FBoardItem> Board;
    FString BoardSort=TEXT("new");
    int32 BoardPage=0;
    bool bBoardMore=false,bBoardLoaded=false;
    TSharedPtr<class SEditableTextBox> BoardName,BoardSummary;
    void LoadBoard();
    void Vote(int64 Id,int32 Value); // 1 추천, -1 비추천, 0 취소.
    void SetPublished(int64 Id,bool bPublish); // 게시 / 내리기 of my post (it stays in 내 글 with its fixed share code).
    /** Share codes (Docs/CustomGamePlayground.md 14): a fresh revocable code per share; board posts keep a fixed one. */
    int32 ShareHours=168; // 24, 168 or 0 (no expiry).
    TArray<TSharedPtr<FString>> ShareNames;TArray<FString> ShareCodesList;TArray<bool> ShareBoard;FString SelectedShare;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ShareCombo;
    void LoadShares();
    void ReadShares(const TSharedPtr<FJsonObject>& Json);
    bool SelectedShareIsBoard()const;
    /** What a received code or board post holds, shown before it is loaded into the room or saved as my preset. */
    bool bPreviewOpen=false;
    FString PreviewTitle,PreviewInfo,PreviewName;
    FProjectCustomMatchRules PreviewRules;
    void OpenPreview(const FString& Title,const FString& Info,const FString& Name,const FProjectCustomMatchRules& Rules);
    void ReadShareAnswer(const TSharedPtr<FJsonObject>& Json);
    void SavePresetAs(const FString& Name,const FProjectCustomMatchRules& Rules);
    static FString DescribeRules(const FProjectCustomMatchRules& Rules);
    /** My board posts (also taken-down ones), so a saved preset shows whether it is on the board. */
    struct FMyPost{int64 Id=0;FString Name;bool bPublished=false,bHidden=false;FProjectCustomMatchRules Rules;};
    TArray<FMyPost> MyPosts;
    void LoadMine();
    void ReadMine(const TSharedPtr<FJsonObject>& Json);
    const FMyPost* FindMyPost(const FProjectCustomMatchRules& Rules)const;
    void TogglePosted(const FString& Name,const FProjectCustomMatchRules& Rules);
    TArray<TFunction<void()>> Queued; // Requests waiting for the one in flight.
    /** 내 프리셋 모아 보기: all saved presets at a glance; ticked ones are posted, taken down or deleted together. */
    static constexpr int32 ManageRows=8;
    bool bManageOpen=false;
    int32 ManagePage=0;
    TSet<FString> ManageSelected;
    TArray<FString> ManagePageNames()const;
    FString PresetBoardState(const FString& Name)const; // 게시 중 / 내림 / 안 올림 / 운영자가 숨김.
    void BulkBoard(bool bPublish);
    void BulkDelete();
    void ReadBoard(const TSharedPtr<FJsonObject>& Json);
    static FString MakeBoardStats(int32 Plays,int32 ClearRate,int32 SurvivorWins,int32 MannequinWins);
    FString HoveredAffix; // Shown in the fixed detail line under the affix grid (no tooltips over other UI).
    FString HoveredPreset; // Recommended preset whose one-line summary is shown (fixed line, no tooltip).
    FString CodeLine;      // Rule code and its plain result summary (fixed two-line area).
    TSharedPtr<class SEditableTextBox> CodeInput;
    void ReadCodeAnswer(const TSharedPtr<FJsonObject>& Json,bool bLoadRules);
    // Personal handicap: the room's players (not observers or bots) and whose numbers the grid edits.
    TArray<TSharedPtr<FString>> HandicapNames;
    TArray<FString> HandicapUserIds;
    FString HandicapUserId,HandicapMembersKey;
    static constexpr const TCHAR* TeamHandicapKey=TEXT("#team"); // The "팀 전체" entry (never a user id: # is not allowed).
    TSharedPtr<SComboBox<TSharedPtr<FString>>> HandicapCombo;
    void RefreshHandicapMembers();
    void SetHandicapValue(int32 FProjectPlaygroundHandicap::* Member,int32 Value);
    TArray<TSharedPtr<FString>> EscapeModeNames; // 기본, 함께 탈출, 마지막 한 명이 모두를 구한다, 먼저 나간 사람 승리.
    TSharedPtr<SComboBox<TSharedPtr<FString>>> EscapeCombo;
    FString RoomId,Status,SelectedName;
    bool bEditable=false;
    TArray<TSharedPtr<FString>> PresetNames;
    TMap<FString,FProjectCustomMatchRules> Presets;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> PresetCombo;
    TSharedPtr<class SEditableTextBox> NameInput;
    TSharedPtr<class IHttpRequest,ESPMode::ThreadSafe> Request;
    /** "적용하시겠습니까?" with 오늘 하루 다시 묻지 않기, before the rules are sent. */
    UPROPERTY(Transient) TObjectPtr<class UProjectConfirmDialogWidget> ApplyConfirm;
    void AskThen(const FString& Question,const FString& SkipKey,TFunction<void()> Run);
    TWeakObjectPtr<UUserWidget> ReturnWidget;
};
