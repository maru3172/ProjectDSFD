#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectProject01FrontendGameMode.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectDFSDTitleScene.h"
#include "ProjectDFSDStartupIntro.h"
#include "Camera/PlayerCameraManager.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "PlayInEditorDataTypes.h"
#include "Kismet/GameplayStatics.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDFSDTitlePIETest,"ProjectProject01.Editor.TitlePresentation",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectDFSDTitlePIETest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) { AddError(TEXT("Stop PIE before this test")); return false; }
	auto Request=UProjectProject01TutorialEditorLibrary::MakePracticeRequest();
	Request.GlobalMapOverride=TEXT("/Game/MyProject/Level/TitleLevel");
	Request.GameModeOverride=AProjectProject01TitleGameMode::StaticClass();
	GEditor->RequestPlaySession(Request);GEditor->StartQueuedPlaySessionRequest();
	struct FState { int32 Stage=0; double At=0; TWeakObjectPtr<UWorld> Previous; };
	auto State=MakeShared<FState>();const double Deadline=FPlatformTime::Seconds()+65;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Deadline]()
	{
		const auto End=[this]()
		{
			GEditor->RequestEndPlayMap();const double EndDeadline=FPlatformTime::Seconds()+15;
			AddCommand(new FFunctionLatentCommand([this,EndDeadline]()
			{
				if(!GEditor->IsPlaySessionInProgress())return true;
				if(FPlatformTime::Seconds()<EndDeadline)return false;
				AddError(TEXT("Title PIE did not stop"));return true;
			}));
		};
		if(FPlatformTime::Seconds()>Deadline){AddError(FString::Printf(TEXT("Title PIE timed out at %d"),State->Stage));End();return true;}
		UWorld* World=GEditor->PlayWorld;if(!World)return false;
		auto* PC=Cast<AProjectProject01TitlePlayerController>(World->GetFirstPlayerController());
		if(State->Stage==2)
		{
			if(World==State->Previous.Get()||!World->GetFirstPlayerController()||PC)return false;
			if(State->At==0){State->At=FPlatformTime::Seconds();return false;}
			if(FPlatformTime::Seconds()-State->At<1.5)return false;
			TestFalse(TEXT("Leaving title removes every decorative set"),static_cast<bool>(TActorIterator<AProjectDFSDTitleScene>(World)));
			if(auto Window=World->GetGameViewport()?World->GetGameViewport()->GetWindow():nullptr)
			{
				TArray<FColor> Pixels;FIntVector Size;
				if(TestTrue(TEXT("Read the themed login through the real viewport"),FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size)))
				{
					const FString Directory=FPaths::ProjectSavedDir()/TEXT("UIThemeQA");IFileManager::Get().MakeDirectory(*Directory,true);
					TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
					FFileHelper::SaveArrayToFile(PNG,*(Directory/TEXT("ActualLoginUI.png")));
				}
			}
			State->Previous=World;State->Stage=3;
			UGameplayStatics::OpenLevel(World,TEXT("/Game/MyProject/Level/TitleLevel"));return false;
		}
		if(!PC||!PC->TitleScene||!PC->TitleWidget)return false;
		if(State->Stage==0)
		{
			TestTrue(TEXT("First title entry owns the initial logo intro"),PC->StartupIntro&&PC->StartupIntro->IsInViewport());
			TestTrue(TEXT("Real title controller selects the decorative camera"),PC->GetViewTarget()==PC->TitleScene);
			State->Stage=1;return false;
		}
		if(State->Stage==1)
		{
			if(PC->StartupIntro&&PC->StartupIntro->IsInViewport())return false;
			if(State->At==0){State->At=FPlatformTime::Seconds();return false;}
			if(FPlatformTime::Seconds()-State->At<1.5)return false;
			TestTrue(TEXT("After intro the normal title controls are visible"),PC->TitleWidget->GetVisibility()==ESlateVisibility::Visible);
			TestTrue(TEXT("The actual player camera is inside the isolated title set"),PC->PlayerCameraManager->GetCameraLocation().Z<-19000);
			if(auto* Viewport=World->GetGameViewport()?World->GetGameViewport()->Viewport:nullptr)
			{
				const auto Size=Viewport->GetSizeXY();TArray<FColor> Pixels;
				if(TestTrue(TEXT("Read the real title viewport, not only an isolated capture"),Viewport->ReadPixels(Pixels)&&Pixels.Num()==Size.X*Size.Y))
				{
					const FString Directory=FPaths::ProjectSavedDir()/TEXT("TitleSceneQA");IFileManager::Get().MakeDirectory(*Directory,true);
					TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
					FFileHelper::SaveArrayToFile(PNG,*(Directory/TEXT("ActualTitlePIE.png")));
					if(auto Window=World->GetGameViewport()->GetWindow())
					{
						TArray<FColor> WithUI;FIntVector UI;
						if(TestTrue(TEXT("Read title with the real Slate menu"),FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),WithUI,UI)))
						{
							FImageUtils::PNGCompressImageArray(UI.X,UI.Y,WithUI,PNG);
							FFileHelper::SaveArrayToFile(PNG,*(Directory/TEXT("ActualTitleUI.png")));
						}
					}
				}
			}
			else AddError(TEXT("Title PIE viewport was unavailable"));
			State->Previous=World;State->Stage=2;State->At=0;
			UGameplayStatics::OpenLevel(World,TEXT("/Game/MyProject/Level/LoginLevel"));return false;
		}
		if(State->Stage==3)
		{
			if(World==State->Previous.Get())return false;
			TestFalse(TEXT("Returning to title does not repeat school/team logos"),IsValid(PC->StartupIntro));
			TestTrue(TEXT("Returning title has visible, enabled controls"),PC->TitleWidget->IsVisible()&&PC->TitleWidget->GetIsEnabled()&&PC->TitleWidget->GetVisibility()!=ESlateVisibility::HitTestInvisible);
			TestTrue(TEXT("Returning title selects its new decorative camera"),PC->GetViewTarget()==PC->TitleScene);
			End();return true;
		}
		return false;
	}));
	return true;
}
#endif
