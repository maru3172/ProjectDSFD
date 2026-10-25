#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectInstantReplay.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectLoadingScreenSubsystem.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "PlayInEditorDataTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

// Exercise real pose capture, preview-scene rendering and teardown in a temporary PIE
// world. No real account, backend, replay file, map save or persistent setting is used.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FProjectDeathCameraPlayTest,"ProjectProject01.Editor.DeathCamera",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
void FProjectDeathCameraPlayTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    for(const TCHAR* Case:{TEXT("Complete"),TEXT("Logout"),TEXT("Travel")}){Names.Add(Case);Commands.Add(Case);}
}
bool FProjectDeathCameraPlayTest::RunTest(const FString& Case)
{
    if(!GEditor||GEditor->IsPlaySessionInProgress()){AddError(TEXT("Stop PIE before death-camera validation"));return false;}
    if(!TestTrue(TEXT("Temporary survivor practice launches"),UProjectProject01TutorialEditorLibrary::StartPractice(false,true)))return false;
    GEditor->StartQueuedPlaySessionRequest();
    struct FState{int32 Stage=0;double At=0;bool OriginalRendering=false;TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UProjectInstantReplay> Death;};
    auto State=MakeShared<FState>();const double Deadline=FPlatformTime::Seconds()+60;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Deadline,Case]()
    {
        auto End=[this]()
        {
            GEditor->RequestEndPlayMap();const double Stop=FPlatformTime::Seconds()+15;
            AddCommand(new FFunctionLatentCommand([this,Stop]{if(!GEditor->IsPlaySessionInProgress())return true;if(FPlatformTime::Seconds()<Stop)return false;AddError(TEXT("Death-camera PIE failed to stop"));return true;}));
        };
        if(FPlatformTime::Seconds()>Deadline){AddError(FString::Printf(TEXT("Death-camera case %s timed out at stage %d"),*Case,State->Stage));End();return true;}
        UWorld* World=GEditor->PlayWorld;if(!World)return false;
        if(State->Stage==4)
        {
            if(World==State->World.Get()||!World->GetMapName().Contains(TEXT("TitleLevel")))return false;
            if(auto* Old=State->Death.Get())TestFalse(TEXT("Map travel cancels the old preview and pending death camera"),Old->IsDeathCameraPending());
            auto* Loading=World->GetGameInstance()->GetSubsystem<UProjectLoadingScreenSubsystem>();
            if(Loading&&Loading->IsLoading())return false;
            TestTrue(TEXT("Travel restores gameplay rendering"),World->GetGameViewport()&&!World->GetGameViewport()->bDisableWorldRendering);End();return true;
        }
        auto* PC=Cast<AMultiplayTestPlayerController>(World->GetFirstPlayerController());auto* Pawn=PC?Cast<APlayerCharacter>(PC->GetPawn()):nullptr;
        auto* Instance=World->GetGameInstance<UProjectProject01GameInstance>();auto* Replay=Instance?Instance->GetSubsystem<UProjectInstantReplay>():nullptr;
        if(!Pawn||!Replay||!World->HasBegunPlay()||!World->GetGameViewport())return false;
        const double Now=FPlatformTime::Seconds();
        if(State->Stage==0)
        {
            // Isolate every request from the test instance before using a synthetic identity.
            auto* Address=FindFProperty<FStrProperty>(Instance->GetClass(),TEXT("SecurityApiBaseUrl"));
            if(!Address){AddError(TEXT("Cannot isolate death-camera test traffic"));End();return true;}
            Address->SetPropertyValue_InContainer(Instance,FString());
            FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=TEXT("death-camera-fixture");Claim.MatchId=FGuid::NewGuid().ToString();Claim.Role=TEXT("Survivor");
            PC->SetAuthenticatedLobbyIdentity(Claim);PC->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
            State->OriginalRendering=World->GetGameViewport()->bDisableWorldRendering;State->At=Now;State->Stage=1;return false;
        }
        if(State->Stage==1)
        {
            if(Now-State->At<3)return false;
            Replay->QueueDeathCamera(Pawn,TEXT("마네킹에게 붙잡혔습니다."));State->At=Now;State->Stage=2;return false;
        }
        if(State->Stage==2)
        {
            if(!Replay->IsPresenting())return false;
            TestTrue(TEXT("Buffered real skeletal poses produce a rendered death-camera preview"),Replay->IsDeathCameraPending());
            TestTrue(TEXT("Preview owns the image while live world rendering is suppressed"),World->GetGameViewport()->bDisableWorldRendering);
            State->Death=Replay;State->World=World;State->At=Now;State->Stage=3;
            if(Case==TEXT("Logout"))
            {
                Instance->GetSubsystem<UProjectProject01AuthSubsystem>()->ForceClearLocalSession();
                TestFalse(TEXT("Logout immediately removes death preview and pending clip"),Replay->IsDeathCameraPending());
                TestEqual(TEXT("Logout restores prior viewport rendering"),World->GetGameViewport()->bDisableWorldRendering,State->OriginalRendering);End();return true;
            }
            if(Case==TEXT("Travel")){State->Stage=4;UGameplayStatics::OpenLevel(World,TEXT("/Game/MyProject/Level/TitleLevel"));}
            return false;
        }
        if(Replay->IsPresenting())return false;
        TestFalse(TEXT("Natural completion clears presentation and pending state"),Replay->IsDeathCameraPending());
        TestEqual(TEXT("Natural completion restores prior viewport rendering"),World->GetGameViewport()->bDisableWorldRendering,State->OriginalRendering);End();return true;
    }));
    return true;
}
#endif
