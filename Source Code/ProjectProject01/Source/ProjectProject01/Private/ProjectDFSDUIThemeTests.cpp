#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectDFSDUITheme.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01VoiceSettingsWidget.h"
#include "ProjectProject01Tutorial.h"
#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/VerticalBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ProjectUIRenderCapture.h"
#include "Engine/UserInterfaceSettings.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWave.h"
#include "ProjectUIClickSound.h"
#include "Sound/SoundGenerator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDFSDUIThemeContractTest,"ProjectProject01.UITheme.Contracts",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectDFSDUIThemeContractTest::RunTest(const FString&)
{
	const auto L=[](FLinearColor C){return .2126f*C.R+.7152f*C.G+.0722f*C.B;};
	TestTrue(TEXT("Body text has at least WCAG 4.5:1 luminance contrast on panels"),(L(ProjectDFSDUI::Text())+.05f)/(L(ProjectDFSDUI::Panel())+.05f)>4.5f);
	const auto& Input=ProjectDFSDUI::InputStyle();
	TestTrue(TEXT("Normal/focused/read-only input captions remain bright"),Input.ForegroundColor.GetSpecifiedColor().R>.8f&&Input.FocusedForegroundColor.GetSpecifiedColor().R>.8f&&Input.ReadOnlyForegroundColor.GetSpecifiedColor().R>.8f);
	TestTrue(TEXT("Input focus is visibly different from its resting border"),Input.BackgroundImageNormal.OutlineSettings.Color!=Input.BackgroundImageFocused.OutlineSettings.Color);
	TestTrue(TEXT("Selected tabs have a persistent style, not only a hover color"),ProjectDFSDUI::ButtonStyle().Normal.TintColor!=ProjectDFSDUI::ButtonStyle(true).Normal.TintColor);
	TestTrue(TEXT("Title buttons retain a translucent neutral surface"),ProjectDFSDUI::TitleButtonStyle().Normal.TintColor.GetSpecifiedColor()==FLinearColor(.014f,.014f,.014f,.25f));
    for(const auto Color:{ProjectDFSDUI::Canvas(),ProjectDFSDUI::Panel(),ProjectDFSDUI::Text(),ProjectDFSDUI::Muted(),
        ProjectDFSDUI::ButtonStyle().Normal.TintColor.GetSpecifiedColor(),ProjectDFSDUI::TitleButtonStyle().HoveredForeground.GetSpecifiedColor()})
        TestTrue(TEXT("Base surfaces and captions are neutral grey without yellow/blue tint"),FMath::IsNearlyEqual(Color.R,Color.G)&&FMath::IsNearlyEqual(Color.G,Color.B));
    auto* Click=Cast<UProjectUIClickSound>(ProjectDFSDUI::ButtonStyle().PressedSlateSound.GetResourceObject());
    if(TestTrue(TEXT("Common buttons have a short generated click sound"),Click&&Click->Duration>0&&Click->Duration<.1f))
    {
        FSoundGeneratorInitParams Params;Params.SampleRate=24000;Params.NumChannels=1;
        auto First=Click->CreateSoundGenerator(Params),Second=Click->CreateSoundGenerator(Params);float Buffer[2048];First->OnGenerateAudio(Buffer,2048);
        float Peak=0;for(float Sample:Buffer)Peak=FMath::Max(Peak,FMath::Abs(Sample));
        TestTrue(TEXT("Click waveform is audible, bounded and finishes"),Peak>.01f&&Peak<.3f&&First->IsFinished());
        TestFalse(TEXT("Rapid repeated clicks have independent playback cursors"),Second->IsFinished());
    }
	const auto Normal=ProjectDFSDUI::ButtonStyle().Normal.TintColor.GetSpecifiedColor();
	TestTrue(TEXT("Other windows have a visibly brighter surface than panels and an outline"),L(Normal)>L(ProjectDFSDUI::Panel())*1.5f&&L(Normal)<.12f&&Normal.A>.9f&&ProjectDFSDUI::ButtonStyle().Normal.OutlineSettings.Width==1.f);
	TestTrue(TEXT("Body text stays comfortably readable on brighter buttons"),(L(ProjectDFSDUI::Text())+.05f)/(L(Normal)+.05f)>4.5f);
	UUserWidget* Lobby=NewObject<UProjectProject01LobbyWidget>();Lobby->Initialize();
	TestTrue(TEXT("Lobby supports keyboard focus when returning from settings or surveys"),Lobby->IsFocusable());
	auto* Owner=NewObject<UProjectProject01LoginWidget>();static_cast<UUserWidget*>(Owner)->Initialize();auto* Tree=Owner->WidgetTree.Get();
	auto* Root=Tree->ConstructWidget<UOverlay>();Tree->RootWidget=Root;
	auto* Content=Tree->ConstructWidget<UVerticalBox>();Root->AddChildToOverlay(Content);
	auto* Field=Tree->ConstructWidget<UEditableTextBox>();Field->SetIsPassword(true);Field->SetText(FText::FromString(TEXT("fixture-only")));Content->AddChild(Field);
	auto* Action=Tree->ConstructWidget<UButton>();Action->SetIsEnabled(false);Action->SetVisibility(ESlateVisibility::Hidden);Content->AddChild(Action);
	auto* Fade=Tree->ConstructWidget<UBorder>();Fade->SetBrushColor(FLinearColor::Black);Fade->SetRenderOpacity(.42f);Root->AddChildToOverlay(Fade);
	ProjectDFSDUI::ApplyTree(Tree);ProjectDFSDUI::FramePage(Tree,1000);
	TestTrue(TEXT("Page framing leaves the full-screen overlay root in place"),Tree->RootWidget==Root&&Fade->GetParent()==Root);
	TestTrue(TEXT("A full-screen fade keeps exact black and its animation opacity"),Fade->GetBrushColor()==FLinearColor::Black&&FMath::IsNearlyEqual(Fade->GetRenderOpacity(),.42f));
	TestTrue(TEXT("Theme does not change password masking or entered text"),Field->GetIsPassword()&&Field->GetText().ToString()==TEXT("fixture-only"));
	TestTrue(TEXT("Theme cannot enable or reveal a blocked action"),!Action->GetIsEnabled()&&Action->GetVisibility()==ESlateVisibility::Hidden);
	ProjectDFSDUI::Apply(Action,true);TestFalse(TEXT("Selection styling cannot enable a button"),Action->GetIsEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectDFSDUIThemeRenderTest,"ProjectProject01.UITheme.RenderPages",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectDFSDUIThemeRenderTest::RunTest(const FString&)
{
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("UIThemeQA");IFileManager::Get().MakeDirectory(*Directory,true);
	FWidgetRenderer Renderer(false);
	const auto Render=[&](UUserWidget* W,const FString& Name)
	{
		// Render the initialized tree only: no login, service polling, microphone or game actions.
		auto Slate=W->GetRootWidget()->TakeWidget();
		for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
		{
			auto* Target=ProjectUIRenderCapture::Create(Size);
			const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
			for(int32 Pass=0;Pass<3;++Pass)Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),0);
			FlushRenderingCommands();TArray<FColor> Pixels;
			if(!TestTrue(TEXT("Read initialized game UI pixels"),ProjectUIRenderCapture::Read(Target,Pixels)&&Pixels.Num()==Size.X*Size.Y))continue;
			TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
			TestTrue(TEXT("Save visual QA page"),FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("%s_%dx%d.png"),*Name,Size.X,Size.Y))));
			W->WidgetTree->ForEachWidget([&](UWidget* Widget)
			{
				if(auto* Frame=Cast<UProjectDFSDPageFrame>(Widget))
				{
					const auto G=Frame->GetContent()->GetCachedGeometry();
					TestTrue(TEXT("Page content fits within the viewport across aspect ratios"),G.GetAbsolutePosition().X>=0&&G.GetAbsolutePosition().Y>=0&&G.GetAbsolutePosition().X+G.GetAbsoluteSize().X<=Size.X+1&&G.GetAbsolutePosition().Y+G.GetAbsoluteSize().Y<=Size.Y+1);
				}
			});
			Target->ReleaseResource();
		}
	};
	struct FPage{UClass* Type;const TCHAR* Name;int32 Count;};
	for(const auto Page:{FPage{UProjectProject01LoginWidget::StaticClass(),TEXT("Login"),2},
		FPage{UProjectProject01SettingsWidget::StaticClass(),TEXT("Settings"),5},
		FPage{UProjectProject01LobbyWidget::StaticClass(),TEXT("Lobby"),6},
		FPage{UProjectProject01SessionMenuWidget::StaticClass(),TEXT("Pause"),3},
		FPage{UProjectProject01TitleWidget::StaticClass(),TEXT("TitleSave"),2},
		FPage{UProjectProject01LeaderboardWidget::StaticClass(),TEXT("Leaderboard"),1},
		FPage{UProjectProject01VoiceSettingsWidget::StaticClass(),TEXT("Voice"),1}})
	{
		auto* W=NewObject<UUserWidget>(GetTransientPackage(),Page.Type);if(!TestTrue(TEXT("Themed page initializes offline"),W->Initialize()))continue;
		UWidgetSwitcher* Switcher=nullptr;
		W->WidgetTree->ForEachWidget([&](UWidget* Child)
		{
			if(!Switcher)Switcher=Cast<UWidgetSwitcher>(Child);
			if(auto* Combo=Cast<UComboBoxString>(Child)){if(!Combo->GetOptionCount())Combo->AddOption(TEXT("기본"));Combo->SetSelectedIndex(0);}
		});
		for(int32 I=0;I<Page.Count;++I){if(Switcher)Switcher->SetActiveWidgetIndex(I);Render(W,FString::Printf(TEXT("%s%d"),Page.Name,I));}
		W->ReleaseSlateResources(true);
	}
	auto* Survey=NewObject<UProjectProject01SurveyWidget>();static_cast<UUserWidget*>(Survey)->Initialize();
	FProjectProject01SurveyDefinition Definition;Definition.Title=TEXT("플레이 피드백");Definition.Description=TEXT("여러분의 의견을 들려주세요.");
	FProjectProject01SurveyQuestion Choice;Choice.Id=TEXT("choice");Choice.Type=TEXT("Choice");Choice.Prompt=TEXT("어떤 역할을 플레이했나요?");Choice.Options={TEXT("생존자"),TEXT("마네킹")};
	FProjectProject01SurveyQuestion Written;Written.Id=TEXT("written");Written.Type=TEXT("Text");Written.Prompt=TEXT("가장 기억에 남은 순간을 알려주세요.");
	Definition.Questions={Choice,Written};Survey->ConfigureSurvey(Definition,nullptr);Render(Survey,TEXT("Survey"));Survey->ReleaseSlateResources(true);
	auto* Guide=NewObject<UProjectProject01GameGuideWidget>();static_cast<UUserWidget*>(Guide)->Initialize();Guide->Configure(false,true,false);Render(Guide,TEXT("SurvivorGuide"));Guide->Configure(true,true,false);Render(Guide,TEXT("MannequinGuide"));Guide->ReleaseSlateResources(true);
	auto* Result=NewObject<UProjectProject01MatchResultWidget>();static_cast<UUserWidget*>(Result)->Initialize();Result->ConfigureSinglePlayerResult(false);Render(Result,TEXT("DeathResult"));
	FMultiplayTestMatchResult Multiplayer;Multiplayer.Role=TEXT("Survivor");Multiplayer.Outcome=TEXT("Eliminated");
	Result->ConfigureResult(Multiplayer);Render(Result,TEXT("MultiplayerDeathResult"));
	Multiplayer.Outcome=TEXT("Escaped");Multiplayer.bSuccess=true;Result->ConfigureResult(Multiplayer);Render(Result,TEXT("MultiplayerEscapeResult"));
	Result->ReleaseSlateResources(true);
	FlushRenderingCommands();return true;
}
#endif
