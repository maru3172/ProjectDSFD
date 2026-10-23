#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectAdaptiveBotPolicy.h"
#include "ProjectMatchBotTypes.h"
#include "ProjectMatchBots.generated.h"

UCLASS()
class PROJECTPROJECT01_API UProjectMatchBotMovement final : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    virtual void TickComponent(float Delta,ELevelTick Kind,FActorComponentTickFunction* TickFunction) override;
protected:
    virtual void ServerAutonomousProxyTick(float Delta) override;
private:
    FVector ServerInput=FVector::ZeroVector;
};

UCLASS(NotBlueprintable)
class PROJECTPROJECT01_API AProjectMatchBotCharacter final : public APlayerCharacter
{
    GENERATED_BODY()
public:
    AProjectMatchBotCharacter(const FObjectInitializer& Initializer=FObjectInitializer::Get());
    void SetBotSprint(bool Sprint){SetSprinting(Sprint);}
    /** Wear the match's default survivor look (mesh, animation, materials, capsule). Runs on server AND
     *  clients: component assets are not replicated, so a server-only copy left bots invisible to players. */
    bool ApplyDefaultPawnAppearance();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    bool bAppearanceApplied=false;
};

/** What the bots learned on one floor and keep for the next: opponent habits (by player identity, since
 *  pawns are recreated) and the in-match relief. Personality/style need no carrying: they derive from the bot id. */
struct FProjectBotLearning
{
    TMap<FString,ProjectBots::Pattern> SurvivorHabits; // Mannequin team's learning, keyed by survivor UserId.
    ProjectBots::Relief MannequinRelief,SurvivorRelief;
    double SavedWorldSeconds=0,SavedRealSeconds=0;
};

struct FProjectMatchBotBrain
{
    ProjectBots::Policy Policy;
    TWeakObjectPtr<AActor> Target;
    FVector Known=FVector::ZeroVector;
    TArray<FVector> Path;
    int32 PathIndex=0;
    double SeenAt=-100,ReactAt=0,SenseAt=0,PathAt=0,AttackAt=0,CommandAt=0;
    // Cached environment-based tactic point and rest-spot hunt state.
    FVector TacticGoal=FVector::ZeroVector,TacticKnown=FVector::ZeroVector;
    double TacticAt=-100,SpotIgnoreUntil=0;
    uint8 TacticFor=255;
    // Human behaviour layer: gaze spring, corner peeks, glances and "go check where it vanished".
    ProjectBots::Gaze Gaze;
    FVector PeekDirection=FVector::ZeroVector,GlanceDirection=FVector::ZeroVector,LostCheck=FVector::ZeroVector;
    double PauseUntil=0,PeekCooldown=0,GlanceUntil=0,NextGlanceAt=0,LostCheckUntil=0;
    double RetargetAt=0,HearAt=0,FrozenSince=-1,PlayCheckAt=0;
    bool WasPeeking=false;
    float PreviousYaw=0;
    bool Initialized=false,Visible=false,Recovering=false,TacticFound=false,HadKnowledge=false;
};

/** Server-only execution adapter. Shares decision code, never the tutorial mode or practice UI. */
UCLASS()
class PROJECTPROJECT01_API UProjectMatchBots final : public UObject
{
    GENERATED_BODY()
    friend class FProjectMatchBotsRuntimeTest;
    friend class FProjectActualBotMatchEditorTest;
public:
    bool AcceptRoster(const FString& Room,const FString& Match,const TArray<FProjectMatchBotSpec>& Bots,FString& Error);
    void Tick(float Delta);
    const TArray<FProjectMatchBotSpec>& GetRoster()const{return Specs;}
    const FString& GetRoomId()const{return SourceRoom;}
    bool HasRoster()const{return !Specs.IsEmpty();}
    /** Level travel: hand learned habits and relief to the next floor's adapter. */
    TSharedPtr<FProjectBotLearning> ExportLearning()const;
    void ImportLearning(const FProjectBotLearning& Learning);
private:
    class AMultiplayTestGameMode* Mode()const;
    bool SpawnParticipants();
    void TickSurvivor(class AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta);
    void TickMannequin(class AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta);
    void Sense(APawn* Observer,AActor* Target,uint32 Team,FProjectMatchBotBrain& Brain,bool Visible,bool HumanAlly=false);
    ProjectBots::Context Context(const FProjectMatchBotBrain& Brain,bool Mannequin)const;
    bool Move(APawn* Pawn,FProjectMatchBotBrain& Brain,const FVector& Goal,float Delta,bool FaceTravel=true,bool Calm=false);
    FRotator HumanLook(FProjectMatchBotBrain& Brain,const FRotator& Current,const FRotator& Target,float Delta);
    void InitializeBrain(FProjectMatchBotBrain& Brain,const FProjectMatchBotSpec& Spec,uint32 Salt);
    // Origin/Preferred/WantsSight override the defaults for play-specific points; CacheKey separates them from tactics.
    bool ChooseTacticPoint(APawn* Pawn,FProjectMatchBotBrain& Brain,ProjectBots::Action Tactic,bool Mannequin,float Radius,FVector& Goal,
        const FVector* Origin=nullptr,const FVector* Preferred=nullptr,bool WantsSight=false,uint8 CacheKey=255);
    bool Hear(APawn* Listener,AActor* Source,float Mistake,FVector& Heard)const;
    void ScanObjectives();
    void TrackRelief(double Now);
    int32 TeamPlays(const FString& Role,const FString& Except)const;
    AActor* ChooseObjective(APawn* Pawn,const FProjectMatchBotBrain& Brain,const FProjectMatchBotSpec& Spec,bool Varied,const AActor* Avoid,FVector& Goal,FVector& Look);
    TArray<FProjectMatchBotSpec> Specs;
    TMap<FString,TWeakObjectPtr<class AMultiplayTestPlayerController>> Controllers;
    TMap<TWeakObjectPtr<AActor>,FProjectMatchBotBrain> Brains;
    TArray<TWeakObjectPtr<AActor>> Objectives;
    ProjectBots::SharedSenses Shared;
    FString SourceRoom,MatchId;
    bool Spawned=false,Failed=false,ScannedObjectives=false;
    double SwitchBodyAt=0,PruneBrainsAt=0;
    // Telegraphed plays per bot identity (signal -> execute -> cooldown).
    struct FBotPlayState
    {
        ProjectBots::Director Director;
        TWeakObjectPtr<AActor> Subject,Helper,Objective;
        FVector Point=FVector::ZeroVector,Point2=FVector::ZeroVector;
        ProjectBots::Action Side=ProjectBots::Action::FlankLeft;
        double StepUntil=0; // StepApproach: end of the current advance/statue phase.
        bool Stepping=false;
    };
    TMap<FString,FBotPlayState> Plays;
    // Habits from the previous floor, re-attached to a survivor the first time its new pawn is seen.
    TMap<FString,ProjectBots::Pattern> PendingSurvivorHabits;
    // In-match relief for the human side facing each bot team, fed by observed setbacks.
    ProjectBots::Relief MannequinRelief,SurvivorRelief;
    struct FRecordWatch{int32 Deaths=-1,Captures=0;uint8 State=255;double CapturedAt=0;}; // State: EMultiplayTestSurvivorState
    TMap<FString,FRecordWatch> RecordWatch;
    TSet<TWeakObjectPtr<AActor>> CoveringSurvivors;
    FString BaitBotId;
    double ReliefScanAt=0,CoverScanAt=0,BaitUntil=0;
};
