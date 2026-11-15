// Fill out your copyright notice in the Description page of Project Settings.


#include "FireShutterActor.h"
#include "ProjectGamepad.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Components/BoxComponent.h"
#include "MultiplayTestGameMode.h"
#include "PlayerCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "ProjectShutterNavigation.h"

AFireShutterActor::AFireShutterActor()
{
	// 셔터가 움직이는 동안 Tick 을 사용할 수 있도록 설정
	PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bAllowTickOnDedicatedServer=true;
    bReplicates=true;SetReplicateMovement(false);NetUpdateFrequency=5;
    ItemMesh->SetMobility(EComponentMobility::Movable);
	
	// 실제 이동이 시작되기 전까지는 Tick 을 실행하지 않는다.
	PrimaryActorTick.bStartWithTickEnabled = false;
	
	// 셔터 메시가 플레이어와 다른 물체를 막도록 충돌을 활성화한다.
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ItemMesh->SetCollisionProfileName(TEXT("BlockAll"));
	
	// UI 에 표시할 기본 상호작용 문구
	if (TargetOpenAlpha >= 0.5f)
	{
		// 셔터가 올라가 있으면? -> 내린다는 문구
		InteractionText = NSLOCTEXT("Interaction", "LowerFireShutter", "F 키: 방화 셔터 내리기");
	}
	else
	{
		// 셔터가 내려가 있으면? -> 올린다는 문구
		InteractionText = NSLOCTEXT("Interaction", "RaiseFireShutter", "F 키: 방화 셔터 올리기");
	}
}

void AFireShutterActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // Layout shutters (Docs/FireShutterLayout.md) are up or down per match, so the baked navmesh must not see them;
    // a broken one cuts paths at runtime with its own modifier instead. Shutters outside the layout keep the old behaviour.
    if(ItemMesh&&LayoutRole!=EProjectShutterLayoutRole::None)ItemMesh->SetCanEverAffectNavigation(false);
}

void AFireShutterActor::BeginPlay()
{
	Super::BeginPlay();
    ItemMesh->SetMobility(EComponentMobility::Movable);
    if(HasAuthority()&&GetNetMode()!=NM_Standalone)SetReplicates(true); // Refresh old placed Blueprint defaults too.
	
	// 에디터에서 배치한 셔터 메시의 위치를 닫힌 위치로 사용한다.
	ClosedLocation = ItemMesh->GetRelativeLocation();
	ClosedRotation = ItemMesh->GetRelativeRotation();
	
	// 닫힌 위치에서 OpenHeight 만큼 위쪽을 열린 위치로 사용한다.
	OpenLocation = ClosedLocation + FVector(0, 0, OpenHeight);
	
	CurrentOpenAlpha = 0.0f;
	TargetOpenAlpha = 0.0f;
	
	// 시작할 때는 닫힌 위치를 정확하게 적용한다.
	// ItemMesh 컴포넌트 위치를 ClosedLocation 위치로 변경한다.
	// 위치는 SceneRoot 컴포넌트를 기준으로 계산된다.
	ItemMesh->SetRelativeLocation(ClosedLocation);
	
	// 매 프레임 호출되던 Tick 을 중지
	SetActorTickEnabled(false);
    bMotionReady=true;
    if(GetNetMode()!=NM_Standalone)OnRep_Motion();
    if(LayoutState!=EProjectShutterLayoutState::Default)ShowLayoutState(); // Layout replicated before BeginPlay.
}

void AFireShutterActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
    if(GetNetMode()!=NM_Standalone)
    {
        if(!bMotionReady)return;
        const float AlphaSpeed=Motion.Height>UE_SMALL_NUMBER?FMath::Max(0.f,Motion.Speed)/Motion.Height:0.f;
        CurrentOpenAlpha=FMath::Clamp(FMath::FInterpConstantTo(Motion.Start,Motion.Target,float(FMath::Max(0.,ServerNow()-Motion.StartedAt)),AlphaSpeed),0.f,1.f);
        ItemMesh->SetRelativeLocation(ClosedLocation+FVector(0,0,FMath::Max(0.f,Motion.Height)*CurrentOpenAlpha));
        if(FMath::IsNearlyEqual(CurrentOpenAlpha,Motion.Target,ArrivalTolerance)||AlphaSpeed<=UE_SMALL_NUMBER)SetActorTickEnabled(false);
        return;
    }
	
	const float TotalDistance = FVector::Distance(ClosedLocation, OpenLocation);
	
	// 이동 거리가 없거나 속도가 0 이면 움직일 수 없으므로 Tick 을 중지한다.
	if (TotalDistance <= KINDA_SMALL_NUMBER || MoveSpeed <= KINDA_SMALL_NUMBER)
	{
		SetActorTickEnabled(false);
		return;
	}
	
	// cm/s 단위의 MoveSpeed 를 0 ~ 1 범위에서 사용할 Alpha 속도로 변환한다.
	const float AlphaSpeed = MoveSpeed / TotalDistance;
	
	// 현재 Alpha 를 목표 Alpha 방향으로 일정한 속도로 이동시킨다.
	// 목표가 바뀌면 현재 위치에서 즉시 반대 방향으로 진행한다.
	CurrentOpenAlpha = FMath::FInterpConstantTo(
		CurrentOpenAlpha,
		TargetOpenAlpha,
		DeltaTime,
		AlphaSpeed
		);
	
	// Clamp: 값을 범위 안으로 제한한다.
	// 계산 오차가 발생해도 이동 범위를 벗어나지 않게 제한한다.
	CurrentOpenAlpha = FMath::Clamp(CurrentOpenAlpha, 0.0f, 1.0f);
	
	// Lerp: 두 값 사이의 중간값이나 위치를 계산한다.
	// Alpha 가 0 이면 ClosedLocation
	// Alpha 가 1 이면 OpenLocation
	const FVector NewLocation = FMath::Lerp(ClosedLocation, OpenLocation, CurrentOpenAlpha);
	
	ItemMesh->SetRelativeLocation(NewLocation);
	
	// 목표에 도착했다면 위치를 정확하게 보정하고 Tick 을 중지한다.
	if (FMath::IsNearlyEqual(CurrentOpenAlpha, TargetOpenAlpha, ArrivalTolerance))
	{
		CurrentOpenAlpha = TargetOpenAlpha;
		
		const FVector FinalLocation = FMath::Lerp(ClosedLocation, OpenLocation, CurrentOpenAlpha);
		
		ItemMesh->SetRelativeLocation(FinalLocation);
		
		SetActorTickEnabled(false);
	}
}

void AFireShutterActor::HandleInteraction(APawn* Interator)
{
    if(GetNetMode()!=NM_Standalone)
    {
        if(!HasAuthority()||!CanInteract_Implementation(Interator))return;
        if(IsActorTickEnabled())Tick(0);
        Motion.Start=CurrentOpenAlpha;Motion.Target=Motion.Target>=.5f?0.f:1.f;Motion.Speed=FMath::Max(0.f,MoveSpeed);Motion.Height=FMath::Max(0.f,OpenHeight);Motion.StartedAt=ServerNow();
        ForceNetUpdate();OnRep_Motion();RefreshNavCut();return;
    }
	Super::HandleInteraction(Interator);
	
	// 현재 목표가 열린 상태라면 닫힌 상태로 변경하고, 닫힌 상태라면 열린 상태로 변경한다.
	// 이동 중에 다시 상호작용해도 목표값이 반전되므로 현재 위치부터 즉시 반대 방향으로 이동한다.
	TargetOpenAlpha = TargetOpenAlpha >= 0.5f ? 0.0f : 1.0f;
	RefreshNavCut();
	
	if (TargetOpenAlpha >= 0.5f)
	{
		// 현재 올라가는 중이므로 다음 행동은 내리기
		InteractionText = NSLOCTEXT("Interaction", "LowerFireShutter", "F 키: 방화 셔터 내리기");
	}
	else
	{
		// 현재 내려가는 중이므로 다음 행동은 올리기
		InteractionText = NSLOCTEXT("Interaction", "RaiseFireShutter", "F 키: 방화 셔터 올리기");
	}
	
	// 셔터 이동을 위해 Tick 을 활성화한다.
	SetActorTickEnabled(true);
}
double AFireShutterActor::ServerNow()const{const auto* State=GetWorld()->GetGameState();return State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}
void AFireShutterActor::OnRep_Motion()
{
    // The prompt follows the replicated target on every machine (server and clients alike).
    InteractionText=Motion.Target>=.5f?NSLOCTEXT("Interaction","LowerFireShutter","F 키: 방화 셔터 내리기"):NSLOCTEXT("Interaction","RaiseFireShutter","F 키: 방화 셔터 올리기");
    if(bMotionReady){SetActorTickEnabled(true);Tick(0);}
    // A shutter coming down or going up shakes the pad of a player close by (Docs/Gamepad.md 7). A layout snap does not move.
    if(bMotionReady&&GetNetMode()!=NM_DedicatedServer&&!FMath::IsNearlyEqual(Motion.Start,Motion.Target))
        if(const APlayerController* PC=GetWorld()->GetFirstPlayerController();PC&&PC->GetPawn()&&FVector::Dist(PC->GetPawn()->GetActorLocation(),GetActorLocation())<1500.f)
            if(auto* Pad=UProjectGamepadSubsystem::Get(this))Pad->Pulse(.45f,.5f,true);
}
void AFireShutterActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AFireShutterActor,Motion);DOREPLIFETIME(AFireShutterActor,LayoutState);DOREPLIFETIME(AFireShutterActor,BrokenTilt);}

