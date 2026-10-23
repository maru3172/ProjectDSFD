#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "ProjectProject01Elevator.h"
#include "ProjectElevatorSynthComponent.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "PlayerInteractionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerStart.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectElevatorTest,"ProjectProject01.Elevator.Flow",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectElevatorTest::RunTest(const FString&)
{
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	if (!TestNotNull(TEXT("Isolated elevator world"),World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	World->SetGameInstance(NewObject<UGameInstance>());
	FURL URL; URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));
	if (!TestTrue(TEXT("Fixture creates authoritative game mode"),World->SetGameMode(URL))) return false;
	auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();
	if (!TestNotNull(TEXT("Multiplayer rules own activation"),Mode)) return false;
	Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;
	auto* PC=World->SpawnActor<AMultiplayTestPlayerController>();
	PC->SetAsLocalPlayerController();
	PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
	PC->PlayerCameraManager->InitializeFor(PC);
	auto* Other=World->SpawnActor<AMultiplayTestPlayerController>();
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Pawn=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(300,125,100)),Spawn);
	PC->Possess(Pawn); PC->AuthenticatedRole=TEXT("Survivor");
	auto* Eye=World->SpawnActor<ACameraActor>(); PC->SetViewTarget(Eye);
	PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
	FMultiplayTestServerPlayerRecord Record; Record.Role=TEXT("Survivor"); Record.UserId=TEXT("one");
	Record.SurvivorState=EMultiplayTestSurvivorState::Active; Record.SurvivorPawn=Pawn; Mode->MatchPlayerRecords.Add(PC,Record);
	Record.UserId=TEXT("two"); Record.SurvivorPawn.Reset(); Mode->MatchPlayerRecords.Add(Other,Record);
	auto& Partner=Mode->MatchPlayerRecords.FindChecked(Other);
	TestFalse(TEXT("Both active survivors cannot summon elevator"),Mode->CanUseEmergencyElevator());
	Partner.SurvivorState=EMultiplayTestSurvivorState::TransitionWaiting;
	TestFalse(TEXT("A survivor waiting at the other floor trigger NEVER unlocks elevator"),Mode->CanUseEmergencyElevator());
	Partner.SurvivorState=EMultiplayTestSurvivorState::Escaped;
	TestFalse(TEXT("Escape is not an elevator activation condition"),Mode->CanUseEmergencyElevator());
	Partner.SurvivorState=EMultiplayTestSurvivorState::Active; Partner.bDisconnected=true;
	TestFalse(TEXT("Reconnect grace is not forfeit"),Mode->CanUseEmergencyElevator());
	Partner.SurvivorState=EMultiplayTestSurvivorState::Eliminated; Partner.bDisconnected=false;
	TestTrue(TEXT("Authoritative death/capture unlocks elevator"),Mode->CanUseEmergencyElevator());
	Partner.bForfeited=true;
	TestTrue(TEXT("Confirmed survivor forfeit also unlocks elevator"),Mode->CanUseEmergencyElevator());
	Mode->DisconnectedMatchRecords.Add(TEXT("two"),Partner); Mode->MatchPlayerRecords.Remove(Other);
	TestTrue(TEXT("Departed confirmed forfeit still unlocks elevator"),Mode->CanUseEmergencyElevator());
	Mode->DisconnectedMatchRecords.FindChecked(TEXT("two")).bForfeited=false;
	Mode->DisconnectedMatchRecords.FindChecked(TEXT("two")).bDisconnected=true;
	TestFalse(TEXT("Disconnected eliminated record during grace cannot unlock on its own"),Mode->CanUseEmergencyElevator());
	Mode->DisconnectedMatchRecords.FindChecked(TEXT("two")).bForfeited=true;
	Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::Results;
	TestFalse(TEXT("Results never permit elevator calls"),Mode->CanUseEmergencyElevator());
	Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::InProgress;

	auto* Lift=World->SpawnActor<AProjectProject01Elevator>();
	Lift->DestinationLevel=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/MyProject/Level/MultiplayTest.MultiplayTest")));
	const FVector PrePlayHome=Lift->CabinRoot->GetRelativeLocation();
	Lift->OnRep_State();
	TestTrue(TEXT("Initial net state cannot animate before home transforms are captured"),Lift->CabinRoot->GetRelativeLocation().Equals(PrePlayHome));
	Lift->DispatchBeginPlay();
	TestEqual(TEXT("Existing placed elevator waits 12 metres below the landing by default"),Lift->CabinRoot->GetRelativeLocation().Z-Lift->CabinHome.Z,-1200.0);
	Lift->BelowLandingDepth=1800; Lift->UpdateVisuals();
	TestEqual(TEXT("Waiting depth is independently editable in centimetres"),Lift->CabinRoot->GetRelativeLocation().Z-Lift->CabinHome.Z,-1800.0);
	Lift->BelowLandingDepth=1200; Lift->UpdateVisuals();
	TestTrue(TEXT("BeginPlay captures the original cabin home, not a replicated animation offset"),Lift->CabinHome.Equals(PrePlayHome));
	TestFalse(TEXT("Entering cabin cannot activate inherited floor trigger"),Lift->bUseOverlapActivation);
	TestEqual(TEXT("Inherited auto-overlap volume stays disabled"),Lift->TransitionVolume->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Default call waiting duration is 20 seconds"),Lift->CallDelaySeconds,20.0f);
	TestTrue(TEXT("Elevator ticking is enabled, including dedicated servers"),Lift->IsActorTickEnabled() && Lift->PrimaryActorTick.bAllowTickOnDedicatedServer);
	TestTrue(TEXT("Default elevator is visible without custom assets"),Lift->CabinMesh->GetStaticMesh()!=nullptr);
	Lift->AdvanceServerState();
	TestTrue(TEXT("Valid destination and confirmed elimination enable outside button"),Lift->State.Phase==EProjectElevatorPhase::Ready);
	Pawn->SetActorLocationAndRotation(FVector(300,125,100),FRotator(0,180,0)); PC->SetControlRotation(FRotator(0,180,0));
	Eye->SetActorLocationAndRotation(FVector(300,125,100),FRotator(0,180,0));
	{
		FVector Start; FRotator Facing; PC->GetPlayerViewPoint(Start,Facing);
		FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorFixture),false,Pawn);
		World->LineTraceSingleByChannel(Hit,Start,Start+Facing.Vector()*500,ECC_Visibility,Params);
		AddInfo(FString::Printf(TEXT("Call view=%s facing=%s target=%s component=%s button=%s"),*Start.ToString(),*Facing.ToString(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Lift->CallButtonVolume->GetComponentLocation().ToString()));
	}
	TestTrue(TEXT("Only active survivor is an eligible elevator interactor"),Lift->IsEligibleInteractor(Pawn));
	if (!TestTrue(TEXT("Visibility ray hits the actual outside button"),Lift->TraceButton(Pawn)==Lift->CallButtonVolume)) return false;
	TestTrue(TEXT("Outside button can be interacted with"),Lift->CanInteract_Implementation(Pawn));
    {
        FVector BotMove,BotLook;
        TestFalse(TEXT("Bot objective adapter never changes human interactions"),Lift->GetBotObjective(Pawn,BotMove,BotLook));
        PC->bServerControlledBot=true;
        TestTrue(TEXT("An eligible survivor bot can find the outside call point"),Lift->GetBotObjective(Pawn,BotMove,BotLook));
        TestTrue(TEXT("Bot call target is the actual authored button"),BotLook.Equals(Lift->CallButtonVolume->GetComponentLocation()));
        const auto SavedLiftState=Lift->State;
        Lift->State.bArrivalTrip=true;
        TestFalse(TEXT("Arrival elevator cannot trap a bot in a second departure loop"),Lift->GetBotObjective(Pawn,BotMove,BotLook));
        Lift->State=SavedLiftState;
        PC->bServerControlledBot=false;
    }
	auto* Interaction=Pawn->FindComponentByClass<UPlayerInteractionComponent>();
	if (!TestNotNull(TEXT("Existing interaction component routes server RPC"),Interaction)) return false;
	TestTrue(TEXT("Existing focus/UI targeting recognizes the native elevator button"),Interaction->FindInteractableActor(false)==Lift);
	const auto CheckClientTargeting=[&](const TCHAR* Context)
	{
		const FString SavedRole=PC->AuthenticatedRole;
		const ENetRole SavedNetRole=Lift->GetLocalRole();
		const auto SavedState=PC->SurvivorState;
		const auto SavedPhase=Lift->State.Phase;
		ON_SCOPE_EXIT { Lift->SetRole(SavedNetRole); PC->AuthenticatedRole=SavedRole; PC->SurvivorState=SavedState; };
		PC->AuthenticatedRole.Empty();
		Lift->SetRole(ROLE_SimulatedProxy);
		TestTrue(FString::Printf(TEXT("%s: client targeting does not require server-only role"),Context),Lift->CanInteract_Implementation(Pawn));
		TestTrue(FString::Printf(TEXT("%s: client focus recognizes replicated active survivor"),Context),Interaction->FindInteractableActor(false)==Lift);
		TestFalse(FString::Printf(TEXT("%s: client cannot change authoritative phase locally"),Context),
			Lift->TryInteractFromView(Pawn,Eye->GetActorLocation(),Eye->GetActorForwardVector(),300,ECC_Visibility));
		for (const auto InvalidState : {EMultiplayTestSurvivorState::NotApplicable, EMultiplayTestSurvivorState::Eliminated,
			EMultiplayTestSurvivorState::TransitionWaiting, EMultiplayTestSurvivorState::Escaped})
		{
			PC->SurvivorState=InvalidState;
			TestFalse(FString::Printf(TEXT("%s: inactive client state %d cannot interact"),Context,static_cast<int32>(InvalidState)),Lift->CanInteract_Implementation(Pawn));
		}
		PC->SurvivorState=SavedState;
		Lift->SetRole(ROLE_Authority);
		TestFalse(FString::Printf(TEXT("%s: server still rejects missing authenticated role"),Context),Lift->CanInteract_Implementation(Pawn));
		Lift->Interact_Implementation(Pawn);
		TestTrue(FString::Printf(TEXT("%s: unauthorized server interaction cannot change phase"),Context),Lift->State.Phase==SavedPhase);
#if WITH_EDITOR
		{
			const auto PreviousWorldType=World->WorldType;
			const auto SavedRecord=Mode->MatchPlayerRecords.FindChecked(PC);
			const auto SavedLiftState=Lift->State;
			const auto SavedPassenger=Lift->Passenger;
			const double SavedRequestTime=Interaction->LastElevatorRequestSeconds;
			ON_SCOPE_EXIT
			{
				World->WorldType=PreviousWorldType; Mode->MatchPlayerRecords.Add(PC,SavedRecord);
				Lift->State=SavedLiftState; Lift->Passenger=SavedPassenger; Lift->OnRep_State();
				Interaction->LastElevatorRequestSeconds=SavedRequestTime;
			};
			World->WorldType=EWorldType::PIE;
			TestTrue(FString::Printf(TEXT("%s: ticket-free PIE uses the registered server role"),Context),Lift->CanInteract_Implementation(Pawn));
			Mode->MatchPlayerRecords.FindChecked(PC).SurvivorPawn.Reset();
			TestTrue(TEXT("PIE may interact before the first Pawn snapshot when server possession matches"),Lift->CanInteract_Implementation(Pawn));
			Mode->MatchPlayerRecords.Add(PC,SavedRecord);
			Mode->MatchPlayerRecords.FindChecked(PC).Role=TEXT("Mannequin");
			TestFalse(TEXT("PIE cannot treat a registered mannequin as a survivor"),Lift->CanInteract_Implementation(Pawn));
			Mode->MatchPlayerRecords.Add(PC,SavedRecord);
			Mode->MatchPlayerRecords.FindChecked(PC).SurvivorState=EMultiplayTestSurvivorState::Eliminated;
			TestFalse(TEXT("PIE cannot use an eliminated match record"),Lift->CanInteract_Implementation(Pawn));
			Mode->MatchPlayerRecords.Add(PC,SavedRecord);
			Mode->MatchPlayerRecords.FindChecked(PC).bDisconnected=true;
			TestFalse(TEXT("PIE cannot use a disconnected match record"),Lift->CanInteract_Implementation(Pawn));
			Mode->MatchPlayerRecords.Remove(PC);
			TestFalse(TEXT("PIE still rejects an unregistered active pawn"),Lift->CanInteract_Implementation(Pawn));
			Mode->MatchPlayerRecords.Add(PC,SavedRecord);
			PC->AuthenticatedRole=TEXT("Mannequin");
			TestFalse(TEXT("PIE never overrides an explicit conflicting authenticated role"),Lift->CanInteract_Implementation(Pawn));
			PC->AuthenticatedRole.Empty(); Interaction->LastElevatorRequestSeconds=-1;
			Interaction->ServerInteractElevator_Implementation(Lift,Eye->GetActorLocation(),Eye->GetActorForwardVector());
			TestTrue(FString::Printf(TEXT("%s: ticket-free PIE RPC starts the requested operation"),Context),
				Lift->State.Phase==(SavedPhase==EProjectElevatorPhase::Ready ? EProjectElevatorPhase::CallDelay : EProjectElevatorPhase::Closing));
		}
#endif
	};
	CheckClientTargeting(TEXT("Outside button"));
	const FVector OutsideClick=Eye->GetActorLocation();
	const FVector OutsideDirection=Eye->GetActorForwardVector();
	TestFalse(TEXT("Click cannot use a remote camera"),Lift->TryInteractFromView(Pawn,OutsideClick+FVector(0,0,1000),OutsideDirection,300,ECC_Visibility));
	TestFalse(TEXT("Click requires a normalized direction"),Lift->TryInteractFromView(Pawn,OutsideClick,FVector::ZeroVector,300,ECC_Visibility));
	TestFalse(TEXT("Click cannot bypass interaction range"),Lift->TryInteractFromView(Pawn,OutsideClick,OutsideDirection,10,ECC_Visibility));
	// Put an occluder only between the pawn's eye and the supplied camera origin.
	auto* Occluder=World->SpawnActor<AActor>();
	auto* OcclusionBox=NewObject<UBoxComponent>(Occluder);
	Occluder->SetRootComponent(OcclusionBox); OcclusionBox->RegisterComponent();
	OcclusionBox->SetBoxExtent(FVector(5,5,5)); OcclusionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OcclusionBox->SetCollisionResponseToAllChannels(ECR_Block);
	Occluder->SetActorLocation((Pawn->GetPawnViewLocation()+OutsideClick)*0.5);
	TestFalse(TEXT("Click camera cannot be placed across a blocking surface"),Lift->TryInteractFromView(Pawn,OutsideClick,OutsideDirection,300,ECC_Visibility));
	// The fixture has no engine world context. Let DestroyWorld own final actor cleanup.
	OcclusionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PC->AuthenticatedRole.Empty();
	Interaction->ServerInteractElevator_Implementation(Lift,OutsideClick,OutsideDirection);
	TestTrue(TEXT("Server RPC rejects an active pawn without an authenticated survivor claim"),Lift->State.Phase==EProjectElevatorPhase::Ready);
	PC->AuthenticatedRole=TEXT("Survivor"); Interaction->LastElevatorRequestSeconds=-1;
	Eye->SetActorRotation(FRotator(0,90,0));
	TestFalse(TEXT("Reproduced: stale remote camera rejects the real button click"),Lift->CanInteract_Implementation(Pawn));
	Interaction->ServerInteractElevator_Implementation(Lift,OutsideClick,OutsideDirection);
	Eye->SetActorRotation(FRotator(0,180,0));
	if (!TestTrue(TEXT("Validated server interaction starts waiting"),Lift->State.Phase==EProjectElevatorPhase::CallDelay)) return false;
	TestTrue(TEXT("Waiting already emits motor sound"),Lift->bMotorPlaying && Lift->MotorGainForPhase()>0);
	Lift->State.StartedAt=Lift->ServerNow()-19.9; Lift->Tick(0.1f);
	TestTrue(TEXT("Cabin does not rise before 20 seconds"),Lift->State.Phase==EProjectElevatorPhase::CallDelay);
	Lift->State.StartedAt=Lift->ServerNow()-20; Lift->Tick(0.1f);
	TestTrue(TEXT("Twenty second wait advances to rising"),Lift->State.Phase==EProjectElevatorPhase::Rising);
	const float InitialMotor=Lift->MotorGainForPhase();
	Lift->State.StartedAt=Lift->ServerNow()-Lift->RiseSeconds*0.5f; Lift->UpdateVisuals();
	const FVector HalfRiseOffset(Lift->BelowLandingOffset.X,Lift->BelowLandingOffset.Y,-Lift->BelowLandingDepth);
	TestTrue(TEXT("Cabin interpolates to halfway above the shaft"),Lift->CabinRoot->GetRelativeLocation().Equals(Lift->CabinHome+HalfRiseOffset*0.5f,0.1f));
	TestTrue(TEXT("Rising motor gently settles instead of cutting off"),Lift->MotorGainForPhase()>0 && Lift->MotorGainForPhase()<InitialMotor);
	Lift->State.StartedAt=Lift->ServerNow()-Lift->RiseSeconds*0.99f;
	TestTrue(TEXT("Motor is almost quiet before arrival bell"),Lift->MotorGainForPhase()<0.001f);
	Lift->State.StartedAt=Lift->ServerNow()-Lift->RiseSeconds; Lift->AdvanceServerState();
	TestTrue(TEXT("Arrival changes to bell and opening"),Lift->State.Phase==EProjectElevatorPhase::Opening && !Lift->bMotorPlaying);
	Lift->State.StartedAt=Lift->ServerNow()-Lift->DoorSeconds; Lift->AdvanceServerState(); Lift->UpdateVisuals();
	TestTrue(TEXT("Doors finish opening"),Lift->State.Phase==EProjectElevatorPhase::Open);
	TestTrue(TEXT("Both door meshes reach configured offsets"),Lift->LeftDoor->GetRelativeLocation().Equals(Lift->LeftDoorHome+Lift->LeftDoorOpenOffset) && Lift->RightDoor->GetRelativeLocation().Equals(Lift->RightDoorHome+Lift->RightDoorOpenOffset));
	Pawn->SetActorLocationAndRotation(FVector(0,125,100),FRotator::ZeroRotator); PC->SetControlRotation(FRotator::ZeroRotator);
	Eye->SetActorLocationAndRotation(FVector(0,125,100),FRotator::ZeroRotator);
	TestTrue(TEXT("Passenger must be physically inside cabin"),Lift->IsInsideCabin(Pawn));
	TestTrue(TEXT("Inside button trace is distinct from outside button"),Lift->TraceButton(Pawn)==Lift->InsideButtonVolume);
	CheckClientTargeting(TEXT("Inside button"));
	const FVector InsideClick=Eye->GetActorLocation();
	const FVector InsideDirection=Eye->GetActorForwardVector();
	Eye->SetActorRotation(FRotator(0,90,0));
	TestFalse(TEXT("Stale server camera also misses the inside button"),Lift->CanInteract_Implementation(Pawn));
	Interaction->LastElevatorRequestSeconds=-1;
	Interaction->ServerInteractElevator_Implementation(Lift,InsideClick,InsideDirection);
	Eye->SetActorRotation(FRotator::ZeroRotator);
	TestTrue(TEXT("Inside button starts closing, not immediate map travel"),Lift->State.Phase==EProjectElevatorPhase::Closing && !Lift->HasStartedTransition());
	Lift->Interact_Implementation(Pawn);
	TestTrue(TEXT("Repeated press cannot restart closing timer"),Lift->PhaseElapsed()==0 && Lift->State.Phase==EProjectElevatorPhase::Closing);
	const FVector DoorBeforeReopen=Lift->LeftDoor->GetRelativeLocation();
	Pawn->SetActorLocation(FVector(400,0,100)); Eye->SetActorLocationAndRotation(FVector(400,0,100),FRotator::ZeroRotator); Lift->AdvanceServerState();
	TestTrue(TEXT("Leaving during closing safely reopens without travel"),Lift->State.Phase==EProjectElevatorPhase::Opening && !Lift->HasStartedTransition());
	TestTrue(TEXT("Reopening never snaps a fully open door shut first"),Lift->LeftDoor->GetRelativeLocation().Equals(DoorBeforeReopen));
	PC->AuthenticatedRole=TEXT("Mannequin"); TestFalse(TEXT("Mannequin cannot use survivor elevator"),Lift->IsEligibleInteractor(Pawn));
	PC->AuthenticatedRole=TEXT("Survivor"); PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Eliminated);
	TestFalse(TEXT("Eliminated spectator cannot press elevator buttons"),Lift->IsEligibleInteractor(Pawn));
	PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
	Lift->SetPhase(EProjectElevatorPhase::Ready); Interaction->LastElevatorRequestSeconds=-1;
	Interaction->ServerInteractElevator_Implementation(Lift,Eye->GetActorLocation(),Eye->GetActorForwardVector());
	TestTrue(TEXT("Spoofing target without button line-of-sight has no effect"),Lift->State.Phase==EProjectElevatorPhase::Ready);

	auto* Arrival=World->SpawnActor<AProjectProject01Elevator>(); Arrival->ArrivalId=TEXT("UniqueArrival"); Arrival->bArrivalOnly=true;
	Arrival->SetActorLocation(FVector(2000,0,0));
	Arrival->DispatchBeginPlay();
	AActor* Start=AProjectProject01Elevator::FindArrivalStart(World,TEXT("?ElevatorArrival=UniqueArrival"));
	TestNotNull(TEXT("Destination provides a player start before pawn creation"),Start);
	TestTrue(TEXT("Authoritative spawn option activates arrival even without a World URL option"),Arrival->State.Phase==EProjectElevatorPhase::WaitingForArrival);
	if (Start) TestTrue(TEXT("Player start is the exact cabin interior transform"),Start->GetActorTransform().Equals(Arrival->ArrivalSpawn->GetComponentTransform()));
	TestTrue(TEXT("Repeated spawn selection reuses the same start"),Start==AProjectProject01Elevator::FindArrivalStart(World,TEXT("?ElevatorArrival=UniqueArrival")));
	TestNull(TEXT("Ordinary travel with empty arrival option never selects elevator"),AProjectProject01Elevator::FindArrivalStart(World,TEXT("?ElevatorArrival=")));
	Arrival->SetPhase(EProjectElevatorPhase::WaitingForArrival); Arrival->AdvanceServerState();
	TestTrue(TEXT("Destination waits for authoritative match readiness before motor/doors"),Arrival->State.Phase==EProjectElevatorPhase::ArrivalRide);
	Pawn->SetActorLocation(Arrival->ArrivalSpawn->GetComponentLocation());
	Arrival->AdvanceServerState();
	Arrival->State.StartedAt=Arrival->ServerNow()-Arrival->ArrivalRideSeconds; Arrival->AdvanceServerState();
	TestTrue(TEXT("Destination ride ends with bell/opening"),Arrival->State.Phase==EProjectElevatorPhase::Opening);
	TestFalse(TEXT("Arrival-only cabin cannot accidentally depart again"),Arrival->CanInteract_Implementation(Pawn));
	Arrival->State.StartedAt=Arrival->ServerNow()-Arrival->DoorSeconds; Arrival->AdvanceServerState();
	TestTrue(TEXT("Arrival doors stay open while the survivor is inside"),Arrival->State.Phase==EProjectElevatorPhase::Open);
	auto* SecondPawn=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(Arrival->ArrivalSpawn->GetComponentLocation()+FVector(0,30,0)),Spawn);
	Other->Possess(SecondPawn); Other->AuthenticatedRole=TEXT("Survivor");
	Other->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
	FMultiplayTestServerPlayerRecord ArrivalPartner; ArrivalPartner.UserId=TEXT("arrival-two"); ArrivalPartner.Role=TEXT("Survivor"); ArrivalPartner.SurvivorState=EMultiplayTestSurvivorState::Active; ArrivalPartner.SurvivorPawn=SecondPawn;
	Mode->MatchPlayerRecords.Add(Other,ArrivalPartner); Arrival->AdvanceServerState();
	Pawn->SetActorLocation(Arrival->GetActorLocation()+FVector(400,0,100)); Arrival->AdvanceServerState();
	TestTrue(TEXT("One survivor leaving cannot close on the other survivor"),Arrival->State.Phase==EProjectElevatorPhase::Open);
	SecondPawn->SetActorLocation(Arrival->GetActorLocation()+FVector(500,0,100));
	Pawn->SetActorLocation(Arrival->GetActorLocation()+FVector(200,0,100)); Arrival->AdvanceServerState();
	TestTrue(TEXT("Doorway clearance includes the survivor body, not just its centre"),Arrival->State.Phase==EProjectElevatorPhase::Open);
	Mode->MatchPlayerRecords.FindChecked(Other).SurvivorState=EMultiplayTestSurvivorState::Eliminated;
	Other->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Eliminated);
	SecondPawn->SetActorLocation(Arrival->ArrivalSpawn->GetComponentLocation());
	const double EarlierDing=Arrival->LastDingAt; World->TimeSeconds+=1;
	Pawn->SetActorLocation(Arrival->GetActorLocation()+FVector(400,0,100)); Arrival->AdvanceServerState();
	TestTrue(TEXT("After disembark a dead observer is ignored and arrival doors close with a ding"),Arrival->State.Phase==EProjectElevatorPhase::ArrivalClosing && Arrival->LastDingAt>EarlierDing && !Arrival->bMotorPlaying);
	Arrival->State.StartedAt=Arrival->ServerNow()-Arrival->CloseDoorSeconds*0.5f;
	const float PartlyClosed=Arrival->DoorFraction();
	Pawn->SetActorLocation(Arrival->ArrivalSpawn->GetComponentLocation()); Arrival->AdvanceServerState();
	TestTrue(TEXT("Re-entering during closure safely reopens without snapping"),Arrival->State.Phase==EProjectElevatorPhase::Opening && FMath::IsNearlyEqual(Arrival->State.DoorStart,PartlyClosed));
	Arrival->State.StartedAt=Arrival->ServerNow()-Arrival->DoorSeconds; Arrival->AdvanceServerState();
	Pawn->SetActorLocation(Arrival->GetActorLocation()+FVector(400,0,100)); Arrival->AdvanceServerState();
	Arrival->State.StartedAt=Arrival->ServerNow()-Arrival->CloseDoorSeconds; Arrival->AdvanceServerState(); Arrival->UpdateVisuals();
	TestTrue(TEXT("Arrival finishes closed at the landing with no descent, map travel or motor"),Arrival->State.Phase==EProjectElevatorPhase::ArrivalClosed && Arrival->DoorFraction()==0 && !Arrival->bMotorPlaying && !Arrival->HasStartedTransition() && Arrival->CabinRoot->GetRelativeLocation().Equals(Arrival->CabinHome));
	Mode->MatchPlayerRecords.Remove(Other); SecondPawn->SetActorHiddenInGame(true); SecondPawn->SetActorEnableCollision(false);
	// Full departure timing: bell/slow close, a separate motor-only ride, then a longer full-screen fade.
	auto* MannequinClient=World->SpawnActor<AMultiplayTestPlayerController>(); MannequinClient->SetAsLocalPlayerController();
	MannequinClient->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>(); MannequinClient->PlayerCameraManager->InitializeFor(MannequinClient);
	FMultiplayTestServerPlayerRecord MannequinRecord; MannequinRecord.UserId=TEXT("mannequin"); MannequinRecord.Role=TEXT("Mannequin");
	Mode->MatchPlayerRecords.Add(MannequinClient,MannequinRecord);
	Lift->SetPhase(EProjectElevatorPhase::Open); Lift->UpdateVisuals();
	Pawn->SetActorLocationAndRotation(FVector(0,125,100),FRotator::ZeroRotator);
	Eye->SetActorLocationAndRotation(FVector(0,125,100),FRotator::ZeroRotator);
	Lift->Interact_Implementation(Pawn);
	Lift->State.StartedAt=Lift->ServerNow()-Lift->CloseDoorSeconds*0.5f; Lift->UpdateVisuals();
	TestTrue(TEXT("Closing doors interpolate rather than snapping shut"),Lift->DoorFraction()>0 && Lift->DoorFraction()<1);
	Lift->State.StartedAt=Lift->ServerNow()-Lift->CloseDoorSeconds; Lift->AdvanceServerState();
	TestTrue(TEXT("Closed doors start descent sound before any map transition"),Lift->State.Phase==EProjectElevatorPhase::Descending && Lift->bMotorPlaying && !Lift->HasStartedTransition());
	Lift->State.StartedAt=Lift->ServerNow()-Lift->DescentSoundSeconds*0.5f; Lift->AdvanceServerState();
	TestTrue(TEXT("Descent motor plays alone before fading the screen"),Lift->State.Phase==EProjectElevatorPhase::Descending && Lift->MotorGainForPhase()>0 && !Lift->HasStartedTransition());
	Lift->State.StartedAt=Lift->ServerNow()-Lift->DescentSoundSeconds; Lift->AdvanceServerState();
	TestTrue(TEXT("Fade/travel begins only after the descent sound interval"),Lift->State.Phase==EProjectElevatorPhase::Travelling && Lift->HasStartedTransition());
	TestTrue(TEXT("Even standalone multiplayer prepares both roles at the same travel commit"),MannequinClient->IsLevelTransitionCarryover() && PC->IsLevelTransitionCarryover());
	TestTrue(TEXT("Elevator waits until its longer fade completes before map travel"),Lift->GetTransitionFadeSeconds()==1.5f && Lift->GetTravelDelaySeconds()>Lift->GetTransitionFadeSeconds());
	TestFalse(TEXT("A separate half-second spectator fade cannot mask the elevator's slower fade"),Lift->ShouldFadeToWaitingSpectator());
	Lift->State.StartedAt=Lift->ServerNow()-Lift->GetTransitionFadeSeconds();
	TestTrue(TEXT("Descent motor fades away with the outgoing screen"),Lift->MotorGainForPhase()<0.001f);
	Lift->SetPhase(EProjectElevatorPhase::CallDelay); Mode->AuthoritativeMatchPhase=EMultiplayTestMatchPhase::Results; Lift->AdvanceServerState();
	TestTrue(TEXT("A finished match disables the elevator and its motor"),Lift->State.Phase==EProjectElevatorPhase::Disabled && !Lift->bMotorPlaying);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectElevatorAudioTest,"ProjectProject01.Elevator.Audio",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectElevatorAudioTest::RunTest(const FString&)
{
	auto* Synth=NewObject<UProjectElevatorSynthComponent>(); int32 Rate=8000; Synth->Init(Rate);
	TArray<float> Samples; Samples.SetNumZeroed(Rate*2);
	Synth->OnGenerateAudio(Samples.GetData(),Samples.Num());
	TestTrue(TEXT("Idle elevator emits silence"),Algo::AllOf(Samples,[](float V){ return V==0; }));
	Synth->MotorTarget=0.06f; Synth->OnGenerateAudio(Samples.GetData(),Samples.Num());
	TestTrue(TEXT("Waiting and moving motor is audible and finite"),Algo::AnyOf(Samples,[](float V){return FMath::Abs(V)>0.01f;}) && Algo::AllOf(Samples,[](float V){return FMath::IsFinite(V)&&FMath::Abs(V)<=0.9f;}));
	Synth->MotorTarget=0; Synth->OnGenerateAudio(Samples.GetData(),Samples.Num());
	TestTrue(TEXT("Motor comes to a smooth quiet stop"),FMath::Abs(Samples.Last())<0.0001f);
	Synth->bDingActive=true; Synth->DingFrame=0; Synth->OnGenerateAudio(Samples.GetData(),Samples.Num());
	TestTrue(TEXT("Ding has audible attack and bounded output"),Algo::AnyOf(Samples,[](float V){return FMath::Abs(V)>0.1f;}) && Algo::AllOf(Samples,[](float V){return FMath::IsFinite(V)&&FMath::Abs(V)<=0.9f;}));
	TestFalse(TEXT("Ding finishes instead of looping forever"),Synth->bDingActive);
	TestTrue(TEXT("Ding tail returns to silence"),FMath::Abs(Samples.Last())<0.0001f);
	return true;
}
#endif
