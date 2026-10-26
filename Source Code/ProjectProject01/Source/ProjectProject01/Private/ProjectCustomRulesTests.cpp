#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectCustomRoomRulesWidget.h"
#include "MultiplayTestGameMode.h"
#include "PlayerCharacter.h"
#include "MannequinAICharacter.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectCaseRecords.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesTest,"ProjectProject01.CustomRules.AuthorityAndRuntime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCustomRulesTest::RunTest(const FString&)
{
    FProjectCustomMatchRules Rules;Rules.bEnabled=true;Rules.DeathCount=6;Rules.MannequinCount=4;Rules.TimeLimitSeconds=300;
    auto Json=Rules.ToJson();FProjectCustomMatchRules Parsed;
    TestTrue(TEXT("Rules cross the trusted JSON boundary unchanged"),FProjectCustomMatchRules::Parse(Json,Parsed)&&Parsed==Rules);
    for(double Bad:{0.,21.,1.5}){Json->SetNumberField(TEXT("deathCount"),Bad);TestFalse(TEXT("Invalid death counts rejected atomically"),FProjectCustomMatchRules::Parse(Json,Parsed));TestTrue(TEXT("Failed parse retains last valid rules"),Parsed==Rules);}
    Json=Rules.ToJson();Json->SetNumberField(TEXT("mannequinCount"),11);TestFalse(TEXT("Number-key roster cannot exceed ten"),FProjectCustomMatchRules::Parse(Json,Parsed));
    Json=Rules.ToJson();Json->SetNumberField(TEXT("timeLimitSeconds"),61);TestFalse(TEXT("Fractional minutes rejected"),FProjectCustomMatchRules::Parse(Json,Parsed));
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Authoritative rules fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();FString Error,Match=FGuid::NewGuid().ToString();
    TestFalse(TEXT("No match identity cannot select rules"),Mode->AcceptCustomRules(TEXT(""),Rules,Error));
    TestTrue(TEXT("Trusted first admission pins custom rules"),Mode->AcceptCustomRules(Match,Rules,Error));
    TestEqual(TEXT("Custom roster count reaches spawn system"),Mode->MannequinSpawnCount,4);TestEqual(TEXT("Custom limit reaches authoritative timeout"),Mode->MatchTimeLimitSeconds,300.f);
    TestTrue(TEXT("Further claims are idempotent"),Mode->AcceptCustomRules(Match,Rules,Error));
    auto Changed=Rules;Changed.DeathCount=1;TestFalse(TEXT("Later player cannot change the frozen rules"),Mode->AcceptCustomRules(Match,Changed,Error));
    TestFalse(TEXT("Another match cannot reuse an occupied world"),Mode->AcceptCustomRules(FGuid::NewGuid().ToString(),Rules,Error));
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Survivor=World->SpawnActor<APlayerCharacter>(APlayerCharacter::StaticClass(),FTransform(FVector(0,0,500)),Spawn);Survivor->DispatchBeginPlay();
    TestEqual(TEXT("Spawned survivor uses selected death count after tuning"),Survivor->GetRemainingDeathCount(),6);
    for(int I=0;I<6;++I)World->SpawnActor<AMannequinAICharacter>(AMannequinAICharacter::StaticClass(),FTransform(FVector(I*300,1000,500)),Spawn);
    AddExpectedError(TEXT("Keeping legacy mannequin transforms"),EAutomationExpectedErrorFlags::Contains,1);Mode->InitializeMannequinSpawns();
    int32 Active=0;for(TActorIterator<AMannequinAICharacter> It(World);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()){++Active;TestTrue(TEXT("Remaining control slots stay in range"),It->GetControlSlot()>=0&&It->GetControlSlot()<4);}
    TestEqual(TEXT("Extra mannequins removed for custom count on legacy maps too"),Active,4);
    FMultiplayTestServerPlayerRecord Record;Record.Role=TEXT("Survivor");Record.SurvivorState=EMultiplayTestSurvivorState::Escaped;
    auto Result=Mode->BuildFinalResult(nullptr,Record);TestTrue(TEXT("Results preserve success and custom identity"),Result.bSuccess&&Result.bCustomMatch);TestFalse(TEXT("Custom result never offers official registration"),Result.bLeaderboardVerificationReady);
    FMultiplayTestMatchCarryover Carry;Carry.MatchId=Match;Carry.CustomRules=Rules;
    auto* Next=World->SpawnActor<AMultiplayTestGameMode>();TestTrue(TEXT("Next floor accepts carried rules"),Next->AcceptCustomRules(Carry.MatchId,Carry.CustomRules,Error));TestEqual(TEXT("Next floor keeps selected limit"),Next->MatchTimeLimitSeconds,300.f);
    auto* Standard=World->SpawnActor<AMultiplayTestGameMode>();Standard->MannequinSpawnCount=7;Standard->MatchTimeLimitSeconds=900;
    TestTrue(TEXT("Standard admission still accepted"),Standard->AcceptCustomRules(FGuid::NewGuid().ToString(),FProjectCustomMatchRules(),Error));
    TestEqual(TEXT("Standard maps retain their authored roster"),Standard->MannequinSpawnCount,7);TestEqual(TEXT("Standard maps retain their authored limit"),Standard->MatchTimeLimitSeconds,900.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCustomRulesRenderTest,"ProjectProject01.CustomRules.Render",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectCustomRulesRenderTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* GI=NewObject<UGameInstance>();World->SetGameInstance(GI);GI->Init();
    auto* L=GI->GetSubsystem<UProjectProject01LobbySubsystem>();auto* W=NewObject<UProjectCustomRoomRulesWidget>(World);
    ON_SCOPE_EXIT{W->ReleaseSlateResources(true);GI->Shutdown();World->SetGameInstance(nullptr);World->DestroyWorld(false);};
    L->CurrentRoom.RoomId=TEXT("offline-render");L->CurrentRoom.bIsHost=true;W->RoomId=L->CurrentRoom.RoomId;W->bEditable=true;W->Draft.bEnabled=true;W->Draft.DeathCount=6;W->Draft.MannequinCount=4;
    static_cast<UUserWidget*>(W)->Initialize();auto Slate=W->TakeWidget();
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("CustomRulesUIQA");IFileManager::Get().MakeDirectory(*Directory,true);FWidgetRenderer Renderer(false);
    for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
    {
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
        // Let the renderer finish uploading newly requested Korean/font atlas glyphs before capture.
        for(int I=0;I<6;++I){Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),.016f);FlushRenderingCommands();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
        if(TestTrue(TEXT("Custom editor renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))
        {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("Rules_%dx%d.png"),Size.X,Size.Y)));}
        int Numbers=0;bool Back=false,Apply=false,Scroll=false;
        TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> Node)
        {
            if(Node->GetType()==FName(TEXT("SScrollBox")))Scroll=true;
            if(Node->GetType()==FName(TEXT("SSpinBox")))++Numbers;
            if(Node->GetType()==FName(TEXT("STextBlock")))
            {auto T=StaticCastSharedRef<STextBlock>(Node);FString Text=T->GetText().ToString();Back|=Text==TEXT("돌아가기");Apply|=Text==TEXT("적용");
              if(!Text.IsEmpty())
              {
                  const auto G=Node->GetCachedGeometry();auto P=G.GetAbsolutePosition(),S=G.GetAbsoluteSize();TestTrue(TEXT("Visible labels fit viewport"),P.X>=0&&P.Y>=0&&P.X+S.X<=Size.X+1&&P.Y+S.Y<=Size.Y+1);
                  if(Text==TEXT("데스카운트")||Text==TEXT("마네킹 수")||Text==TEXT("적용")||Text==TEXT("돌아가기"))
                  {
                      int Bright=0;for(int Y=FMath::Max(0,int(P.Y));Y<FMath::Min(Size.Y,int(P.Y+S.Y));++Y)for(int X=FMath::Max(0,int(P.X));X<FMath::Min(Size.X,int(P.X+S.X));++X)
                          if(Pixels.IsValidIndex(Y*Size.X+X)&&Pixels[Y*Size.X+X].R>120)++Bright;
                      TestTrue(TEXT("Primary labels contain visibly rendered glyphs: ")+Text,Bright>5);
                  }
              }}
            if(auto* C=Node->GetChildren())for(int I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));
        };Inspect(Slate);TestTrue(TEXT("Apply and back remain visible without page scrolling"),Apply&&Back&&!Scroll);
        Target->ReleaseResource();
    }
    return true;
}
#endif
