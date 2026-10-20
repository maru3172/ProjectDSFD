#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectDFSDStartupIntro.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectDFSDBranding.h"
#include "Misc/AutomationTest.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDFSDIntroTimelineTest,"ProjectProject01.Frontend.StartupIntroTimeline",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectDFSDIntroTimelineTest::RunTest(const FString&)
{
	FProjectDFSDIntroTimeline T;
	TestEqual(TEXT("First logo starts black"),T.Opacity(),0.0f);
	T.Advance(100);TestTrue(TEXT("A loading hitch cannot skip the logo"),T.Stage==0&&T.Elapsed<=0.05f);
	T.Advance(-10);TestTrue(TEXT("Negative time cannot rewind"),T.Elapsed>=0);
	for(int32 I=0;I<24;++I)T.Advance(0.05f);
	TestTrue(TEXT("University logo fully fades in"),FMath::IsNearlyEqual(T.Opacity(),1.0f));
	for(int32 I=0;I<30;++I)T.Advance(0.05f);
	TestTrue(TEXT("Full logo has a readable hold"),T.Stage==0&&T.Opacity()>0.99f);
	for(int32 I=0;I<19;++I)T.Advance(0.05f);
	TestTrue(TEXT("University transitions through black into team"),T.Stage==1&&T.Opacity()==0);
	for(int32 I=0;I<74;++I)T.Advance(0.05f);
	TestTrue(TEXT("Team fades out before revealing title"),T.Stage==2&&T.Opacity()==0);
	for(int32 I=0;I<30;++I)T.Advance(0.05f);
	TestEqual(TEXT("Natural sequence completes"),T.Stage,3);
	for(float RevealTime : {0.0f,0.75f,FProjectDFSDIntroTimeline::Reveal-0.01f})
	{
		FProjectDFSDIntroTimeline Reveal;Reveal.Stage=2;Reveal.Elapsed=RevealTime;Reveal.Skip();
		TestTrue(TEXT("Click completes title reveal instantly at any progress"),Reveal.Stage==3&&!Reveal.bSkipping);
		Reveal.Skip();Reveal.Advance(0.05f);
		TestEqual(TEXT("Repeated clicks cannot restart the completed intro"),Reveal.Stage,3);
	}
	T={};for(int32 I=0;I<12;++I)T.Advance(0.05f);
	const float Before=T.Opacity();T.Skip();
	TestEqual(TEXT("Click preserves current fade opacity, no flash"),T.Opacity(),Before);
	T.Skip();for(int32 I=0;I<5;++I)T.Advance(0.05f);
	TestTrue(TEXT("Repeated clicks during release skip exactly one logo"),T.Stage==1&&!T.bSkipping&&T.Opacity()==0);
	T.Skip();for(int32 I=0;I<5;++I)T.Advance(0.05f);
	TestEqual(TEXT("Second logo can also be skipped"),T.Stage,2);
	auto* GI=NewObject<UProjectProject01GameInstance>();
	TestTrue(TEXT("First title entry shows intro"),GI->ConsumeStartupIntro());
	TestFalse(TEXT("Returning to title within same execution never repeats intro"),GI->ConsumeStartupIntro());
	TestTrue(TEXT("New execution/PIE instance can show it again"),NewObject<UProjectProject01GameInstance>()->ConsumeStartupIntro());
	TestEqual(TEXT("Title/login use the configured game name"),ProjectDFSDBranding::GameTitle(),FString(TEXT("백화점 화재사건")));
	const FString Fixture=FPaths::ProjectSavedDir()/TEXT("StartupIntroQA/Migration")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Old=Fixture/TEXT("Old"),New=Fixture/TEXT("New");
	IFileManager::Get().MakeDirectory(*(Old/TEXT("SaveGames")),true);
	FFileHelper::SaveStringToFile(TEXT("old-slot"),*(Old/TEXT("SaveGames/slot.sav")));
	FFileHelper::SaveStringToFile(TEXT("private-token"),*(Old/TEXT("auth.json")));
	IFileManager::Get().MakeDirectory(*(New/TEXT("SaveGames")),true);
	FFileHelper::SaveStringToFile(TEXT("newer-slot"),*(New/TEXT("SaveGames/keep.sav")));
	FFileHelper::SaveStringToFile(TEXT("older-slot"),*(Old/TEXT("SaveGames/keep.sav")));
	TestTrue(TEXT("Copy-only import succeeds"),ProjectDFSDBranding::ImportLegacyUserData(Old,New));
	FString Copied;FFileHelper::LoadFileToString(Copied,*(New/TEXT("SaveGames/slot.sav")));
	TestEqual(TEXT("Existing checkpoint names stay compatible"),Copied,FString(TEXT("old-slot")));
	FFileHelper::LoadFileToString(Copied,*(New/TEXT("SaveGames/keep.sav")));
	TestEqual(TEXT("New data is never overwritten by legacy import"),Copied,FString(TEXT("newer-slot")));
	TestFalse(TEXT("Authentication data is never migrated"),IFileManager::Get().FileExists(*(New/TEXT("auth.json"))));
	FFileHelper::SaveStringToFile(TEXT("later"),*(Old/TEXT("SaveGames/later.sav")));
	ProjectDFSDBranding::ImportLegacyUserData(Old,New);
	TestFalse(TEXT("Migration is one-time so deleted saves cannot reappear"),IFileManager::Get().FileExists(*(New/TEXT("SaveGames/later.sav"))));
	TestTrue(TEXT("Original legacy saves remain recoverable"),IFileManager::Get().FileExists(*(Old/TEXT("SaveGames/slot.sav"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDFSDIntroRenderTest,"ProjectProject01.Frontend.StartupIntroRender",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectDFSDIntroRenderTest::RunTest(const FString&)
{
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("StartupIntroQA");IFileManager::Get().MakeDirectory(*Directory,true);
	for(const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600),FIntPoint(1920,800)})
	{
		auto* W=NewObject<UProjectDFSDStartupIntro>();W->Initialize();
		if(!TestNotNull(TEXT("Staged branding PNG loads without editor asset dependencies"),W->LogoTexture.Get()))continue;
		TestTrue(TEXT("Runtime logo preserves sRGB colors"),W->LogoTexture->SRGB);
		TestTrue(TEXT("University image keeps its original wide proportions"),FMath::IsNearlyEqual(
			static_cast<float>(W->LogoTexture->GetSizeX())/W->LogoTexture->GetSizeY(),2287.0f/831.0f,0.01f));
		TSharedRef<SWidget> Slate=SNew(SOverlay)
			+SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0.05f,0.08f,0.12f,1))]
			+SOverlay::Slot()[W->TakeWidget()];
		FWidgetRenderer Renderer(true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,true);
		auto Capture=[&](int32 Stage,float Seconds,const TCHAR* Name)
		{
			W->Timeline.Stage=Stage;W->Timeline.Elapsed=Seconds;W->ApplyFrame();
			Renderer.DrawWidget(Target,Slate,FVector2D(Size),0);FlushRenderingCommands();
			TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
			TestTrue(TEXT("Read real startup overlay pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
			TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
			TestTrue(TEXT("Save startup image for visual QA"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),Name,Size.X,Size.Y))));
			return Pixels;
		};
		const auto Black=Capture(0,0,TEXT("Black"));
		const auto LogoPixels=Capture(0,1.5f,TEXT("University"));
		auto* Logo=Cast<UImage>(W->LogoBox->GetChildAt(0));
		const FGeometry G=Logo->GetCachedGeometry();
		const FVector2D Start=G.LocalToAbsolute(FVector2D::ZeroVector),End=G.LocalToAbsolute(G.GetLocalSize());
		const FVector2D Actual=End-Start,Center=(Start+End)*0.5;
		TestTrue(TEXT("Logo is exactly screen-centered, including on 4:3 and ultrawide"),Center.Equals(FVector2D(Size)*0.5,1.0));
		TestTrue(TEXT("Logo aspect ratio never stretches"),FMath::IsNearlyEqual(Actual.X/Actual.Y,
			static_cast<double>(W->LogoTexture->GetSizeX())/W->LogoTexture->GetSizeY(),0.005));
		TestTrue(TEXT("Logo remains inside safe margins without cropping"),Start.X>=0&&Start.Y>=0&&End.X<=Size.X&&End.Y<=Size.Y);
		TestTrue(TEXT("University is visibly rendered over black"),LogoPixels!=Black);
		const auto Team=Capture(1,1.5f,TEXT("Team"));
		TestTrue(TEXT("Team DSFD is separately rendered"),Team!=Black&&Team!=LogoPixels);
		Capture(2,0.75f,TEXT("TitleReveal"));
		int32 CompletionCount=0;
		W->OnFinished.BindLambda([&CompletionCount]{++CompletionCount;});
		const FGeometry MouseGeometry=FGeometry::MakeRoot(FVector2D(Size),FSlateLayoutTransform());
		const TSet<FKey> NoButtons;
		const FPointerEvent RightClick(0,FVector2D::ZeroVector,FVector2D::ZeroVector,NoButtons,EKeys::RightMouseButton,0,FModifierKeysState());
		W->NativeOnMouseButtonDown(MouseGeometry,RightClick);
		TestTrue(TEXT("Right click does not finish reveal"),W->Timeline.Stage==2&&CompletionCount==0);
		const FPointerEvent LeftClick(0,FVector2D::ZeroVector,FVector2D::ZeroVector,NoButtons,EKeys::LeftMouseButton,0,FModifierKeysState());
		TestTrue(TEXT("Finishing left click is consumed, not passed to title menu"),W->NativeOnMouseButtonDown(MouseGeometry,LeftClick).IsEventHandled());
		TestTrue(TEXT("Click clears darkness and enables title completion in the same event, without waiting a tick"),
			W->Timeline.Stage==3&&W->GetRenderOpacity()==0.0f&&CompletionCount==1);
		W->NativeOnMouseButtonDown(MouseGeometry,LeftClick);W->NativeTick(MouseGeometry,0.05f);
		TestEqual(TEXT("Completion callback is only invoked once"),CompletionCount,1);
		W->ReleaseSlateResources(true);FlushRenderingCommands();
	}
	return true;
}
#endif
