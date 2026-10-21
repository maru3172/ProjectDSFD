#pragma once

#include "CoreMinimal.h"
#include "PlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01Tutorial.generated.h"

/** The practice bot has no remote player sending character movement packets. */
UCLASS()
class PROJECTPROJECT01_API UProjectTutorialTargetMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
protected:
	virtual void ServerAutonomousProxyTick(float DeltaTime) override;
private:
	FVector PracticeInput=FVector::ZeroVector;
};

UCLASS(NotBlueprintable)
class PROJECTPROJECT01_API AProjectTutorialTargetCharacter : public APlayerCharacter
{
	GENERATED_BODY()
public:
	AProjectTutorialTargetCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	void SetPracticeSprint(bool bSprint) { SetSprinting(bSprint); }
	bool IsPracticeSprinting() const { return bIsSprinting; }
};

USTRUCT()
struct FProjectTutorialSurvivorBot
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<AProjectTutorialTargetCharacter> Pawn;
	UPROPERTY(Transient) TObjectPtr<AMultiplayTestPlayerController> Controller;
	TArray<FVector> Path;
	int32 PathIndex=0;
	float RepathSeconds=0,LookSeconds=0,StuckSeconds=0;
	FVector LastLocation=FVector::ZeroVector,AvoidDirection=FVector::ZeroVector;
	bool bRecoveringStamina=false,bCovering=false,bRequestedSprintLastTick=false;
	float WatchTargetYaw=0,WatchHoldSeconds=0,WatchTurnSpeed=75;
	bool bWatchTargetChosen=false;
	bool bSpawnedAtCorner=false;
	float NextSprintDecision=0,NextWatchDecision=0;
	bool bWantsSprint=false,bWantsWatch=false;
	float NextPacingDecision=0,PauseUntil=0,WatchYawOffset=0;
};

UENUM(BlueprintType)
enum class EProjectTutorialCondition : uint8
{
	Continue, Walk, Sprint, Ping, ObserveMannequin, SelectMannequin, ManualControl,
	MoveMannequin, IssueCommand, ReleaseMannequin, Capture, Trigger,
	CommandPractice, PatrolCommand, ChaseCommand, FollowCommand, ResetFollow, ToggleContactDamage, DirectAttack
};

