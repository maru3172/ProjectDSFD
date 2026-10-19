#include "ProjectProject01Elevator.h"
#include "ProjectElevatorSynthComponent.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AProjectProject01Elevator::AProjectProject01Elevator()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true; bAlwaysRelevant = true; bUseOverlapActivation = false;
	DestinationArrivalId = TEXT("Elevator_1F");
	CabinRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CabinRoot")); CabinRoot->SetupAttachment(GetRootComponent());
	CabinRoot->SetMobility(EComponentMobility::Movable);
	CabinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CabinMesh")); CabinMesh->SetupAttachment(CabinRoot);
	CabinMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	LeftDoor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftDoor")); LeftDoor->SetupAttachment(CabinRoot);
	RightDoor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightDoor")); RightDoor->SetupAttachment(CabinRoot);
	LeftDoor->SetRelativeLocation(FVector(150,-75,100)); RightDoor->SetRelativeLocation(FVector(150,75,100));
	LeftDoor->SetCollisionProfileName(TEXT("BlockAllDynamic")); RightDoor->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	// Replaceable blockout: visible/collidable before a custom model is assigned.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CabinMesh->SetStaticMesh(Cube.Object); CabinMesh->SetRelativeScale3D(FVector(3,3,0.12f));
	for (auto* Door : {LeftDoor.Get(),RightDoor.Get()})
	{
		Door->SetStaticMesh(Cube.Object); Door->SetRelativeScale3D(FVector(0.12f,1.5f,2));
		Door->SetMobility(EComponentMobility::Movable);
	}
	const FName Names[] = {TEXT("BackWall"),TEXT("LeftWall"),TEXT("RightWall")};
	for (int32 I=0; I<3; ++I)
	{
		auto* Wall = CreateDefaultSubobject<UStaticMeshComponent>(Names[I]); Wall->SetupAttachment(CabinRoot);
		Wall->SetStaticMesh(Cube.Object); Wall->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Wall->SetRelativeLocation(I==0 ? FVector(-150,0,100) : FVector(0,I==1 ? -150 : 150,100));
		Wall->SetRelativeScale3D(I==0 ? FVector(0.1f,3,2) : FVector(3,0.1f,2)); CabinWalls.Add(Wall);
	}
	auto MakeButton = [this](FName Name,USceneComponent* Parent,FVector Position)
	{
		auto* Box = CreateDefaultSubobject<UBoxComponent>(Name); Box->SetupAttachment(Parent);
		Box->SetRelativeLocation(Position); Box->InitBoxExtent(FVector(12,12,20));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Box->SetGenerateOverlapEvents(false);
		return Box;
	};
	CallButtonVolume = MakeButton(TEXT("CallButtonVolume"),GetRootComponent(),FVector(200,125,100));
	InsideButtonVolume = MakeButton(TEXT("InsideButtonVolume"),CabinRoot,FVector(120,125,100));
	for (int32 I=0; I<2; ++I)
	{
		auto* ButtonMesh = CreateDefaultSubobject<UStaticMeshComponent>(I==0 ? TEXT("CallButtonMesh") : TEXT("InsideButtonMesh"));
		ButtonMesh->SetupAttachment(I==0 ? CallButtonVolume.Get() : InsideButtonVolume.Get());
		ButtonMesh->SetStaticMesh(Cube.Object); ButtonMesh->SetRelativeScale3D(FVector(0.16f));
		ButtonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	CabinVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("CabinVolume")); CabinVolume->SetupAttachment(CabinRoot);
	CabinVolume->SetRelativeLocation(FVector(0,0,100)); CabinVolume->InitBoxExtent(FVector(130,130,100));
	CabinVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision); CabinVolume->SetGenerateOverlapEvents(false);
	ArrivalSpawn = CreateDefaultSubobject<USceneComponent>(TEXT("ArrivalSpawn")); ArrivalSpawn->SetupAttachment(GetRootComponent());
	ArrivalSpawn->SetRelativeLocation(FVector(0,0,100));
	ElevatorAudio = CreateDefaultSubobject<UProjectElevatorSynthComponent>(TEXT("ElevatorAudio")); ElevatorAudio->SetupAttachment(CabinRoot);
}
void AProjectProject01Elevator::BeginPlay()
{
	Super::BeginPlay();
	CabinHome = CabinRoot->GetRelativeLocation(); LeftDoorHome = LeftDoor->GetRelativeLocation(); RightDoorHome = RightDoor->GetRelativeLocation();
	bPresentationReady = true;
	if (HasAuthority())
	{
		const auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();
		FString Requested=UGameplayStatics::ParseOption(Mode ? Mode->OptionsString : FString(),TEXT("ElevatorArrival"));
		if (Requested.IsEmpty()) Requested=UGameplayStatics::ParseOption(GetWorld()->URL.ToString(),TEXT("ElevatorArrival"));
		if ((!Requested.IsEmpty() && FName(*Requested)==ArrivalId) || State.Phase==EProjectElevatorPhase::WaitingForArrival)
			SetPhase(EProjectElevatorPhase::WaitingForArrival);
		else SetPhase(bArrivalOnly ? EProjectElevatorPhase::Disabled : EProjectElevatorPhase::Locked);
	}
	else OnRep_State();
	UpdateVisuals();
}
void AProjectProject01Elevator::EndPlay(const EEndPlayReason::Type Reason)
{
	ElevatorAudio->Stop();
	if (IsValid(ArrivalStart)) ArrivalStart->Destroy();
	Super::EndPlay(Reason);
}
void AProjectProject01Elevator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AProjectProject01Elevator,State);
}
double AProjectProject01Elevator::ServerNow() const
{
	const auto* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}
