#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01Tutorial.h"
#include "ProjectPracticeEnvironment.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FProjectPracticeEnvironmentEditorTest,"ProjectProject01.Editor.PracticeEnvironment",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FProjectPracticeEnvironmentEditorTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    for(const TCHAR* Role:{TEXT("Survivor"),TEXT("Mannequin")})for(const TCHAR* State:{TEXT("On"),TEXT("Off")})
    {const FString Name=FString(Role)+TEXT(".")+State;Names.Add(Name);Commands.Add(Name);}
}
bool FProjectPracticeEnvironmentEditorTest::RunTest(const FString& Parameter)
{
    if(!GEditor||GEditor->IsPlaySessionInProgress()){AddError(TEXT("Stop PIE before testing practice layout"));return false;}
    const bool Mannequin=Parameter.StartsWith(TEXT("Mannequin")),Enabled=Parameter.EndsWith(TEXT("On"));
    const FString Previous=GEditor->UserEditedPlayWorldURL;
    // Obsolete adaptive URL parameters must not affect practice AI anymore.
    GEditor->UserEditedPlayWorldURL=FString::Printf(TEXT("/Game/MyProject/Level/MultiplayerTutorial?FreePractice=1?TutorialRole=%s?PracticeSurvivors=2?PracticeDirector=1?PracticeObstacles=%d?AdaptiveBots=1?BotDifficulty=5"),Mannequin?TEXT("Mannequin"):TEXT("Survivor"),Enabled?1:0);
    GEditor->RequestPlaySession(UProjectProject01TutorialEditorLibrary::MakePracticeRequest());GEditor->StartQueuedPlaySessionRequest();
    GEditor->UserEditedPlayWorldURL=Previous;
    const double Deadline=FPlatformTime::Seconds()+60;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Mannequin,Enabled,Deadline]()
    {
        auto End=[this]()
        {
            GEditor->RequestEndPlayMap();const double Stop=FPlatformTime::Seconds()+15;
            AddCommand(new FFunctionLatentCommand([this,Stop](){if(!GEditor->IsPlaySessionInProgress())return true;if(FPlatformTime::Seconds()<Stop)return false;AddError(TEXT("Practice layout PIE failed to stop"));return true;}));
        };
        if(FPlatformTime::Seconds()>Deadline){AddError(TEXT("Practice layout/navigation readiness timeout"));End();return true;}
        UWorld* World=GEditor->PlayWorld;auto* Mode=World?World->GetAuthGameMode<AProjectProject01TutorialGameMode>():nullptr;
        if(!Mode||!Mode->Learner||!Mode->bInitialized||Mode->bPracticeAwaitingPresentation)return false;
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        if(!Nav||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return false;
        TestEqual(TEXT("Practice role remains selected"),Mode->IsMannequinTutorial(),Mannequin);
        TestEqual(TEXT("Obstacle option reaches runtime"),Mode->HasPracticeObstacles(),Enabled);
        TestEqual(TEXT("Legacy mannequin director preserved"),Mode->IsPracticeDirectorEnabled(),!Mannequin);
        int32 Combos=0;
        if(Mode->PracticeWidget)Mode->PracticeWidget->WidgetTree->ForEachWidget([&Combos](UWidget* W){if(W->IsA<UComboBoxString>())++Combos;});
        TestEqual(TEXT("Practice has no difficulty selector"),Combos,0);
        if(Enabled)
        {
            if(TestNotNull(TEXT("Transient environment exists"),Mode->PracticeEnvironment.Get()))
            {
                TestTrue(TEXT("Layout provides multiple corners and islands, not one decorative cube"),Mode->PracticeEnvironment->GetObstacleCount()>=5);
                FCollisionQueryParams Query(SCENE_QUERY_STAT(PracticeTest),true);
                for(TActorIterator<APawn> It(World);It;++It)Query.AddIgnoredActor(*It);
                FHitResult Hit;
                TestTrue(TEXT("Central wall breaks line of sight"),World->LineTraceSingleByChannel(Hit,FVector(-600,0,120),FVector(200,0,120),ECC_Visibility,Query)&&Hit.GetActor()==Mode->PracticeEnvironment);
                TestFalse(TEXT("Alternate observation lane stays open"),World->LineTraceSingleByChannel(Hit,FVector(-600,300,120),FVector(200,300,120),ECC_Visibility,Query));
                auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(-1000,0,10),FVector(600,0,10));
                TestTrue(TEXT("AI can navigate around the wall on a full path"),Path&&Path->IsValid()&&!Path->IsPartial()&&Path->PathPoints.Num()>=3);
                auto* ExitPath=UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(-1000,0,10),FVector(1550,0,10));
                TestTrue(TEXT("Exit remains reachable"),ExitPath&&ExitPath->IsValid()&&!ExitPath->IsPartial());
                if(Mannequin&&FApp::CanEverRender())
                {
                    auto* Capture=World->SpawnActor<ASceneCapture2D>();auto* Component=Capture->GetCaptureComponent2D();
                    auto* Texture=NewObject<UTextureRenderTarget2D>(Capture);Texture->InitAutoFormat(1280,900);
                    Component->TextureTarget=Texture;Component->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
                    Component->ProjectionType=ECameraProjectionMode::Orthographic;Component->OrthoWidth=4400;
                    Component->bCaptureEveryFrame=false;Component->bCaptureOnMovement=false;
                    Capture->SetActorLocationAndRotation(FVector(0,0,2800),FRotator(-90,0,0));Component->CaptureScene();
                    TArray<FColor> Pixels;auto* Resource=Texture->GameThread_GetRenderTargetResource();
                    if(TestTrue(TEXT("Practice layout overview renders"),Resource&&Resource->ReadPixels(Pixels)&&Pixels.Num()==1280*900))
                    {
                        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1280,900,Pixels,PNG);
                        TestTrue(TEXT("Practice layout visual QA written"),FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("AdaptiveBotChecks/PracticeEnvironment.png"))));
                    }
                    Capture->Destroy();
                }
            }
        }
        else TestNull(TEXT("Toggle off restores authored obstacle-free arena"),Mode->PracticeEnvironment);
        End();return true;
    }));
    return true;
}
#endif