/** 맵의 TutorialPlan에서 순서·문구·조건·목표량을 편집합니다. */
USTRUCT(BlueprintType)
struct FProjectTutorialStep
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MultiLine=true)) FText Instructions;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EProjectTutorialCondition Condition = EProjectTutorialCondition::Continue;
	/** 이동은 cm, 주시는 초. 나머지 조건은 유효한 행동 횟수입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float RequiredAmount = 1;
	/** Legacy saved-plan compatibility only. Visible goal completion no longer has a hidden time gate. */
	UPROPERTY() float MinimumPracticeSeconds = 0;
	/** Trigger 조건일 때 ObjectiveTrigger의 TutorialEvent와 일치시킵니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Event;
};

USTRUCT(BlueprintType)
struct FProjectObjectiveState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName Id;
	UPROPERTY(BlueprintReadOnly) FText Title;
	UPROPERTY(BlueprintReadOnly) FText Instructions;
	UPROPERTY(BlueprintReadOnly) int32 Step = 0;
	UPROPERTY(BlueprintReadOnly) int32 Total = 0;
	UPROPERTY(BlueprintReadOnly) bool bCompleted = false;
	UPROPERTY() uint32 Revision = 0;
};

/** 해당 소유자에게만 복제되는 목표. 싱글 Pawn에는 추가하지 않습니다. */
UCLASS(ClassGroup=(ProjectProject01), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UProjectProject01ObjectiveComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProjectProject01ObjectiveComponent();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Multiplayer|Objective")
	bool UpdateObjective(FName Id, FText Title, FText Instructions, int32 Step=0, int32 Total=0);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Multiplayer|Objective") void CompleteObjective();
	UFUNCTION(BlueprintPure, Category="Multiplayer|Objective") FProjectObjectiveState GetObjective() const { return State; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY(ReplicatedUsing=OnRep_State) FProjectObjectiveState State;
	UFUNCTION() void OnRep_State();
	UPROPERTY(Transient) TObjectPtr<class UProjectProject01TransientNoticeWidget> HUD;
	uint32 LastShownRevision = MAX_uint32;
};

/** 별도 튜토리얼 맵에 하나 배치. 배열에 단계를 추가/삭제/재정렬할 수 있습니다. */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectProject01TutorialPlan : public AActor
{
	GENERATED_BODY()
public:
	AProjectProject01TutorialPlan();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") TArray<FProjectTutorialStep> SurvivorSteps;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") TArray<FProjectTutorialStep> MannequinSteps;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial", meta=(MultiLine=true)) FText SurvivorClosing;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial", meta=(MultiLine=true)) FText MannequinClosing;
	/** 마네킹 실전 단계에서 연습용 생존자 1명을 스폰할 위치. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial", meta=(MakeEditWidget=true)) FVector PracticeTargetOffset = FVector(600,0,100);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Practice Survivor", meta=(ClampMin="50", ClampMax="500")) float PracticeTargetSpeed=220;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Practice Survivor", meta=(ClampMin="100", ClampMax="1000")) float PracticeTargetSprintSpeed=550;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Practice Survivor", meta=(ClampMin="200")) float PracticeTargetRoamRadius=900;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Practice Survivor", meta=(ClampMin="0.5")) float PracticeTargetWatchSeconds=1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Practice Survivor", meta=(ClampMin="1")) float PracticeTargetLookAwaySeconds=2.5f;
};

UENUM(BlueprintType)
enum class EProjectObjectiveAudience : uint8 { EnteringPlayer, Survivors, Mannequin, Everyone };

/** 목표 표시/갱신 또는 튜토리얼 현재 단계 달성. 서버의 실제 겹침만 인정합니다. */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectProject01ObjectiveTrigger : public AActor
{
	GENERATED_BODY()
	friend class FProjectTutorialTest;
    friend class FProjectTutorialRuntimeTest;
    friend class FProjectFreePracticeRuntimeTest;
	friend class AProjectProject01TutorialGameMode;
public:
	AProjectProject01ObjectiveTrigger();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective") TObjectPtr<class UBoxComponent> Volume;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") EProjectObjectiveAudience Audience=EProjectObjectiveAudience::EnteringPlayer;
	/** 비어 있지 않으면 이 목표를 진행 중인 플레이어만 갱신합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") FName RequiredObjective;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") FName NewObjectiveId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") bool bCompleteCurrentObjective=false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective", meta=(MultiLine=true)) FText Instructions;
	/** 튜토리얼 단계 달성 전용. 실제 매치에서는 이 이벤트를 무시합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") FName TutorialEvent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective") bool bOncePerPlayer=true;
protected:
	virtual void BeginPlay() override;
private:
	UFUNCTION() void OnOverlap(UPrimitiveComponent* Component, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
	bool ApplyTo(class AMultiplayTestPlayerController* PC);
	TSet<TWeakObjectPtr<AMultiplayTestPlayerController>> UsedBy;
};

/** 멀티 규칙을 사용하지만 외부 접속·EOS·기록 제출을 하지 않는 로컬 연습 전용 모드. */
UCLASS(Blueprintable)
class PROJECTPROJECT01_API AProjectProject01TutorialMessageTrigger : public AActor
{
	GENERATED_BODY()
public:
	AProjectProject01TutorialMessageTrigger();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tutorial") TObjectPtr<class UBoxComponent> Volume;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial", meta=(MultiLine=true)) FText Message;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") FName RequiredStep;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") bool bOnce=true;
protected:
	virtual void BeginPlay() override;
private:
	UFUNCTION() void OnOverlap(UPrimitiveComponent* Component, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
	bool bShown=false;
};