float AProjectProject01Elevator::PhaseElapsed() const { return FMath::Max(0.0,ServerNow()-State.StartedAt); }
float AProjectProject01Elevator::MotorGainForPhase() const
{
	if (State.Phase==EProjectElevatorPhase::CallDelay || State.Phase==EProjectElevatorPhase::Descending) return 0.5f;
	float Duration=GetTransitionFadeSeconds();
	if (State.Phase==EProjectElevatorPhase::Rising) Duration=FMath::Max(0.1f,RiseSeconds);
	if (State.Phase==EProjectElevatorPhase::ArrivalRide) Duration=FMath::Max(0.7f,ArrivalRideSeconds);
	const float Progress=FMath::Clamp(PhaseElapsed()/Duration,0.0f,1.0f);
	return 0.5f*(1-FMath::SmoothStep(0.0f,1.0f,Progress));
}
void AProjectProject01Elevator::SetPhase(EProjectElevatorPhase NewPhase)
{
	if (!HasAuthority()) return;
	State.DoorStart=DoorFraction(); // Reopening during closing must not snap the doors shut first.
	State.Phase = NewPhase; State.StartedAt = ServerNow(); ForceNetUpdate(); OnRep_State();
	UE_LOG(LogTemp, Log, TEXT("[Elevator] %s phase=%s time=%.2f"), *GetPathName(),
		*UEnum::GetValueAsString(NewPhase), State.StartedAt);
}
float AProjectProject01Elevator::DoorFraction() const
{
	if (State.Phase==EProjectElevatorPhase::Open) return 1;
	if (State.Phase==EProjectElevatorPhase::Disabled) return State.DoorStart;
	const float Duration=State.Phase==EProjectElevatorPhase::Closing ? CloseDoorSeconds : DoorSeconds;
	const float T=FMath::SmoothStep(0.0f,1.0f,FMath::Clamp(PhaseElapsed()/FMath::Max(0.1f,Duration),0.0f,1.0f));
	if (State.Phase==EProjectElevatorPhase::Opening) return FMath::Lerp(State.DoorStart,1.0f,T);
	if (State.Phase==EProjectElevatorPhase::Closing) return FMath::Lerp(State.DoorStart,0.0f,T);
	return 0;
}
void AProjectProject01Elevator::OnRep_State()
{
	// Initial replication can precede BeginPlay. Never capture an already animated transform as the home position.
	if (!bPresentationReady) return;
	const auto P = State.Phase;
	bMotorPlaying = P==EProjectElevatorPhase::CallDelay || P==EProjectElevatorPhase::Rising || P==EProjectElevatorPhase::Descending || P==EProjectElevatorPhase::Travelling || P==EProjectElevatorPhase::ArrivalRide;
	if (GetNetMode()!=NM_DedicatedServer)
	{
		LastMotorGain=MotorGainForPhase();
		ElevatorAudio->SetMotor(bMotorPlaying,LastMotorGain);
		if ((P==EProjectElevatorPhase::Opening || P==EProjectElevatorPhase::Closing) && PhaseElapsed()<0.5f)
		{
			ElevatorAudio->Ding(); LastDingAt = GetWorld()->GetTimeSeconds();
		}
		if (P==EProjectElevatorPhase::Disabled) ElevatorAudio->Stop();
	}
	UpdateVisuals();
}
void AProjectProject01Elevator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority()) AdvanceServerState();
	UpdateVisuals();
	if (GetNetMode()!=NM_DedicatedServer && bMotorPlaying)
	{
		const float Gain=MotorGainForPhase();
		if (FMath::Abs(Gain-LastMotorGain)>=0.01f || (Gain==0 && LastMotorGain!=0))
		{ ElevatorAudio->SetMotor(true,Gain); LastMotorGain=Gain; }
	}
	if (!bMotorPlaying && GetWorld()->GetTimeSeconds()-LastDingAt>1.8 && ElevatorAudio->IsPlaying()) ElevatorAudio->Stop();
}
void AProjectProject01Elevator::AdvanceServerState()
{
	const auto* Mode = GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();
	const auto P = State.Phase;
	if (P==EProjectElevatorPhase::WaitingForArrival)
	{
		if (Mode && Mode->IsAuthoritativeMatchInProgress()) SetPhase(EProjectElevatorPhase::ArrivalRide);
		else if (PhaseElapsed()>120) SetPhase(EProjectElevatorPhase::Disabled);
		return;
	}
	if (P==EProjectElevatorPhase::Disabled || P==EProjectElevatorPhase::Travelling) return;
	if (!Mode || !Mode->IsAuthoritativeMatchInProgress())
	{
		if (P!=EProjectElevatorPhase::Locked) SetPhase(EProjectElevatorPhase::Disabled);
		return;
	}
	const bool bAllowed = Mode->CanUseEmergencyElevator() && !bArrivalOnly;
	switch (P)
	{
	case EProjectElevatorPhase::Locked: if (bAllowed && !DestinationArrivalId.IsNone() && IsDestinationAvailable()) SetPhase(EProjectElevatorPhase::Ready); break;
	case EProjectElevatorPhase::Ready: if (!bAllowed) SetPhase(EProjectElevatorPhase::Locked); break;
	case EProjectElevatorPhase::CallDelay: if (PhaseElapsed()>=FMath::Clamp(CallDelaySeconds,0.0f,300.0f)) SetPhase(EProjectElevatorPhase::Rising); break;
	case EProjectElevatorPhase::Rising: if (PhaseElapsed()>=FMath::Clamp(RiseSeconds,0.1f,60.0f)) SetPhase(EProjectElevatorPhase::Opening); break;
	case EProjectElevatorPhase::Opening: if (PhaseElapsed()>=FMath::Clamp(DoorSeconds,0.1f,15.0f)) SetPhase(EProjectElevatorPhase::Open); break;
	case EProjectElevatorPhase::Closing:
		if (!IsEligibleInteractor(Passenger.Get()) || !IsInsideCabin(Passenger.Get()))
		{ Passenger.Reset(); SetPhase(EProjectElevatorPhase::Opening); break; }
		if (PhaseElapsed()>=FMath::Clamp(CloseDoorSeconds,0.1f,15.0f)) SetPhase(EProjectElevatorPhase::Descending);
		break;
	case EProjectElevatorPhase::Descending:
		if (!IsEligibleInteractor(Passenger.Get()) || !IsInsideCabin(Passenger.Get()))
		{ Passenger.Reset(); SetPhase(EProjectElevatorPhase::Opening); break; }
		if (PhaseElapsed()>=FMath::Clamp(DescentSoundSeconds,0.1f,30.0f))
		{
			SetPhase(EProjectElevatorPhase::Travelling);
			if (!RequestTransition(Passenger.Get()) || !HasStartedTransition()) RollbackTransition(TEXT("Elevator departure was not accepted"));
		}
		break;
	case EProjectElevatorPhase::ArrivalRide: if (PhaseElapsed()>=FMath::Clamp(ArrivalRideSeconds,0.7f,30.0f)) SetPhase(EProjectElevatorPhase::Opening); break;
	default: break;
	}
}
void AProjectProject01Elevator::UpdateVisuals()
{
	if (!bPresentationReady) return;
	const auto P = State.Phase;
	float Rise = 1;
	if (P==EProjectElevatorPhase::Locked || P==EProjectElevatorPhase::Ready || P==EProjectElevatorPhase::CallDelay) Rise=0;
	if (P==EProjectElevatorPhase::Rising) Rise=FMath::Clamp(PhaseElapsed()/FMath::Max(0.1f,RiseSeconds),0.0f,1.0f);
	CabinRoot->SetRelativeLocation(CabinHome+BelowLandingOffset*(1-FMath::SmoothStep(0.0f,1.0f,Rise)));
	const float SmoothDoor=DoorFraction();
	LeftDoor->SetRelativeLocation(LeftDoorHome+LeftDoorOpenOffset*SmoothDoor);
	RightDoor->SetRelativeLocation(RightDoorHome+RightDoorOpenOffset*SmoothDoor);
}
bool AProjectProject01Elevator::IsEligibleInteractor(const APawn* Pawn) const
{
	const auto* Survivor = Cast<APlayerCharacter>(Pawn);
	const auto* PC = Survivor ? Cast<AMultiplayTestPlayerController>(Survivor->GetController()) : nullptr;
	if (!IsValid(Survivor) || Survivor->IsGameOver() || Survivor->IsHidden() || !IsValid(PC) ||
		PC->GetSurvivorState()!=EMultiplayTestSurvivorState::Active) return false;
	// AuthenticatedRole is server-only. Clients use the owner-replicated survivor
	// state for targeting; the authoritative interaction still validates the claim.
	if (!HasAuthority() || PC->GetAuthenticatedRole().Equals(TEXT("Survivor"),ESearchCase::IgnoreCase)) return true;
#if WITH_EDITOR
	// Local PIE intentionally skips join tickets: RegisterMatchPlayer stores its role
	// in GameMode, not AuthenticatedRole. Never extend this exception to packaged games.
	if (PC->GetAuthenticatedRole().IsEmpty() && GetWorld()->WorldType==EWorldType::PIE)
	{
		const auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();
		return Mode && Mode->IsRegisteredActiveSurvivor(Survivor);
	}
#endif
	return false;
}
bool AProjectProject01Elevator::IsInsideCabin(const APawn* Pawn) const
{
	if (!IsValid(Pawn)) return false;
	const FVector Local = CabinVolume->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
	const FVector Extent = CabinVolume->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X)<=Extent.X && FMath::Abs(Local.Y)<=Extent.Y && FMath::Abs(Local.Z)<=Extent.Z;
}
UPrimitiveComponent* AProjectProject01Elevator::TraceButton(APawn* Pawn) const
{
	if (!IsValid(Pawn) || !IsValid(Pawn->GetController())) return nullptr;
	FVector Eye; FRotator Look; Pawn->GetController()->GetPlayerViewPoint(Eye,Look);
	if (HasAuthority() && FVector::DistSquared(Eye,Pawn->GetActorLocation())>FMath::Square(400.0f)) return nullptr;
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorButton),false,Pawn);
	if (GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Look.Vector()*500,ECC_Visibility,Params) && Hit.GetActor()==this)
		return Hit.GetComponent();
	return nullptr;
}
bool AProjectProject01Elevator::CanInteract_Implementation(APawn* Interactor) const
{
	return CanUseButton(Interactor,TraceButton(Interactor));
}
bool AProjectProject01Elevator::CanUseButton(APawn* Interactor, const UPrimitiveComponent* Button) const
{
	if (bArrivalOnly || !IsEligibleInteractor(Interactor)) return false;
	if (HasAuthority())
	{
		const auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();
		if (!Mode || !Mode->CanUseEmergencyElevator() || !IsDestinationAvailable()) return false;
	}
	return (State.Phase==EProjectElevatorPhase::Ready && Button==CallButtonVolume) ||
		(State.Phase==EProjectElevatorPhase::Open && Button==InsideButtonVolume && IsInsideCabin(Interactor));
}
bool AProjectProject01Elevator::TryInteractFromView(APawn* Interactor, const FVector& ViewOrigin,
	const FVector& ViewDirection, float Range, ECollisionChannel Channel)
{
	if (!HasAuthority()) return false;
	const auto Reject=[&](const TCHAR* Reason)
	{
		UE_LOG(LogTemp, Log, TEXT("[ElevatorInteraction] Rejected: target=%s owner=%s phase=%s reason=%s"),
			*GetNameSafe(this), *GetNameSafe(Interactor), *UEnum::GetValueAsString(State.Phase), Reason);
		return false;
	};
	if (!IsEligibleInteractor(Interactor))
	{
		const auto* Controller=IsValid(Interactor) ? Cast<AMultiplayTestPlayerController>(Interactor->GetController()) : nullptr;
		UE_LOG(LogTemp, Log, TEXT("[ElevatorInteraction] Eligibility: worldType=%d role='%s' survivorState=%d gameOver=%d hidden=%d"),
			static_cast<int32>(GetWorld()->WorldType), Controller ? *Controller->GetAuthenticatedRole() : TEXT("NoController"),
			Controller ? static_cast<int32>(Controller->GetSurvivorState()) : -1,
			IsValid(Cast<APlayerCharacter>(Interactor)) && Cast<APlayerCharacter>(Interactor)->IsGameOver(), IsValid(Interactor) && Interactor->IsHidden());
		return Reject(TEXT("RoleOrSurvivorState"));
	}
	if (ViewOrigin.ContainsNaN() || ViewDirection.ContainsNaN() || !FMath::IsFinite(Range) ||
		!FMath::IsNearlyEqual(ViewDirection.SizeSquared(),1.0,0.02)) return Reject(TEXT("InvalidView"));
	// Keep the existing camera-offset limit, and disallow moving the supplied camera through a wall.
	if (FVector::DistSquared(ViewOrigin,Interactor->GetActorLocation())>FMath::Square(400.0f)) return Reject(TEXT("CameraTooFar"));
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorClick),false,Interactor);
	if (GetWorld()->LineTraceSingleByChannel(Hit,Interactor->GetPawnViewLocation(),ViewOrigin,ECC_Visibility,Params))
		return Reject(TEXT("CameraOccluded"));
	if (!GetWorld()->LineTraceSingleByChannel(Hit,ViewOrigin,
		ViewOrigin+ViewDirection.GetSafeNormal()*FMath::Clamp(Range,0.0f,500.0f),Channel,Params) || Hit.GetActor()!=this)
		return Reject(TEXT("ButtonNotInLineOfSightOrRange"));
	if (!CanUseButton(Interactor,Hit.GetComponent())) return Reject(TEXT("ButtonOrMatchNotReady"));
	BeginButtonInteraction(Interactor);
	return true;
}
FText AProjectProject01Elevator::GetInteractionText_Implementation(APawn* Interactor) const
{
	return State.Phase==EProjectElevatorPhase::Ready ? NSLOCTEXT("Elevator","Call","엘리베이터 호출") :
		NSLOCTEXT("Elevator","Depart","1층으로 이동");
}
void AProjectProject01Elevator::Interact_Implementation(APawn* Interactor)
{
	if (!HasAuthority() || !CanInteract_Implementation(Interactor)) return;
	BeginButtonInteraction(Interactor);
}
void AProjectProject01Elevator::BeginButtonInteraction(APawn* Interactor)
{
	if (State.Phase==EProjectElevatorPhase::Ready) SetPhase(EProjectElevatorPhase::CallDelay);
	else { Passenger=Cast<APlayerCharacter>(Interactor); SetPhase(EProjectElevatorPhase::Closing); }
}
void AProjectProject01Elevator::RollbackTransition(const FString& Reason)
{
	Super::RollbackTransition(Reason); Passenger.Reset(); SetPhase(EProjectElevatorPhase::Opening);
}
AActor* AProjectProject01Elevator::FindArrivalStart(UWorld* World, const FString& Options)
{
	if (!IsValid(World) || World->GetNetMode()==NM_Client) return nullptr;
	const FString Requested=UGameplayStatics::ParseOption(Options,TEXT("ElevatorArrival"));
	if (Requested.IsEmpty()) return nullptr;
	AProjectProject01Elevator* Found=nullptr;
	for (TActorIterator<AProjectProject01Elevator> It(World);It;++It)
		if (It->ArrivalId==FName(*Requested))
		{
			if (Found) { UE_LOG(LogTemp,Error,TEXT("Duplicate elevator ArrivalId: %s"),*Requested); return nullptr; }
			Found=*It;
		}
	if (!Found) { UE_LOG(LogTemp,Error,TEXT("No elevator for arrival ID %s"),*Requested); return nullptr; }
	// Spawn selection already has the authoritative travel option, even if BeginPlay saw an incomplete World URL.
	if (Found->State.Phase==EProjectElevatorPhase::Disabled || Found->State.Phase==EProjectElevatorPhase::Locked)
		Found->SetPhase(EProjectElevatorPhase::WaitingForArrival);
	if (!IsValid(Found->ArrivalStart))
	{
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Found->ArrivalStart=World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(),Found->ArrivalSpawn->GetComponentTransform(),Params);
	}
	return Found->ArrivalStart;
}
