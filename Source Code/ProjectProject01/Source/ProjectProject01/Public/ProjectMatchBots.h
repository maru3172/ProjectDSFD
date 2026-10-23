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
};

struct FProjectMatchBotBrain
{
    ProjectBots::Policy Policy;
    TWeakObjectPtr<AActor> Target;
    FVector Known=FVector::ZeroVector;
    TArray<FVector> Path;
    int32 PathIndex=0;
    double SeenAt=-100,ReactAt=0,SenseAt=0,PathAt=0,AttackAt=0,CommandAt=0;
    float PreviousYaw=0;
    bool Initialized=false,Visible=false,Recovering=false;
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
private:
    class AMultiplayTestGameMode* Mode()const;
    bool SpawnParticipants();
    void TickSurvivor(class AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta);
    void TickMannequin(class AMultiplayTestPlayerController* PC,const FProjectMatchBotSpec& Spec,float Delta);
    void Sense(APawn* Observer,AActor* Target,uint32 Team,FProjectMatchBotBrain& Brain,bool Visible,bool HumanAlly=false);
    ProjectBots::Context Context(const FProjectMatchBotBrain& Brain,bool Mannequin)const;
    bool Move(APawn* Pawn,FProjectMatchBotBrain& Brain,const FVector& Goal,float Delta,bool FaceTravel=true);
    TArray<FProjectMatchBotSpec> Specs;
    TMap<FString,TWeakObjectPtr<class AMultiplayTestPlayerController>> Controllers;
    TMap<TWeakObjectPtr<AActor>,FProjectMatchBotBrain> Brains;
    TArray<TWeakObjectPtr<AActor>> Objectives;
    ProjectBots::SharedSenses Shared;
    FString SourceRoom,MatchId;
    bool Spawned=false,Failed=false,ScannedObjectives=false;
    double SwitchBodyAt=0;
};