UCLASS()
class PROJECTPROJECT01_API AProjectProject01TutorialGameMode : public AMultiplayTestGameMode
{
	GENERATED_BODY()
	friend class FProjectTutorialTest;
    friend class FProjectTutorialRuntimeTest;
	friend class FProjectFreePracticeRuntimeTest;
	friend class FProjectPracticeDirectorTest;
public:
	static bool IsTutorialWorld(const UWorld* World);
	virtual void InitGame(const FString& Map, const FString& Options, FString& Error) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& Id, FString& Error) override;
	virtual FString InitNewPlayer(APlayerController*, const FUniqueNetIdRepl&, const FString&, const FString&) override;
	virtual void PostLogin(APlayerController*) override;
	virtual void BeginPlay() override;
	virtual void Tick(float Delta) override;
	void RecordAction(AMultiplayTestPlayerController* PC, EProjectTutorialCondition Action);
	bool SignalEvent(AMultiplayTestPlayerController* PC, FName Event);
	bool ShowMessage(AMultiplayTestPlayerController* PC, FText Message, FName RequiredStep=NAME_None);
	FText GetExtraMessage() const { return ExtraMessage; }
	void ContinueTutorial();
	void ToggleTutorialMenu();
	void ReturnToMatching();
	void RestartTutorial(bool bPracticeOnly = false);
	bool IsMannequinTutorial() const { return bMannequin; }
	AMultiplayTestPlayerController* GetLearnerController() const { return Learner; }
	bool IsResultVisible() const { return bResultVisible; }
	bool IsFinishing() const { return bFinishing; }
	bool IsAwaitingRead() const { return bAwaitingRead; }
	const FProjectTutorialStep* GetCurrentStep() const;
	FText GetClosingText() const;
	FText GetPracticeStatusText() const;
	bool IsFreePractice() const { return bFreePractice; }
	bool IsPracticeMenuOpen() const { return bPracticeMenuOpen; }
	bool IsPracticeRoundEnded() const { return bPracticeRoundEnded; }
	int32 GetPracticeMannequinCount() const { return PracticeMannequins; }
	int32 GetPracticeSurvivorCount() const { return PracticeSurvivors; }
	bool ArePracticeSurvivorsStationary() const { return bPracticeStationary; }
	bool DoStationarySurvivorsLookAround() const { return bPracticeStandingWatch; }
	bool HasPracticeInfiniteLives() const { return bFreePractice && bPracticeInfiniteLives; }
	bool IsPracticeCornerPlacementEnabled() const { return bPracticeCorners; }
	int32 GetPracticeCornerCount() const { return PracticeCornerCount; }
	bool IsPracticeDirectorEnabled() const { return bFreePractice && !bMannequin && bPracticeDirector; }
	bool IsPracticeDirectorConfigured() const { return bPracticeDirector; }
	bool CanPracticeDirectorToggleContact() const { return bPracticeDirectorToggleContact; }
	FText GetPracticeContactDamageText() const;
	bool IsPracticeContactDamageEnabled() const;
	FText GetFreePracticeStatus() const;
	void ShowPracticeMenu(bool bShow);
	void RestartFreePractice(bool bAsMannequin,int32 Mannequins,int32 Survivors,bool bStationary=false,bool bStandingWatch=true,bool bInfiniteLives=false,bool bCorners=false,int32 CornerCount=1,bool bDirector=false,bool bAllowContactToggle=true);
protected:
	virtual void FinishAuthoritativeMatch(const FString& Reason) override;
	virtual AMannequinAICharacter* GetMannequinPlayerFocusPawn() const override;
private:
	void InitializePractice();
	void EnterStep();
	void AdvanceStep();
	void FinishTutorial();
	void SpawnPracticeSurvivor();
	void SpawnPracticeSurvivors(int32 Count);
	bool FindPracticeCornerLocation(const FVector& Anchor,float Radius,float HalfHeight,const TArray<FVector>& Reserved,FVector& OutLocation,FRotator& OutRotation) const;
	void UpdatePracticeTactics();
	void TickPracticeMannequinDirector(float Delta);
	void UpdatePracticeDirectorPacing(float Now);
	void SelectPracticeDirectorBody(AMannequinAICharacter* Body);
	bool IssuePracticeDirectorCommand(EMultiplayMannequinCommand Command);
	bool ResetPracticeDirectorFollowers();
	bool SetPracticeDirectorContactDamage(bool bEnabled);
	void TickPracticeSurvivor(float Delta);
	void UpdatePracticeTargetPath(FProjectTutorialSurvivorBot& Bot,int32 Index);
	class AMannequinAICharacter* FindPracticeThreat(const FProjectTutorialSurvivorBot& Bot) const;
	bool IsPracticeTargetSegmentClear(const FProjectTutorialSurvivorBot& Bot,const FVector& From,const FVector& To) const;
	void FinishFreePractice();
	bool TryFinishPracticeStep();
	int32 GetInitialStepIndex() const;
	void SetMenuInput(bool bShowCursor);
	UPROPERTY(Transient) TObjectPtr<AProjectProject01TutorialPlan> Plan;
	UPROPERTY(Transient) TObjectPtr<AMultiplayTestPlayerController> Learner;
	UPROPERTY(Transient) TObjectPtr<class UProjectProject01TutorialWidget> Widget;
	TArray<FProjectTutorialStep> Steps;
	int32 StepIndex=0;
	float Progress=0, EndSeconds=0;
	TWeakObjectPtr<class AMannequinAICharacter> LastSelectedMannequin, CommandedMannequin, ReleasedMannequin;
	UPROPERTY(Transient) TArray<FProjectTutorialSurvivorBot> PracticeBots;
	UPROPERTY(Transient) TObjectPtr<class UProjectProject01PracticeWidget> PracticeWidget;
	bool bFreePractice=false,bPracticeMenuOpen=false,bPracticeRoundEnded=false,bPracticeAwaitingPresentation=false;
	int32 PracticeMannequins=10,PracticeSurvivors=1;
	bool bPracticeStationary=false,bPracticeStandingWatch=true;
	bool bPracticeInfiniteLives=false;
	bool bPracticeCorners=false;
	bool bPracticeDirector=false;
	bool bPracticeDirectorToggleContact=true;
	int32 PracticeCornerCount=1,PracticeCornersPlaced=0;
	int32 PracticeTacticalPhase=0,PracticeCoverIndex=0;
	float NextTacticalDecision=0,NextCoverDecision=0,NextDirectorDecision=0,NextDirectorAttack=0;
	float NextDirectorMove=0;
	float NextDirectorAction=0,DirectorManualReadyAt=0,NextDirectorContactToggle=0,ContactChangedUntil=0;
	bool bDirectorCommandIssued=false;
	uint8 DirectorBeat=0; // pressure / probe / reposition / brief pause; never a fixed cycle
	float NextDirectorPacing=0,DirectorStandOff=420,DirectorRepositionAngle=70,DirectorRepositionRadius=450;
	FVector DirectorLastSeen=FVector::ZeroVector;
	FVector DirectorApproachOffset=FVector::ZeroVector;
	float DirectorLastSeenSeconds=-100;
	TWeakObjectPtr<class AMannequinAICharacter> PracticeDirectorBody;
	float PracticeTacticalSeconds=0,PracticeEndSeconds=0;
	FRandomStream PracticeRandom;
	FVector LastLocation=FVector::ZeroVector;
	bool bMannequin=false, bInitialized=false, bFinishing=false, bResultVisible=false, bReturning=false;
	bool bPracticeOnly=false;
	bool bAwaitingRead=true, bClosingConfirmed=false;
	FText ExtraMessage;
	FTimerHandle ReturnTimer;
};