void AFireShutterActor::ApplyLayout(const EProjectShutterLayoutState NewState,const float OpenAlpha,const float TiltDegrees)
{
    if(!HasAuthority())return;
    LayoutState=NewState;BrokenTilt=TiltDegrees;
    const float Alpha=FMath::Clamp(OpenAlpha,0.f,1.f);const bool bOpen=Alpha>=.5f;
    if(GetNetMode()==NM_Standalone)
    {
        CurrentOpenAlpha=TargetOpenAlpha=Alpha;
        ItemMesh->SetRelativeLocation(FMath::Lerp(ClosedLocation,OpenLocation,Alpha));
        InteractionText=bOpen?NSLOCTEXT("Interaction","LowerFireShutter","F 키: 방화 셔터 내리기"):NSLOCTEXT("Interaction","RaiseFireShutter","F 키: 방화 셔터 올리기");
        SetActorTickEnabled(false);
    }
    else
    {
        // Start == Target: every machine places it there at once (no motion, no pad shake).
        Motion.Start=Motion.Target=Alpha;Motion.Speed=FMath::Max(0.f,MoveSpeed);Motion.Height=FMath::Max(0.f,OpenHeight);Motion.StartedAt=ServerNow();
        ForceNetUpdate();OnRep_Motion();
    }
    ShowLayoutState();
    // Not in play this match (a compartment line that does not stand, an unused slot): the server stops checking it.
    // Going dormant still sends the pending state first, and a connection that joins later gets it once.
    if(GetNetMode()!=NM_Standalone)SetNetDormancy(LayoutState==EProjectShutterLayoutState::Raised?DORM_DormantAll:DORM_Awake);
}
bool AFireShutterActor::IsHeadingDown()const{return (GetNetMode()==NM_Standalone?TargetOpenAlpha:Motion.Target)<.5f;}
bool AFireShutterActor::ToggleByServer()
{
    if(!HasAuthority()||!IsOperableShutter())return false;
    if(GetNetMode()==NM_Standalone)
    {
        TargetOpenAlpha=TargetOpenAlpha>=.5f?0.f:1.f;
        InteractionText=TargetOpenAlpha>=.5f?NSLOCTEXT("Interaction","LowerFireShutter","F 키: 방화 셔터 내리기"):NSLOCTEXT("Interaction","RaiseFireShutter","F 키: 방화 셔터 올리기");
        SetActorTickEnabled(true);RefreshNavCut();return true;
    }
    if(!bMotionReady)return false;
    if(IsActorTickEnabled())Tick(0);
    Motion.Start=CurrentOpenAlpha;Motion.Target=Motion.Target>=.5f?0.f:1.f;Motion.Speed=FMath::Max(0.f,MoveSpeed);Motion.Height=FMath::Max(0.f,OpenHeight);Motion.StartedAt=ServerNow();
    ForceNetUpdate();OnRep_Motion();RefreshNavCut();return true;
}

