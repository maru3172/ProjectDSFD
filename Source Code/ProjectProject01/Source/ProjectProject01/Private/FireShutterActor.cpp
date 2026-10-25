// Fill out your copyright notice in the Description page of Project Settings.


#include "FireShutterActor.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Components/BoxComponent.h"
#include "MultiplayTestGameMode.h"
#include "PlayerCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

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

void AFireShutterActor::BeginPlay()
{
	Super::BeginPlay();
    ItemMesh->SetMobility(EComponentMobility::Movable);
    if(HasAuthority()&&GetNetMode()!=NM_Standalone)SetReplicates(true); // Refresh old placed Blueprint defaults too.
	
	// 에디터에서 배치한 셔터 메시의 위치를 닫힌 위치로 사용한다.
	ClosedLocation = ItemMesh->GetRelativeLocation();
	
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
        ForceNetUpdate();OnRep_Motion();return;
    }
	Super::HandleInteraction(Interator);
	
	// 현재 목표가 열린 상태라면 닫힌 상태로 변경하고, 닫힌 상태라면 열린 상태로 변경한다.
	// 이동 중에 다시 상호작용해도 목표값이 반전되므로 현재 위치부터 즉시 반대 방향으로 이동한다.
	TargetOpenAlpha = TargetOpenAlpha >= 0.5f ? 0.0f : 1.0f;
	
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
void AFireShutterActor::OnRep_Motion(){if(bMotionReady){SetActorTickEnabled(true);Tick(0);}}
void AFireShutterActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AFireShutterActor,Motion);}
bool AFireShutterActor::CanInteract_Implementation(APawn* Interactor)const
{
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
