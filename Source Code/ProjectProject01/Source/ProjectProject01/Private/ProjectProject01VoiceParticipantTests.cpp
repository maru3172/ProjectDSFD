#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01VoiceParticipantControls.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "ProjectProject01VoiceSettingsWidget.h"
#include "ProjectProject01LoginWidget.h"
#include "Engine/GameInstance.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"
#include "RenderingThread.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectVoiceParticipantControlsTest,"ProjectProject01.Multiplayer.SurvivorVoice.LocalParticipantControls",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectVoiceParticipantControlsTest::RunTest(const FString&)
{
	FProjectVoiceParticipantControls Controls;
	const TArray<FString> Peers={TEXT("peer-a"),TEXT("peer-b")};
	TestTrue(TEXT("Current peer may be muted locally"),Controls.SetMuted(TEXT("peer-a"),TEXT("self"),Peers,true));
	TestTrue(TEXT("Current peer volume may be lowered"),Controls.SetVolume(TEXT("peer-a"),TEXT("self"),Peers,0.35f));
	TestEqual(TEXT("Gain is retained independently from mute"),Controls.GetVolume(TEXT("peer-a")),0.35f);
	TestFalse(TEXT("Another peer is not muted"),Controls.Muted.Contains(TEXT("peer-b")));
	TestEqual(TEXT("Another peer keeps default volume"),Controls.GetVolume(TEXT("peer-b")),1.0f);
	TestTrue(TEXT("Mute may be removed without resetting gain"),Controls.SetMuted(TEXT("peer-a"),TEXT("self"),Peers,false));
	TestEqual(TEXT("Unmute restores chosen gain, not full volume"),Controls.GetVolume(TEXT("peer-a")),0.35f);
	TestFalse(TEXT("Unknown channel member cannot be controlled"),Controls.SetVolume(TEXT("outsider"),TEXT("self"),Peers,0.0f));
	TestFalse(TEXT("Self cannot be muted even if provider lists it"),Controls.SetMuted(TEXT("peer-a"),TEXT("peer-a"),Peers,true));
	TestFalse(TEXT("Empty identifier is rejected"),Controls.SetMuted(TEXT(""),TEXT("self"),Peers,true));
	TestFalse(TEXT("NaN is rejected without corrupting gain"),Controls.SetVolume(TEXT("peer-a"),TEXT("self"),Peers,std::numeric_limits<float>::quiet_NaN()));
	TestFalse(TEXT("Infinity is rejected"),Controls.SetVolume(TEXT("peer-a"),TEXT("self"),Peers,std::numeric_limits<float>::infinity()));
	TestEqual(TEXT("Invalid gain preserves previous value"),Controls.GetVolume(TEXT("peer-a")),0.35f);
	Controls.SetVolume(TEXT("peer-b"),TEXT("self"),Peers,9.0f);
	TestEqual(TEXT("No amplification above 100 percent"),Controls.GetVolume(TEXT("peer-b")),1.0f);
	Controls.SetVolume(TEXT("peer-b"),TEXT("self"),Peers,-5.0f);
	TestEqual(TEXT("Gain lower bound is zero"),Controls.GetVolume(TEXT("peer-b")),0.0f);
	TestEqual(TEXT("Both adjusted participants can be reset in provider"),Controls.GetAdjustedIds().Num(),2);
	Controls.Reset();
	TestTrue(TEXT("Reset clears all local overrides"),Controls.GetAdjustedIds().IsEmpty());
	FProjectVoiceParticipantState State;
	TestEqual(TEXT("Idle is not inferred from a voice hotkey"),State.GetStatusLabel(),FString(TEXT("대기")));
	State.bTalking=true;TestEqual(TEXT("Detected voice indicates actual speech"),State.GetStatusLabel(),FString(TEXT("말하는 중")));
	State.Volume=0;TestEqual(TEXT("Zero volume remains distinguishable from mute"),State.GetStatusLabel(),FString(TEXT("수신 음량 0%")));
	State.bMuted=true;TestEqual(TEXT("Local mute takes display priority"),State.GetStatusLabel(),FString(TEXT("음소거됨")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectVoiceParticipantIsolationTest,"ProjectProject01.Multiplayer.SurvivorVoice.ParticipantIsolationAndCleanup",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectVoiceParticipantIsolationTest::RunTest(const FString&)
{
	auto* Instance=NewObject<UGameInstance>();
	auto* Service=NewObject<UProjectProject01VoiceChatSubsystem>(Instance);
	Service->LocalUserId=TEXT("self");Service->MatchId=TEXT("test-match");Service->ChannelName=TEXT("survivor-test");
	Service->ParticipantControls.Muted.Add(TEXT("peer"));Service->ParticipantControls.Volumes.Add(TEXT("peer"),0.3f);
	TestTrue(TEXT("Unconfigured/mannequin path exposes no participants"),Service->GetVoiceParticipantStates().IsEmpty());
	Service->SetParticipantVolume(TEXT("peer"),0.9f);Service->SetParticipantMuted(TEXT("peer"),false);
	TestEqual(TEXT("No provider/channel means no mutation"),Service->GetParticipantVolume(TEXT("peer")),0.3f);
	TestTrue(TEXT("No provider cannot unmute another identity"),Service->IsParticipantMuted(TEXT("peer")));
	Service->bConfiguredForSurvivor=true;Service->bJoinedChannel=true;
	TestTrue(TEXT("Ready flags alone never expose fabricated participants"),Service->GetVoiceParticipantStates().IsEmpty());
	Service->LeaveMatch();
	TestTrue(TEXT("Leaving clears local receive preferences"),Service->ParticipantControls.GetAdjustedIds().IsEmpty());
	TestFalse(TEXT("Leaving disables survivor communication"),Service->IsConfiguredForSurvivor());
	TestFalse(TEXT("Leaving clears channel readiness"),Service->IsVoiceReady());
	TestTrue(TEXT("Leaving clears channel identity"),Service->ChannelName.IsEmpty()&&Service->LocalUserId.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectVoiceParticipantWidgetTest,"ProjectProject01.Operations.VoiceParticipantWidgets",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectVoiceParticipantWidgetTest::RunTest(const FString&)
{
	UUserWidget* Widget=NewObject<UProjectProject01VoiceSettingsWidget>();
	if(!TestTrue(TEXT("Settings tree initializes without EOS or a microphone"),Widget->Initialize()))return false;
	auto* Settings=CastChecked<UProjectProject01VoiceSettingsWidget>(Widget);
	TestFalse(TEXT("No peer disables gain slider"),Settings->ParticipantVolume->GetIsEnabled());
	TestFalse(TEXT("No peer disables mute button"),Settings->ParticipantMuteButton->GetIsEnabled());
	Settings->ParticipantVolumeChanged(0.4f);Settings->ToggleMute();Settings->RefreshParticipants();
	TestFalse(TEXT("Stale/no-peer callbacks remain harmless"),Settings->ParticipantVolume->GetIsEnabled());
	UUserWidget* HUDBase=NewObject<UProjectProject01VoiceStatusWidget>();HUDBase->Initialize();
	auto* HUD=CastChecked<UProjectProject01VoiceStatusWidget>(HUDBase);HUD->RefreshVoiceStatus();
	TestTrue(TEXT("No survivor service hides entire voice panel"),HUD->StatusBackground->GetVisibility()==ESlateVisibility::Collapsed);
	TestTrue(TEXT("No survivor service leaves no peer speech data"),HUD->TeammateVoiceText->GetText().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectVoiceParticipantRenderTest,"ProjectProject01.Operations.VoiceParticipantRender",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectVoiceParticipantRenderTest::RunTest(const FString&)
{
	const FString Directory=FPaths::ProjectSavedDir()/TEXT("VoiceParticipantTests");
	IFileManager::Get().MakeDirectory(*Directory,true);
	for(const FVector2D Size:{FVector2D(1280,720),FVector2D(800,600)})
	{
		UUserWidget* Widget=NewObject<UProjectProject01VoiceSettingsWidget>();Widget->Initialize();
		TSharedRef<SWidget> Slate=Widget->TakeWidget();
		// No game world is running here; exercise the same responsive layout update explicitly.
		CastChecked<UProjectProject01VoiceSettingsWidget>(Widget)->NativeTick(FGeometry::MakeRoot(Size,FSlateLayoutTransform()),0.016f);
		FWidgetRenderer Renderer(true);
		UTextureRenderTarget2D* Target=Renderer.DrawWidget(Slate,Size);
		Renderer.DrawWidget(Target,Slate,Size,0.016f);FlushRenderingCommands();
		FBufferArchive Image;
		TestTrue(TEXT("Voice settings render to PNG"),FImageUtils::ExportRenderTarget2DAsPNG(Target,Image));
		const FString Name=FString::Printf(TEXT("VoiceSettings_%dx%d.png"),FMath::RoundToInt(Size.X),FMath::RoundToInt(Size.Y));
		TestTrue(TEXT("Visual inspection artifact is written"),FFileHelper::SaveArrayToFile(Image,*(Directory/Name)));
		TArray<UWidget*> Children;Widget->WidgetTree->GetAllWidgets(Children);
		bool bFoundPeerSlider=false;
		for(UWidget* Child:Children)
			if(auto* Slider=Cast<USlider>(Child);Slider && !Slider->GetIsEnabled())
			{
				bFoundPeerSlider=true;
				const FVector2D Position=Slider->GetCachedGeometry().GetAbsolutePosition();
				const FVector2D Extent=Slider->GetCachedGeometry().GetLocalSize();
				TestTrue(TEXT("Peer volume gauge has useful width"),Extent.X>200);
				TestTrue(TEXT("Peer volume gauge remains inside the viewport"),Position.X>=0&&Position.X+Extent.X<=Size.X+1&&Position.Y>=0&&Position.Y+Extent.Y<=Size.Y+1);
			}
		TestTrue(TEXT("Rendered peer control is present"),bFoundPeerSlider);
		Widget->ReleaseSlateResources(true);FlushRenderingCommands();
	}
	return true;
}
#endif
