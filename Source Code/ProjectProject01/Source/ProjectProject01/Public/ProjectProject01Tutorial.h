#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01Tutorial.generated.h"

UENUM(BlueprintType)
enum class EProjectTutorialCondition : uint8
{
	Continue, Walk, Sprint, Ping, ObserveMannequin, SelectMannequin, ManualControl,
	MoveMannequin, IssueCommand, ReleaseMannequin, Capture, Trigger
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
	bool IsResultVisible() const { return bResultVisible; }
	bool IsFinishing() const { return bFinishing; }
	bool IsAwaitingRead() const { return bAwaitingRead; }
	const FProjectTutorialStep* GetCurrentStep() const;
	FText GetClosingText() const;
protected:
	virtual void FinishAuthoritativeMatch(const FString& Reason) override;
private:
	void InitializePractice();
	void EnterStep();
	void AdvanceStep();
	void FinishTutorial();
	void SpawnPracticeSurvivor();
	int32 GetInitialStepIndex() const;
	void SetMenuInput(bool bShowCursor);
	UPROPERTY(Transient) TObjectPtr<AProjectProject01TutorialPlan> Plan;
	UPROPERTY(Transient) TObjectPtr<AMultiplayTestPlayerController> Learner;
	UPROPERTY(Transient) TObjectPtr<class UProjectProject01TutorialWidget> Widget;
	TArray<FProjectTutorialStep> Steps;
	int32 StepIndex=0;
	float Progress=0, EndSeconds=0;
	FVector LastLocation=FVector::ZeroVector;
	bool bMannequin=false, bInitialized=false, bFinishing=false, bResultVisible=false, bReturning=false;
	bool bPracticeOnly=false;
	bool bAwaitingRead=true, bClosingConfirmed=false;
	FText ExtraMessage;
	FTimerHandle ReturnTimer;
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
	UPROPERTY(Transient) TObjectPtr<class UButton> ContinueButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> RestartButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> FullRestartButton;
	UPROPERTY(Transient) TObjectPtr<class UButton> ScreenButton;
};
