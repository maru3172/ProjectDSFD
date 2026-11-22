// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InteractionActorBase.h"
#include "FireShutterActor.generated.h"

class APawn;
class UNavModifierComponent;
class UStaticMesh;

/** Where a placed shutter sits in the per-match layout (Docs/FireShutterLayout.md). */
UENUM(BlueprintType)
enum class EProjectShutterLayoutRole : uint8
{
	None,      // Not part of the layout: behaves exactly as before.
	Corridor,  // Corridor candidate: sealed (broken, down) or raised out of the way, per match.
	Escalator, // Around an escalator opening: one working (operable) shutter per opening, the rest broken.
	Store,     // Store front (normal shutter): operable, starts down or up at random.
	Chokepoint, // A narrow passage between areas: closed now and then, always with one working shutter in the row.
	Exit       // The row in front of the 1F exit: always closed every match, working shutters mixed with broken ones.
};

/** What the layout made of the shutter this match. */
UENUM(BlueprintType)
enum class EProjectShutterLayoutState : uint8
{
	Default,  // Untouched by the layout.
	Sealed,   // Broken and down: blocks, cannot be operated.
	Raised,   // Unused corridor slot: up and out of the way, cannot be operated.
	Operable, // Working shutter: players raise and lower it as usual.
	Hanging   // Broken and stuck high overhead: people pass under it, it only hides the view above (ruined look).
};

USTRUCT()
struct FProjectShutterMotion
{
    GENERATED_BODY()
    UPROPERTY() float Start=0;
    UPROPERTY() float Target=0;
    UPROPERTY() float Speed=150;
    UPROPERTY() float Height=300;
    UPROPERTY() double StartedAt=0;
};

/**
 * CurrentOpenAlpha
 * 0.0 = 완전히 닫힘
 * 1.0 = 완전히 열림
 */
UCLASS()
class PROJECTPROJECT01_API AFireShutterActor : public AInteractionActorBase
{
	GENERATED_BODY()
	friend class FProjectShutterTest;
	friend class FProjectShutterLayoutRuntimeTest;
	
public:
	AFireShutterActor();
	
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool CanInteract_Implementation(APawn* Interactor) const override;
    bool TryInteractFromView(APawn* Interactor,const FVector& Origin,const FVector& Direction,float Range,ECollisionChannel Channel);

	EProjectShutterLayoutRole GetLayoutRole() const { return LayoutRole; }
	FName GetEscalatorId() const { return EscalatorId; }
	EProjectShutterLayoutState GetLayoutState() const { return LayoutState; }
	/** Server/standalone: puts the shutter into its per-match state at once (up when bOpen). Called after BeginPlay. */
	void ApplyLayout(EProjectShutterLayoutState NewState, bool bOpen) { ApplyLayout(NewState, bOpen ? 1.f : 0.f, 0.f); }
	/** Same, at any height (0 down .. 1 up) and skewed in its own plane by TiltDegrees (a jammed, bent shutter). */
	void ApplyLayout(EProjectShutterLayoutState NewState, float OpenAlpha, float TiltDegrees);
	float GetOpenHeight() const { return OpenHeight; }
	/** True while the shutter is down or going down. */
	bool IsHeadingDown() const;
	/** Server: raises an operable shutter without a player (bots cannot press F). Does nothing if already going up. */
	void OpenByServer();
	/** Server: raise or lower a working shutter (the mannequin player's F through the body it views). False if not operable. */
	bool ToggleByServer();
	/** A working shutter players can raise and lower (layout Operable, or a plain shutter outside the layout). */
	bool IsOperableShutter() const { return LayoutState != EProjectShutterLayoutState::Sealed && LayoutState != EProjectShutterLayoutState::Raised && LayoutState != EProjectShutterLayoutState::Hanging; }
	
protected:
	virtual void BeginPlay() override;
	
	// 상호작용이 허용된 후 셔터의 이동 방향을 전환한다.
	virtual void HandleInteraction(APawn* Interactor) override;
	
	// 셔터가 시작 위치로부터 올라갈 높이
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float OpenHeight = 300.0f;
	
	// 셔터의 초당 이동 거리
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveSpeed = 150.0f;
	
	// 셔터가 목표 위치에 도착했다고 판단할 오차 범위
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Movement", meta = (ClampmMin = "0.001"))
	float ArrivalTolerance = 0.001f;

	/** Per-match layout role (Docs/FireShutterLayout.md). None keeps the old behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Layout")
	EProjectShutterLayoutRole LayoutRole = EProjectShutterLayoutRole::None;

	/** Escalator shutters: the same id as the escalator's level transition trigger. Every shutter around one opening shares it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Layout", meta = (EditCondition = "LayoutRole == EProjectShutterLayoutRole::Escalator"))
	FName EscalatorId;

	/** Optional look for a broken (sealed) shutter. Empty keeps the normal mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shutter|Layout")
	TObjectPtr<UStaticMesh> BrokenMesh;
	
private:
    UPROPERTY(ReplicatedUsing=OnRep_Motion) FProjectShutterMotion Motion;
    UFUNCTION() void OnRep_Motion();
    UPROPERTY(ReplicatedUsing=OnRep_LayoutState) EProjectShutterLayoutState LayoutState=EProjectShutterLayoutState::Default;
    /** Broken shutters hang crooked: roll of the panel in its own plane, degrees. */
    UPROPERTY(ReplicatedUsing=OnRep_LayoutState) float BrokenTilt=0.f;
    FRotator ClosedRotation=FRotator::ZeroRotator;
    UFUNCTION() void OnRep_LayoutState();
    void ShowLayoutState();
    /** Server: the navmesh cut under this shutter follows its state (broken: wall for all; working and down: wall for mannequins). */
    void RefreshNavCut();
    UPROPERTY(Transient) TObjectPtr<UNavModifierComponent> SealNavModifier;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> NormalMesh;
    FVector NormalScale=FVector::OneVector;
    /** Added to the closed/open spots so a tilted panel turns about its middle, whatever the model's pivot. */
    FVector TiltPivotFix=FVector::ZeroVector;
    double ServerNow()const;
    bool bMotionReady=false;
	// 셔터가 완전히 내려가 있을 때의 상대 위치
	FVector ClosedLocation = FVector::ZeroVector;
	
	// 셔터가 완전히 올라가 있을 때의 상대 위치
	FVector OpenLocation = FVector::ZeroVector;
	
	// 현재 셔터 위치를 0 ~ 1 범위로 나타낸 값
	float CurrentOpenAlpha = 0.0f;
	
	// 현재 이동 목표
	// 0.0 이면 닫기, 1.0 이면 열기
	float TargetOpenAlpha = 0.0f;
};