/** Free practice controls are independent of the tutorial's reading/progression UI. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01PracticeWidget : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectFreePracticeRuntimeTest;
protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry&,float Delta) override;
	virtual FReply NativeOnKeyDown(const FGeometry&,const FKeyEvent&) override;
private:
	UFUNCTION() void HandleToggle();
	UFUNCTION() void HandleApply();
	UFUNCTION() void HandleSurvivor();
	UFUNCTION() void HandleMannequin();
	UFUNCTION() void HandleReturn();
	UFUNCTION() void LessMannequins();
	UFUNCTION() void MoreMannequins();
	UFUNCTION() void LessSurvivors();
	UFUNCTION() void MoreSurvivors();
	UFUNCTION() void LessCorners();
	UFUNCTION() void MoreCorners();
	void ApplyRole(bool bMannequin);
	static void SetCount(class USpinBox* Input,int32 Delta,int32 Maximum);
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> SettingsPanel;
	UPROPERTY(Transient) TObjectPtr<class USizeBox> PanelBounds;
	UPROPERTY(Transient) TObjectPtr<class UButton> ToggleButton;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ToggleText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<class UBorder> ContactPanel;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ContactStatus;
	UPROPERTY(Transient) TObjectPtr<class USpinBox> MannequinCount;
	UPROPERTY(Transient) TObjectPtr<class USpinBox> SurvivorCount;
	UPROPERTY(Transient) TObjectPtr<class UButton> MannequinMinus;
	UPROPERTY(Transient) TObjectPtr<class UButton> MannequinPlus;
	UPROPERTY(Transient) TObjectPtr<class UButton> SurvivorMinus;
	UPROPERTY(Transient) TObjectPtr<class UButton> SurvivorPlus;
	UPROPERTY(Transient) TObjectPtr<class UButton> SurvivorSwitch;
	UPROPERTY(Transient) TObjectPtr<class UButton> MannequinSwitch;
	UPROPERTY(Transient) TObjectPtr<class UButton> ApplyButton;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> ApplyText;
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> SurvivorControls;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> StationaryToggle;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> StandingWatchToggle;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> InfiniteLivesToggle;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> DirectorToggle;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> DirectorContactToggle;
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> CornerToggle;
	UPROPERTY(Transient) TObjectPtr<class USpinBox> CornerCount;
	UPROPERTY(Transient) TObjectPtr<class UButton> CornerMinus;
	UPROPERTY(Transient) TObjectPtr<class UButton> CornerPlus;
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> CornerControls;
};

UCLASS()
class PROJECTPROJECT01_API UProjectProject01TutorialWidget : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectTutorialTest;
protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry&, float Delta) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
	virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
private:
	UFUNCTION() void HandleContinue();
	UFUNCTION() void HandleReturn();
	UFUNCTION() void HandleRestart();
	UFUNCTION() void HandleRestartFromBeginning();
	UPROPERTY(Transient) TObjectPtr<class UBorder> Panel;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> Heading;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> Body;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> PracticeStatus;
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> ObjectiveStack;
	UPROPERTY(Transient) TObjectPtr<class UButton> ContinueButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> RestartButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> FullRestartButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> ScreenButton;
};
