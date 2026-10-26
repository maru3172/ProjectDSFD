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

/** A bot's own growth: self-made combos and self-tuned play knobs. Kept across floors and, through the
 *  backend, across matches (one pool per role and level, split by play style). Always level-capped. */
struct FProjectBotExperience
{
    ProjectBots::ComboBook Combos;
    ProjectBots::PlayTuning Tuning;
};

/** What the bots learned on one floor and keep for the next: opponent habits (by player identity, since
 *  pawns are recreated) and the in-match relief. Personality/style need no carrying: they derive from the bot id. */
struct FProjectBotLearning
{
    TMap<FString,ProjectBots::Pattern> SurvivorHabits; // Mannequin team's learning, keyed by survivor UserId.
    ProjectBots::Relief MannequinRelief,SurvivorRelief;
    TMap<FString,FProjectBotExperience> Experience; // Keyed by bot id.
    int32 FloorDepth=0; // Floor these were learned on (0 = first floor of the match).
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
    // Survivor craft: every mannequin actually seen recently (not just the nearest), and whether a
    // teammate is currently freezing it; discipline is re-rolled every few seconds by level.
    struct FSeenThreat{TWeakObjectPtr<AActor> Actor;FVector Position=FVector::ZeroVector;double At=-100;bool Covered=false;};
    TArray<FSeenThreat> Threats;
    // Where each mannequin was last seen, kept past the threat memory so experts can route around that area.
    struct FSighting{TWeakObjectPtr<AActor> Actor;FVector Position=FVector::ZeroVector;double At=-100;};
    TArray<FSighting> Sightings;
    double DisciplineUntil=0,SoloCheckAt=0;
    bool Disciplined=false;
    // Emotion edges (near miss -> shaken -> "phew"), fidgeting while waiting, per-match condition refresh.
    bool WasNear=false,Shaken=false;
    double IdleSince=-1,FidgetAt=0,ShuffleUntil=0,ConditionAt=-100,BreatheUntil=0,PressedAt=-100;
    // Gaze budget: a stare is held only so long, then the eyes leave it for a beat (the mannequin's window).
    double WatchSince=-1,LookAwayUntil=0;
    double NoPathSince=-1; // Has a goal, is not holding, yet cannot move toward it.
    // Stuck watchdog: barely moved for a while outside a deliberate hold -> go somewhere else.
    FVector AnchorAt=FVector::ZeroVector;
    double AnchorTime=-1,UnstickUntil=0;
    double LongFreezeAt=-100; // Mannequin body: last moment it had been held frozen for a while (FakeRetreat).
    FVector FidgetLook=FVector::ZeroVector,ShuffleTo=FVector::ZeroVector;
    bool WasPeeking=false;
    // Look back while stopped (see TickSurvivor).
    bool WasHolding=false;double BackCheckAt=0,BackCheckUntil=0;FVector BackDirection=FVector::ZeroVector;
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
    friend class FProjectBotSimulationTest;
public:
    bool AcceptRoster(const FString& Room,const FString& Match,const TArray<FProjectMatchBotSpec>& Bots,FString& Error);
    void Tick(float Delta);
    const TArray<FProjectMatchBotSpec>& GetRoster()const{return Specs;}
    const FString& GetRoomId()const{return SourceRoom;}
    bool HasRoster()const{return !Specs.IsEmpty();}
    /** Level travel: hand learned habits and relief to the next floor's adapter. */
    TSharedPtr<FProjectBotLearning> ExportLearning()const;
    void ImportLearning(const FProjectBotLearning& Learning);
    /** Match end: this match's growth merged into each role/level pool the bots arrived with. */
    TArray<FProjectBotExperienceUpload> ExportExperience()const;
    /** Pool text: "v1" then "|style;from-to-score10,...;play-duration100-reach100,..." per play style. */
    static FString EncodeExperience(const TMap<uint8,FProjectBotExperience>& Styles);
    static bool DecodeExperience(const FString& Text,TMap<uint8,FProjectBotExperience>& Styles);
    static uint8 StyleOf(const FProjectMatchBotSpec& Spec);
    /** A survivor's team ping (human or bot): bots look, learn the exit, or come to help. */
    void HearPing(class AMultiplayTestPlayerController* Sender,uint8 Type,const FVector& Location); // Type: EProjectProject01PingType
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
    int32 FloorDepth=0; // How many floors down this match is (ProjectBots::FloorResolve).
    // Progress clock for stalemates (ProjectBots::StallSeconds): survivors closing on exits, catches, escapes.
    double LastProgressAt=-1,StallLogAt=0;
    int32 LastOutcomeCount=-1;
    TMap<FString,float> ClosestToExit; // Per survivor bot: best distance to its current exit so far.
    TMap<FString,TWeakObjectPtr<AActor>> ProgressExit;
    void NoteProgress(double Now);
    // A walkable point inside an exit volume (overlap triggers it), cached per volume.
    FVector InsideGoal(const AActor* Objective);
    TMap<TWeakObjectPtr<const AActor>,FVector> InsideGoals;
    bool IsStalled(const ProjectBots::Difficulty& Level,double Now)const{return LastProgressAt>=0&&Now-LastProgressAt>ProjectBots::StallSeconds(Level);}
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
        ProjectBots::ComboBook Combos; // Self-made play chains, learned from what worked.
        ProjectBots::PlayTuning Tuning; // Self-tuned duration/reach per play.
        uint32 ActiveMask=0;           // Plays running at the last check, to notice which ones started/ended.
        bool ExperienceLoaded=false;   // Stored experience applied (or carried from the previous floor).
        ProjectBots::Mood Mood;        // Fear (survivor) and frustration (mannequin).
        // Map knowledge on this floor: exits are known only once seen, told or pinged.
        TSet<TWeakObjectPtr<AActor>> KnownExits;
        bool ExitsSeeded=false;
        double ExitScanAt=0,ExploreUntil=0;
        FVector ExploreGoal=FVector::ZeroVector;
        TArray<FVector> Visited;
        // Pings: one queued call-out at a time (a person reacts before pressing the key), plus a teammate's call for help.
        double PingReadyAt=0,PingDueAt=-1,HelpUntil=0;
        uint8 PingKind=0;
        FVector PingPoint=FVector::ZeroVector,HelpPoint=FVector::ZeroVector;
        int32 Sweep=0; // SearchSweep/ExitSweep: index of the current point.
        double HelperSentAt=-100; // Mannequin: last time a free body was sent in while the viewed one was held frozen.
        // Mannequin: bodies a lower-level player leaves parked (existing Hold command), and when that was last reviewed.
        TArray<TWeakObjectPtr<class AMannequinAICharacter>> Parked;
        double ParkCheckAt=0;
        // Last survivor's way out (ProjectBots::LoneApproach) and its working state.
        uint8 Lone=255; // 255 = not chosen yet.
        bool LoneGuarded=false;
        double LoneSince=0,LoneCheckAt=0;
        FVector LonePoint=FVector::ZeroVector;
        TWeakObjectPtr<AActor> LoneAvoid; // The guarded exit a detour stays away from.
        double StallLogAt=0; // Next diagnostic line while the match is stalled.
        // Exits it could not find a way to (no path from here): skipped for a while so another exit is tried.
        TMap<TWeakObjectPtr<AActor>,double> UnreachableUntil;
        // Pair plays run on both partners: the starter holds the Director slot, the partner follows this.
        uint8 PartnerPlay=0;
        double PartnerStart=0,PartnerUntil=0;
        bool PartnerLead=false;
        float PartnerSide=0;
    };
    TMap<FString,FBotPlayState> Plays;
    // Feed started/ended plays to the bot's combo book and tuning; Worked judges the outcome from the bot's own side.
    static void TrackLearning(FBotPlayState& State,const ProjectBots::Difficulty& Level,double Now,bool Worked);
    // First tick of a bot: apply its style's stored experience, capped to its level.
    static void LoadExperience(FBotPlayState& State,const FProjectMatchBotSpec& Spec,const ProjectBots::Difficulty& Level);
    // Map knowledge: seeded by familiarity on first use, then grown by sight (Eyes) and teammates.
    bool KnowsExit(FBotPlayState& State,const FProjectMatchBotSpec& Spec,const AActor* Exit,const ProjectBots::Difficulty& Level);
    // Returns an exit found by this scan (to call out), or null.
    AActor* DiscoverExits(FBotPlayState& State,const FProjectMatchBotSpec& Spec,TArrayView<APawn* const> Eyes,const ProjectBots::Difficulty& Level,double Now);
    // Nearest known exit to a point, or null.
    AActor* NearestKnownExit(FBotPlayState& State,const FProjectMatchBotSpec& Spec,const FVector& From,const ProjectBots::Difficulty& Level);
    // A reachable spot away from where this bot has already been.
    bool ChooseExploreGoal(APawn* Pawn,FBotPlayState& State,double Now,FVector& Goal);
    // Queue/send a ping through the real team-ping path.
    void QueuePing(FBotPlayState& State,const ProjectBots::Difficulty& Level,const ProjectBots::Persona& Who,uint8 Kind,const FVector& At,double Now,float U);
    void FlushPing(class AMultiplayTestPlayerController* PC,FBotPlayState& State,double Now);
    float ConditionOf(const FProjectMatchBotSpec& Spec)const;
    // This match's team plans (same on every floor: derived from the match id).
    ProjectBots::MatchScript ScriptOf()const{return ProjectBots::MatchScript::For(GetTypeHash(MatchId)^0x5bd1e995u);}
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
