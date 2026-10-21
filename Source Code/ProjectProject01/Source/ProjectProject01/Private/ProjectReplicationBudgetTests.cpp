#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ProjectReplicationBudgetComponent.h"
#include "MannequinAICharacter.h"
#include "PlayerCharacter.h"
#include "AIController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReplicationBudgetTest,"ProjectProject01.Multiplayer.ReplicationBudget",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReplicationBudgetTest::RunTest(const FString&)
{
	using Budget=UProjectReplicationBudgetComponent;
	TestEqual(TEXT("Close rear threat retains full transport rate"),Budget::FrequencyForView(100,1000.*1000.,false,true),100.f);
	TestEqual(TEXT("Front hemisphere prefetches at full rate"),Budget::FrequencyForView(100,10000.*10000.,true,true),100.f);
	TestEqual(TEXT("Audible offscreen threat retains 20Hz positions"),Budget::FrequencyForView(100,4000.*4000.,false,true),20.f);
	TestEqual(TEXT("Distant offscreen movement uses 12Hz"),Budget::FrequencyForView(100,10000.*10000.,false,true),12.f);
	TestEqual(TEXT("Distant stationary body uses 6Hz, never zero"),Budget::FrequencyForView(100,10000.*10000.,false,false),6.f);
	TestEqual(TEXT("Does not raise an author's lower original rate"),Budget::FrequencyForView(4,4000.*4000.,false,true),4.f);
	TestTrue(TEXT("Budget evaluation is interval-based, not every frame"),Budget::StaticClass()->GetDefaultObject<Budget>()->PrimaryComponentTick.TickInterval>=.2f);
	const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	ON_SCOPE_EXIT { if(World)World->DestroyWorld(false); };
	if(!TestNotNull(TEXT("Budget fixture world"),World))return false;
	auto* Body=World->SpawnActor<AMannequinAICharacter>();Body->SetActorLocation(FVector(-10000,0,0));
	auto* AI=World->SpawnActor<AAIController>();AI->Possess(Body);
	auto* PC=World->SpawnActor<APlayerController>();World->AddController(PC);
	PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);
	PC->SetControlRotation(FRotator::ZeroRotator);PC->SetViewTarget(PC);PC->PlayerCameraManager->UpdateCamera(.016f);
	TestEqual(TEXT("Unselected stationary body behind the viewer is budgeted"),Budget::FrequencyForActor(*Body,100),6.f);
	TestTrue(TEXT("Budget never disables actor tick or AI possession"),Body->PrimaryActorTick.bCanEverTick && Body->GetController()==AI);
	PC->SetViewTarget(Body);
	TestEqual(TEXT("Selected mannequin or spectator target immediately has full budget"),Budget::FrequencyForActor(*Body,100),100.f);
	PC->SetViewTarget(PC);
	auto* Other=World->SpawnActor<APlayerController>();World->AddController(Other);
	Other->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();Other->PlayerCameraManager->InitializeFor(Other);
	auto* Observer=World->SpawnActor<AMannequinAICharacter>();Observer->SetActorLocation(FVector(-20000,0,0));
	Other->SetControlRotation(FRotator::ZeroRotator);Other->SetViewTarget(Observer);Other->PlayerCameraManager->UpdateCamera(.016f);
	TestEqual(TEXT("Any other player's view prevents shared transport throttling"),Budget::FrequencyForActor(*Body,100),100.f);
	TestNotNull(TEXT("Mannequin owns server-only transport budget component"),Body->FindComponentByClass<Budget>());
	auto* Component=Body->FindComponentByClass<Budget>();Component->BeginPlay();
	TestFalse(TEXT("Standalone play disables transport budget tick"),Component->IsComponentTickEnabled());
	auto* Survivor=World->SpawnActor<APlayerCharacter>();
	TestNotNull(TEXT("Survivor also owns the transport budget component"),Survivor->FindComponentByClass<Budget>());
	return true;
}
#endif
