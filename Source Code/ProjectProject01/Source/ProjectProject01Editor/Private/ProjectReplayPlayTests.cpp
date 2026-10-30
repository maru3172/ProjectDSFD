#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectMatchReplay.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "MultiplayTestPlayerController.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/DemoNetDriver.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "NetworkReplayStreaming.h"
#include "PlayInEditorDataTypes.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "ProjectEditorTestWorlds.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReplayPlayTest,"ProjectProject01.Editor.Replay.RoundTrip",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReplayPlayTest::RunTest(const FString&)
{
    if(!GEditor||GEditor->IsPlaySessionInProgress()){AddError(TEXT("Stop PIE before replay validation"));return false;}
    if(!UProjectProject01TutorialEditorLibrary::StartPractice(false,true)){AddError(TEXT("Replay fixture could not start PIE"));return false;}
    GEditor->StartQueuedPlaySessionRequest();
    struct FState{int32 Stage=0;double At=0;FString Name,Directory;};auto State=MakeShared<FState>();
    State->Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FeatureChecks/ReplayRoundTrip")/FGuid::NewGuid().ToString(EGuidFormats::Digits))+TEXT("/");
    IFileManager::Get().MakeDirectory(*State->Directory,true);const double Deadline=FPlatformTime::Seconds()+90;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Deadline]()
    {
        auto End=[this,State]()
        {
            // Keep a failed fixture for diagnosis; never enumerate/delete the user's replays.
            GEditor->RequestEndPlayMap();const double Stop=FPlatformTime::Seconds()+15;
            AddCommand(new FFunctionLatentCommand([this,Stop]{if(!GEditor->IsPlaySessionInProgress())return true;if(FPlatformTime::Seconds()<Stop)return false;AddError(TEXT("Replay PIE failed to stop"));return true;}));
            // The worlds the replay loaded finish their teardown on the following frames; collect them here, so a
            // later test's own garbage collection never meets a half torn-down replay world.
            auto Frames=MakeShared<int32>(0);
            AddCommand(new FFunctionLatentCommand([Frames]{if(++*Frames<4)return false;ProjectEditorTestWorlds::ReleaseLeftoverWorlds();return true;}));
        };
        if(FPlatformTime::Seconds()>Deadline){AddError(FString::Printf(TEXT("Replay round trip timed out at stage %d"),State->Stage));End();return true;}
        UWorld* World=GEditor->PlayWorld;
        // Waiting for playback: what the world and demo driver look like (grep "[ReplayWait]") when it never starts.
        if(State->Stage==3&&World&&FPlatformTime::Seconds()-State->At>2)
        {
            State->At=FPlatformTime::Seconds();auto* Driver=World->GetDemoNetDriver();auto* First=World->GetFirstPlayerController();
            UE_LOG(LogTemp,Display,TEXT("[ReplayWait] Map=%s Seamless=%d Begun=%d Time=%.2f Real=%.2f DebugPause=%d Playing=%d PC=%s Paused=%d Driver=%d DemoTime=%.2f Total=%.2f Dilation=%.2f Frame=%d Data=%d"),
                *World->GetMapName(),World->IsInSeamlessTravel(),World->HasBegunPlay(),World->GetTimeSeconds(),World->GetRealTimeSeconds(),World->bDebugPauseExecution,World->IsPlayingReplay(),*GetNameSafe(First),World->IsPaused(),
                Driver!=nullptr,Driver?Driver->GetDemoCurrentTime():-1.f,Driver?Driver->GetDemoTotalTime():-1.f,World->GetWorldSettings()->DemoPlayTimeDilation,Driver?Driver->GetDemoFrameNum():-1,Driver&&Driver->GetReplayStreamer().IsValid()&&Driver->GetReplayStreamer()->IsDataAvailable());
        }
        if(!World||!World->HasBegunPlay())return false;
        auto* Instance=World->GetGameInstance<UProjectProject01GameInstance>();auto* Replay=Instance?Instance->GetSubsystem<UProjectMatchReplay>():nullptr;
        if(!Replay)return false;const double Now=FPlatformTime::Seconds();
        auto* PC=World->GetFirstPlayerController();
        if(State->Stage==0)
        {
            auto* Player=Cast<AMultiplayTestPlayerController>(PC);if(!Player||!Player->GetPawn()||!Replay->Streamer)return false;
            auto* Address=FindFProperty<FStrProperty>(Instance->GetClass(),TEXT("SecurityApiBaseUrl"));if(!Address){AddError(TEXT("Cannot isolate replay HTTP traffic"));End();return true;}
            Address->SetPropertyValue_InContainer(Instance,FString());
            FProjectProject01ValidatedJoinClaim Claim;Claim.UserId=TEXT("replay-fixture");Claim.MatchId=FGuid::NewGuid().ToString();Claim.Role=TEXT("Survivor");
            Player->SetAuthenticatedLobbyIdentity(Claim);Player->SetServerMatchState(EMultiplayTestMatchPhase::InProgress,EMultiplayTestSurvivorState::Active);
            // Test-only opt-in changes this temporary subsystem, not the editor/game preference.
            TestEqual(TEXT("The fixture has a separate replay directory"),Replay->Streamer->SetDemoPath(State->Directory),EStreamingOperationResult::Success);
            Replay->bAllowPersistence=true;Replay->bRecordingEnabled=true;Replay->bCatalogueReady=true;
            UGameplayStatics::SetGamePaused(World,false);State->Stage=1;State->At=Now;return false;
        }
        if(State->Stage==1)
        {
            auto* Driver=World->GetDemoNetDriver();if(!Driver||!Driver->IsRecording())return false;
            if(State->Name.IsEmpty())State->Name=Replay->RecordingName;
            if(Driver->GetDemoCurrentTime()<3)return false;
            TestTrue(TEXT("Real game recording uses a current-protocol owned name"),UProjectMatchReplay::IsOwnedName(State->Name));
            Replay->bRecordingEnabled=false;Replay->StopRecording();State->Stage=2;State->At=Now;
            UGameplayStatics::OpenLevel(World,TEXT("/Game/MyProject/Level/TitleLevel"));return false;
        }
        if(State->Stage==2)
        {
            if(!PC||Cast<AMultiplayTestPlayerController>(PC)||!World->GetMapName().Contains(TEXT("TitleLevel")))return false;
            const auto* Entry=Replay->Entries.FindByPredicate([&](const auto& E){return E.Name==State->Name&&E.DurationMs>0;});
            if(!Entry){if(!Replay->IsBusy()&&Now-State->At>.25){State->At=Now;Replay->RefreshList();}return false;}
            TestTrue(TEXT("Recorded network replay has playable duration and file bytes"),Entry->DurationMs>=2000&&Entry->Bytes>0);
            if(!TestTrue(TEXT("Public play command starts the isolated recorded replay"),Replay->Play(State->Name))){End();return true;}
            State->Stage=3;return false;
        }
        if(State->Stage==3)
        {
            auto* Viewer=Cast<AProjectReplayController>(PC);auto* Driver=World->GetDemoNetDriver();if(!World->IsPlayingReplay()||!Viewer||!Driver||!Viewer->Controls)return false;
            TestFalse(TEXT("Replay spectator has no live multiplayer controller"),PC->IsA<AMultiplayTestPlayerController>());
            Viewer->bPaused=true;Viewer->PlayerTick(.016f);TestEqual(TEXT("Pause applies to demo time only"),World->GetWorldSettings()->DemoPlayTimeDilation,0.f);
            Viewer->bPaused=false;Viewer->PlaybackSpeed=2;Viewer->PlayerTick(.016f);TestEqual(TEXT("Playback speed reaches the actual driver world"),World->GetWorldSettings()->DemoPlayTimeDilation,2.f);
            Viewer->PlaybackSpeed=1;Viewer->Seek(.5f);State->Stage=4;State->At=Now;return false;
        }
        if(State->Stage==4)
        {
            auto* Viewer=Cast<AProjectReplayController>(PC);if(!Viewer||Viewer->bSeeking)return false;
            TestTrue(TEXT("Replay seek completes against an actual recorded file"),World->GetDemoNetDriver()&&FMath::IsFinite(World->GetDemoNetDriver()->GetDemoCurrentTime()));
            Replay->ExitPlayback();State->Stage=5;return false;
        }
        if(State->Stage==5)
        {
            if(World->IsPlayingReplay()||Cast<AProjectReplayController>(PC)||!World->GetMapName().Contains(TEXT("LoginLevel")))return false;
            TestFalse(TEXT("Exiting playback clears the pending playback guard"),Replay->IsPlaybackRequested());
            if(Replay->IsBusy())return false;Replay->RefreshList();State->Stage=6;return false;
        }
        if(State->Stage==6)
        {
            if(Replay->IsBusy())return false;
            if(!Replay->Entries.ContainsByPredicate([&](const auto& E){return E.Name==State->Name;})){AddError(TEXT("Fixture replay missing before delete"));End();return true;}
            Replay->Delete(State->Name);State->Stage=7;return false;
        }
        if(Replay->IsBusy())return false;
        const FString File=State->Directory+State->Name+FNetworkReplayStreaming::GetReplayFileExtension();
        TestFalse(TEXT("Public delete removes only the owned fixture replay"),IFileManager::Get().FileExists(*File));
        Replay->bAllowPersistence=false;IFileManager::Get().DeleteDirectory(*State->Directory,false,false);End();return true;
    }));
    return true;
}
#endif