void AFireShutterActor::OpenByServer()
{
    if(!HasAuthority()||!bMotionReady||!IsHeadingDown()||LayoutState==EProjectShutterLayoutState::Sealed||LayoutState==EProjectShutterLayoutState::Raised||LayoutState==EProjectShutterLayoutState::Hanging)return;
    if(GetNetMode()==NM_Standalone)
    {
        TargetOpenAlpha=1.f;InteractionText=NSLOCTEXT("Interaction","LowerFireShutter","F 키: 방화 셔터 내리기");SetActorTickEnabled(true);RefreshNavCut();return;
    }
    if(IsActorTickEnabled())Tick(0);
    Motion.Start=CurrentOpenAlpha;Motion.Target=1.f;Motion.Speed=FMath::Max(0.f,MoveSpeed);Motion.Height=FMath::Max(0.f,OpenHeight);Motion.StartedAt=ServerNow();
    ForceNetUpdate();OnRep_Motion();RefreshNavCut();
}
void AFireShutterActor::OnRep_LayoutState(){ShowLayoutState();}
void AFireShutterActor::ShowLayoutState()
{
    // Tucked away this match: not drawn, no collision (it sits above the ceiling, out of play). Server and clients alike.
    const bool bAway=LayoutState==EProjectShutterLayoutState::Raised;
    SetActorHiddenInGame(bAway);SetActorEnableCollision(!bAway);
    if(bMotionReady)ItemMesh->SetRelativeRotation(ClosedRotation+FRotator(0.f,0.f,BrokenTilt));
    const bool bSealed=LayoutState==EProjectShutterLayoutState::Sealed||LayoutState==EProjectShutterLayoutState::Hanging; // broken look
    if(BrokenMesh)
    {
        if(!NormalMesh)NormalMesh=ItemMesh->GetStaticMesh();
        ItemMesh->SetStaticMesh(bSealed?BrokenMesh.Get():NormalMesh.Get());
    }
    RefreshNavCut();
}
void AFireShutterActor::RefreshNavCut()
{
    // Pathfinding lives on the server (needs a navmesh that takes runtime modifiers: Dynamic Modifiers Only).
    if(GetNetMode()==NM_Client)return;
    TSubclassOf<UNavArea> Area=nullptr;
    if(LayoutState==EProjectShutterLayoutState::Sealed)Area=UNavArea_Null::StaticClass();
    else if(LayoutState==EProjectShutterLayoutState::Operable&&IsHeadingDown())Area=UNavArea_ProjectShutterDoor::StaticClass();
    if(!Area){if(SealNavModifier){SealNavModifier->DestroyComponent();SealNavModifier=nullptr;}return;}
    if(!SealNavModifier)
    {
        SealNavModifier=NewObject<UNavModifierComponent>(this,TEXT("SealNavModifier"));
        // Layout shutters do not affect navigation through their mesh, so the modifier uses this box (shutter-sized, actor-rotated).
        SealNavModifier->FailsafeExtent=FVector(40.f,150.f*FMath::Abs(GetActorScale3D().Y),220.f);
        SealNavModifier->SetAreaClass(Area);
        SealNavModifier->RegisterComponent();
    }
    else if(SealNavModifier->AreaClass!=Area)SealNavModifier->SetAreaClass(Area);
}
bool AFireShutterActor::CanInteract_Implementation(APawn* Interactor)const
{
    // Broken shutters and unused corridor slots are not operable (Docs/FireShutterLayout.md).
    if(LayoutState==EProjectShutterLayoutState::Sealed||LayoutState==EProjectShutterLayoutState::Raised||LayoutState==EProjectShutterLayoutState::Hanging)return false;
    if(GetNetMode()==NM_Standalone)return Super::CanInteract_Implementation(Interactor);
    const auto* Survivor=Cast<APlayerCharacter>(Interactor);const auto* PC=Survivor?Cast<AMultiplayTestPlayerController>(Survivor->GetController()):nullptr;
    if(!Super::CanInteract_Implementation(Interactor)||!Survivor||Survivor->IsGameOver()||!PC||PC->IsServerControlledBot()||PC->GetSurvivorState()!=EMultiplayTestSurvivorState::Active)return false;
    if(!HasAuthority())return true;
    const auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();return Mode&&Mode->IsAuthoritativeMatchInProgress()&&Mode->IsRegisteredActiveSurvivor(Survivor);
}
bool AFireShutterActor::TryInteractFromView(APawn* Interactor,const FVector& Origin,const FVector& Direction,float Range,ECollisionChannel Channel)
{
    if(!HasAuthority()||!CanInteract_Implementation(Interactor)||Origin.ContainsNaN()||Direction.ContainsNaN()||!FMath::IsFinite(Range)||!FMath::IsNearlyEqual(Direction.SizeSquared(),1.,.02)||FVector::DistSquared(Origin,Interactor->GetActorLocation())>FMath::Square(400.f))return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ShutterClick),false,Interactor);FHitResult Hit;
    if(GetWorld()->LineTraceSingleByChannel(Hit,Interactor->GetPawnViewLocation(),Origin,ECC_Visibility,Query))return false;
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*FMath::Clamp(Range,0.f,500.f),Channel,Query)||Hit.GetActor()!=this)return false;
    HandleInteraction(Interactor);return true;
}
