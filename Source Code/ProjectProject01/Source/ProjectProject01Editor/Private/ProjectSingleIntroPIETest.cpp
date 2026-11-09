#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PlayerInteractionComponent.h"
#include "ProjectDFSDBranding.h"
#include "ProjectDFSDStartupIntro.h"
#include "ProjectProject01FrontendGameMode.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectSingleIntro.h"
#include "RenderingThread.h"
#include "Sound/SoundWaveProcedural.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"

namespace
{
	bool RendersCinematicBars(const TSharedRef<SWidget>& Mask, FIntPoint Size)
	{
		FWidgetRenderer Renderer(false);
		const FVector2D DrawSize(Size.X, Size.Y);
		auto* Target = FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false);
		const auto Frame = SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"))]
			+ SOverlay::Slot()[Mask];
		Renderer.DrawWidget(Target, Frame, DrawSize, 0.f);
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
			return false;
		const auto Pixel = [&](int32 X, int32 Y) { return Pixels[Y * Size.X + X]; };
		for (const int32 X : { 8, Size.X - 9 })
		{
			const int32 Edge = Size.Y * .07f;
			if (Pixel(X, Edge).R > 8 || Pixel(X, Size.Y - Edge - 1).R > 8 || Pixel(X, Size.Y / 2).R < 245)
				return false;
		}
		return true;
	}

	void CaptureIntroShot(UWorld* World, const FVector& Location, const FRotator& Rotation, const TCHAR* File)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("ReviewIntroFinale"))) return;
		auto* Capture = World->SpawnActor<ASceneCapture2D>(Location, Rotation);
		if (!Capture) return;
		auto* Component = Capture->GetCaptureComponent2D();
		auto* Target = NewObject<UTextureRenderTarget2D>(Capture);
		Target->InitCustomFormat(960, 540, PF_B8G8R8A8, false);
		Target->UpdateResourceImmediate(true);
		Component->TextureTarget = Target;
		Component->bCaptureEveryFrame = false;
		Component->bCaptureOnMovement = false;
		Component->CaptureSource = SCS_FinalColorLDR;
		Component->ShowFlags.SetMotionBlur(false);
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			Component->CaptureScene();
			FlushRenderingCommands();
		}
		TArray<FColor> Pixels;
		if (Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))
		{
			for (FColor& Pixel : Pixels) Pixel.A = 255;
			TArray64<uint8> PNG;
			FImageUtils::PNGCompressImageArray(960, 540, Pixels, PNG);
			FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectSavedDir() / TEXT("SingleIntroQA") / File));
		}
		Capture->Destroy();
	}

	void CaptureFinale(const ACameraActor& Camera, const TCHAR* File)
	{
		CaptureIntroShot(Camera.GetWorld(), Camera.GetActorLocation(), Camera.GetActorRotation(), File);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectSingleIntroPIETest, "ProjectProject01.Editor.SingleIntroFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSingleIntroPIETest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) return false;
	auto Request = UProjectProject01TutorialEditorLibrary::MakePracticeRequest();
	Request.GlobalMapOverride = TEXT("/Game/MyProject/Level/SinglePlayerIntro");
	Request.GameModeOverride = nullptr;
	GEditor->RequestPlaySession(Request);
	GEditor->StartQueuedPlaySessionRequest();

	struct FProgress
	{
		int32 Step = 0;
		float ResumeAt = 0;
		bool bCapturedLastLight = false;
		bool bCapturedWideShot = false;
		bool bCapturedLastAfterglow = false;
		bool bCheckedFirePhone = false;
		bool bCheckedInjuryMask = false;
		bool bCheckedHatShot = false;
		bool bCheckedOpeningBlack = false;
		bool bCheckedOpeningReveal = false;
		bool bCheckedHeldHat = false;
		bool bCheckedHatStow = false;
		bool bCheckedAlarmOrder = false;
		bool bSawTitleReveal = false;
		bool bSawPartiallyRevealedTitle = false;
		float PhoneWalkSpeed = 0.f;
		FRotator ApproachView = FRotator::ZeroRotator;
	};
	const auto State = MakeShared<FProgress>();
	const double Deadline = FPlatformTime::Seconds() + 130.;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, Deadline]()
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			AddError(FString::Printf(TEXT("Single intro timed out at step %d"), State->Step));
			GEditor->RequestEndPlayMap();
			return true;
		}
		UWorld* World = GEditor->PlayWorld;
		if (State->Step == 19)
		{
			if (!World || !World->GetAuthGameMode<AProjectProject01TitleGameMode>()) return false;
			TArray<UUserWidget*> Reveals;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Reveals, UProjectDFSDStartupIntro::StaticClass(), true);
			if (!Reveals.IsEmpty())
			{
				auto* Reveal = Reveals[0];
				const float Opacity = Reveal->GetRenderOpacity();
				if (!State->bSawTitleReveal)
				{
					State->bSawTitleReveal = true;
					bool bHasGameLogo = false;
					Reveal->WidgetTree->ForEachWidget([&bHasGameLogo](UWidget* Widget)
					{
						const auto* Label = Cast<UTextBlock>(Widget);
						bHasGameLogo |= Label && Label->GetVisibility() == ESlateVisibility::HitTestInvisible
							&& Label->GetText().ToString() == ProjectDFSDBranding::GameTitle();
					});
					TestTrue(TEXT("The loaded title starts covered by the same game logo"), bHasGameLogo && Opacity > .9f);
				}
				State->bSawPartiallyRevealedTitle |= Opacity > .1f && Opacity < .9f;
				return false;
			}
			TestTrue(TEXT("The ending gradually reveals the title and then dismisses its cover"),
				State->bSawTitleReveal && State->bSawPartiallyRevealedTitle
				&& IsValid(Cast<AProjectProject01TitlePlayerController>(World->GetFirstPlayerController())));
			GEditor->RequestEndPlayMap();
			return true;
		}
		auto* Mode = World ? World->GetAuthGameMode<AProjectSingleIntroGameMode>() : nullptr;
		if (!Mode || !Mode->Controller.IsValid() || !Mode->Interaction || !Mode->Overlay) return false;
		auto& PC = *Mode->Controller;
		auto& Pawn = *PC.GetPawn();
		auto AimAt = [&](const FVector& Position)
		{
			PC.SetControlRotation((Position - Pawn.GetPawnViewLocation()).Rotation());
		};
		auto Place = [&](const FVector& Location, const FVector& LookAt)
		{
			Pawn.SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			AimAt(LookAt);
			State->ResumeAt = World->GetTimeSeconds() + .22f;
		};
		if (World->GetTimeSeconds() < State->ResumeAt) return false;
		if (State->Step >= 20)
		{
			const FVector Route[] = {
				FVector(-2060,-1300,-354), FVector(-1730,-1300,-354),
				FVector(-1730,-2650,96), FVector(-1405,-2650,96)
			};
			const FVector Point = Route[State->Step-20];
			const FVector Delta = Point - Pawn.GetActorLocation();
			if (Delta.SizeSquared2D() > FMath::Square(42.f))
			{
				Pawn.AddMovementInput(Delta.GetSafeNormal2D(), .8f, true);
				return false;
			}
			TestTrue(TEXT("The character reached the physical landing at the expected floor height"), FMath::Abs(Delta.Z) < 42.f);
			if (++State->Step == 24)
			{
				Place(FVector(-525,-1810,96), FVector(-525,-1630,101));
				State->Step = 2;
			}
			return false;
		}
		using EPhase = AProjectSingleIntroGameMode::EPhase;
		switch (State->Step)
		{
		case 0:
			if (Mode->Phase == EPhase::Phone && Mode->Elapsed < .25f && !State->bCheckedOpeningBlack)
			{
				State->bCheckedOpeningBlack = true;
				TestEqual(TEXT("The opening camera starts fully covered"), Mode->GetOpeningFade(), 1.f);
			}
			if (Mode->Phase == EPhase::Phone && Mode->Elapsed >= 1.1f && !State->bCheckedOpeningReveal)
			{
				State->bCheckedOpeningReveal = true;
				const float Cover = Mode->GetOpeningFade();
				TestTrue(TEXT("The opening camera gradually becomes visible"), Cover > .2f && Cover < .8f);
			}
			if (Mode->Phase == EPhase::Phone && Mode->Elapsed >= 2.25f)
			{
				TestTrue(TEXT("The opening fade completes after starting black"),
					State->bCheckedOpeningBlack && State->bCheckedOpeningReveal && Mode->GetOpeningFade() == 0.f);
				TestTrue(TEXT("Phone overlay locks the player's movement and view"), PC.IsMoveInputIgnored() && PC.IsLookInputIgnored());
				TestTrue(TEXT("The opening tracks the player from low on their front-left"), Mode->OpeningCamera
					&& PC.GetViewTarget() == Mode->OpeningCamera
					&& FVector::Dist2D(Mode->OpeningCamera->GetActorLocation(), Pawn.GetActorLocation()) < 380.f
					&& Mode->OpeningCamera->GetActorLocation().X > Pawn.GetActorLocation().X
					&& Mode->OpeningCamera->GetActorLocation().Y > Pawn.GetActorLocation().Y);
				TestTrue(TEXT("The walking character holds the actual phone prop"), Mode->PhoneProp
					&& !Mode->PhoneProp->IsHidden() && Mode->PhoneDisplay && !Mode->PhoneDisplay->IsHidden());
				State->PhoneWalkSpeed = Pawn.GetVelocity().Size2D();
				TestTrue(TEXT("The character is walking steadily while holding the phone"),
					FMath::IsNearlyEqual(State->PhoneWalkSpeed, 96.f, 4.f));
				if (Mode->OpeningCamera) CaptureFinale(*Mode->OpeningCamera, TEXT("Opening_Tracking.png"));
				++State->Step;
			}
			break;
		case 1:
			if (Mode->Phase == EPhase::EnterStore && Mode->Elapsed >= 2.45f && !State->bCapturedWideShot)
			{
				State->bCapturedWideShot = true;
				TestTrue(TEXT("A separate wide shot frames the facade once the phone is put away"),
					Mode->OpeningCamera && PC.GetViewTarget() == Mode->OpeningCamera
					&& FVector::Dist2D(Mode->OpeningCamera->GetActorLocation(), Pawn.GetActorLocation()) > 1100.f
					&& FVector::DotProduct(Mode->OpeningCamera->GetActorForwardVector(), FVector::RightVector) > .85f
					&& Mode->PhoneProp && Mode->PhoneProp->IsHidden());
				TestTrue(TEXT("Putting the phone away preserves the opening walk speed"),
					FMath::IsNearlyEqual(Pawn.GetVelocity().Size2D(), State->PhoneWalkSpeed, 4.f));
				if (Mode->OpeningCamera) CaptureFinale(*Mode->OpeningCamera, TEXT("Opening_Wide.png"));
			}
			if (Mode->Phase == EPhase::Shopping)
			{
				TestTrue(TEXT("Shopping returns to the player's camera and unlocks input"), PC.GetViewTarget() == &Pawn
					&& !PC.IsMoveInputIgnored() && !PC.IsLookInputIgnored());
				TestTrue(TEXT("The player walked through the entrance after putting away the phone"),
					FVector::DistSquared(Pawn.GetActorLocation(), Mode->OpeningDestination->GetActorLocation()) < FMath::Square(100.f));
				TestTrue(TEXT("The exterior entrance opens on the first floor, below the shopping aisle"), Pawn.GetActorLocation().Z < -700.f);
				Place(FVector(-2060,-2630,-804), FVector(-2060,-1430,-354));
				State->Step = 20;
			}
			break;
		case 2:
			Mode->Interaction->TryInteract();
			TestTrue(TEXT("Using a shelf updates the optional purchase list"), Mode->PickedItems.Contains(TEXT("IntroHandkerchief")));
			Place(Mode->Hat->GetActorLocation() + FVector(0,-171,-22), Mode->Hat->GetActorLocation());
			++State->Step;
			break;
		case 3:
			TestTrue(TEXT("The enlarged shop ends at the hat counter and its partner"),
				Mode->Phase == EPhase::Shopping && Mode->Hat->GetActorLocation().Y > Mode->Mannequin->GetActorLocation().Y
				&& FVector::Dist2D(Mode->Hat->GetActorLocation(), Mode->Partner->GetActorLocation()) < 420.f);
			Mode->Interaction->TryInteract();
			TestTrue(TEXT("Reaching for the hat starts its cinematic"), Mode->Phase == EPhase::InspectHat);
			++State->Step;
			break;
		case 4:
			if (Mode->Phase == EPhase::InspectHat && Mode->Elapsed >= 1.45f && !State->bCheckedHatShot)
			{
				State->bCheckedHatShot = true;
				TestTrue(TEXT("The hat reach uses a cinematic camera and an animated arm at the counter"),
					Mode->HatCamera && PC.GetViewTarget() == Mode->HatCamera && Mode->HatReachMesh
					&& Mode->HatReachMesh->GetOwner() == &Pawn && !Pawn.IsHidden()
					&& Mode->Hat->GetActorLocation().Equals(Mode->HatStart, .5f));
				if (Mode->HatCamera) CaptureFinale(*Mode->HatCamera, TEXT("Hat_Reach.png"));
			}
			if (Mode->Phase == EPhase::Fire && Mode->Elapsed >= 1.10f && !State->bCheckedHeldHat)
			{
				State->bCheckedHeldHat = true;
				const auto& View = PC.PlayerCameraManager->GetCameraCacheView();
				const FVector ToHat = Mode->Hat->GetActorLocation() - View.Location;
				TestTrue(TEXT("The shutter lands while the hat is still in hand in first person"),
					PC.GetViewTarget() == &Pawn && !Mode->Hat->IsHidden() && Mode->HatReachMesh
					&& !Mode->Hat->GetActorLocation().Equals(Mode->HatStart, 10.f)
					&& ToHat.Size() < 110.f && FVector::DotProduct(ToHat.GetSafeNormal(), View.Rotation.Vector()) > .6f
					&& Mode->ShutterDropSound && !Mode->Alarm && Mode->TurnDuration == 0.f
					&& Mode->GetSubtitle().ToString() == TEXT("무슨 일이지?"));
				CaptureIntroShot(World, View.Location, View.Rotation, TEXT("Hat_Held_FirstPerson.png"));
			}
			if (Mode->Phase == EPhase::Fire && Mode->Elapsed >= 2.40f && !State->bCheckedHatStow)
			{
				State->bCheckedHatStow = true;
				TestTrue(TEXT("The hat is put away before the player turns or the alarm sounds"),
					Mode->Hat->IsHidden() && !Mode->HatReachMesh && Mode->TurnDuration == 0.f && !Mode->Alarm);
			}
			if (Mode->Phase == EPhase::Fire && Mode->Alarm && !State->bCheckedAlarmOrder)
			{
				State->bCheckedAlarmOrder = true;
				TestTrue(TEXT("The alarm starts after the turn toward the closed shutter"),
					State->bCheckedHeldHat && State->bCheckedHatStow && Mode->TurnDuration > 0.f
					&& PC.GetControlRotation().Equals((Mode->TurnStart + Mode->TurnDelta).GetNormalized(), 1.f)
					&& Mode->GetSubtitle().ToString().StartsWith(TEXT("화재가 발생했습니다.")));
				if (FParse::Param(FCommandLine::Get(), TEXT("ReviewIntroFinale")))
					TestTrue(TEXT("The fire warning has black bars across the top and bottom of the screen"),
						RendersCinematicBars(Mode->BuildCinematicMask(), FIntPoint(480, 270)));
			}
			if (Mode->Phase == EPhase::Fire && Mode->Elapsed >= 10.7f && !State->bCheckedFirePhone)
			{
				State->bCheckedFirePhone = true;
				TestTrue(TEXT("The player lifts a physical phone with its screen off during the cutscene"),
					Mode->PhoneProp && !Mode->PhoneProp->IsHidden() && Mode->PhoneDisplay && Mode->PhoneDisplay->IsHidden()
					&& PC.GetViewTarget() == Mode->OpeningCamera);
				if (Mode->OpeningCamera) CaptureFinale(*Mode->OpeningCamera, TEXT("Fire_Phone.png"));
			}
			if (Mode->Phase == EPhase::Meet)
			{
				TestTrue(TEXT("The shutter descended by the full door height"),
					Mode->Shutter->GetActorLocation().Equals(Mode->ShutterOpen - FVector(0,0,Mode->ShutterDrop), .5f));
				TestTrue(TEXT("The fire sequence returns the camera and puts away the phone"),
					PC.GetViewTarget() == &Pawn && Mode->PhoneProp && Mode->PhoneProp->IsHidden());
				Place(Mode->Partner->GetActorLocation() + FVector(0,-166,96), Mode->Partner->GetActorLocation() + FVector(0,0,125));
				++State->Step;
			}
			break;
		case 5:
			Mode->Interaction->TryInteract();
			TestTrue(TEXT("The player talks to the partner through the normal interaction component"), Mode->Phase == EPhase::Talk);
			++State->Step;
			break;
		case 6:
			if (Mode->Phase == EPhase::LeaveTogether)
			{
				Place(FVector(-900,850,96), FVector(-900,-550,140));
				++State->Step;
			}
			break;
		case 7:
			if (Mode->Phase == EPhase::WalkToShutter)
			{
				Place(FVector(-900,548,96), FVector(-900,-550,140));
				++State->Step;
			}
			break;
		case 8:
			TestTrue(TEXT("Passing the mannequin does not lock the view or force the player to look"),
				Mode->Phase == EPhase::WalkToShutter && PC.GetViewTarget() == &Pawn && !PC.IsLookInputIgnored());
			State->ApproachView = PC.GetControlRotation();
			Pawn.SetActorLocation(FVector(-900,310,96), false, nullptr, ETeleportType::TeleportPhysics);
			TestTrue(TEXT("Crossing the trigger after the mannequin immediately starts the injury scene"),
				Mode->Phase == EPhase::PartnerInjured && Mode->PartnerScream && PC.IsLookInputIgnored()
				&& PC.GetControlRotation().Equals(State->ApproachView, 1.f));
			++State->Step;
			break;
		case 9:
			if (FParse::Param(FCommandLine::Get(), TEXT("ReviewIntroFinale"))
				&& Mode->Phase == EPhase::PartnerInjured && Mode->Elapsed >= .3f && !State->bCheckedInjuryMask)
			{
				State->bCheckedInjuryMask = true;
				TestTrue(TEXT("The scream has black bars across the top and bottom of the screen"),
					RendersCinematicBars(Mode->BuildCinematicMask(), FIntPoint(640, 480)));
			}
			if (Mode->Phase == EPhase::PartnerInjured && Mode->Elapsed >= 1.1f)
			{
				TestTrue(TEXT("The partner reacts and the player asks if they are okay"),
					Mode->Partner->GetActorRotation().Pitch < -10.f && Mode->GetSubtitle().ToString() == TEXT("괜찮아요?"));
				TestTrue(TEXT("The camera turns toward the scream gradually without roll"),
					FMath::Abs((PC.GetControlRotation() - State->ApproachView).GetNormalized().Yaw) < 95.f
					&& FMath::Abs(PC.GetControlRotation().Roll) < 1.f);
				State->Step = 14;
			}
			break;
		case 14:
			if (Mode->Phase == EPhase::ReadyToLeave)
			{
				TestTrue(TEXT("The corridor crowd stays hidden until the shutter scene"),
					Mode->FinaleCrowd.Num() > Mode->FinaleLights.Num() && Mode->FinaleCrowd[0]->IsHidden());
				TestTrue(TEXT("Controls and interior lighting remain available before the blackout"),
					!PC.IsLookInputIgnored() && !PC.IsMoveInputIgnored() && !Mode->StoreLights.IsEmpty()
					&& Mode->StoreLights[0]->IsVisible());
				Place(FVector(-885,-695,96), Mode->ShutterHandle->GetActorLocation());
				++State->Step;
			}
			break;
		case 15:
			Mode->Interaction->TryInteract();
			TestTrue(TEXT("The original shutter starts the final cinematic"), Mode->Phase == EPhase::Finale);
			++State->Step;
			break;
		case 16:
			if (Mode->Phase == EPhase::Finale && Mode->Elapsed >= 2.25f)
			{
				TestTrue(TEXT("An authored camera frames the fully open shutter and corridor"), PC.GetViewTarget() == Mode->FinaleCamera
					&& Mode->Shutter->GetActorLocation().Equals(Mode->ShutterOpen, 1.f));
				TestTrue(TEXT("The authored shutter shot only uses the shared cinematic bars"),
					!PC.PlayerCameraManager->GetCameraCacheView().bConstrainAspectRatio);
				for (const auto& Figure : Mode->FinaleCrowd)
					TestFalse(TEXT("The cluster of mannequins is visible"), Figure->IsHidden());
				for (const auto& Light : Mode->FinaleLights)
					TestTrue(TEXT("Each corridor light starts on"), Light->IsVisible());
				for (const auto& Diffuser : Mode->FinaleDiffusers)
					TestTrue(TEXT("Each fixture has a visible diffuser"), Diffuser && !Diffuser->IsHidden());
				CaptureFinale(*Mode->FinaleCamera, TEXT("Outage_AllLights.png"));
				if (const auto* FixtureLens = Mode->FinaleDiffusers.Last().Get())
				{
					const FVector Lens = FixtureLens->GetActorLocation();
					const FVector Detail = Lens + FVector(-11, 60, -42);
					CaptureIntroShot(World, Detail, (Lens - Detail).Rotation(), TEXT("Recessed_Downlight.png"));
				}
				++State->Step;
			}
			break;
		case 17:
			if (Mode->NextFinaleLight > 0)
			{
				TestTrue(TEXT("The far light drops to an afterglow first and the near light remains fully on"),
					Mode->NextFinaleLight == 1 && Mode->FinaleLights[0]->IsVisible()
					&& Mode->FinaleLights[0]->Intensity > 0.f && Mode->FinaleLights[0]->Intensity < Mode->FinaleLightIntensities[0] * .1f
					&& FMath::IsNearlyEqual(Mode->FinaleLights.Last()->Intensity, Mode->FinaleLightIntensities.Last())
					&& FVector::DistSquared2D(Mode->FinaleLights[0]->GetComponentLocation(), Mode->ShutterOpen)
					> FVector::DistSquared2D(Mode->FinaleLights.Last()->GetComponentLocation(), Mode->ShutterOpen));
				TestTrue(TEXT("The far light's diffuser turns off with its light source"),
					Mode->FinaleDiffusers[0] && Mode->FinaleDiffusers[0]->IsHidden() && !Mode->FinaleDiffusers.Last()->IsHidden());
				TestFalse(TEXT("The other department lights turn off only at the blackout"), Mode->StoreLights[0]->IsVisible());
				for (const auto& Light : Mode->StoreLights)
				{
					TArray<AActor*> Attached;
					Light->GetOwner()->GetAttachedActors(Attached);
					for (const auto* Part : Attached)
						if (Part->ActorHasTag(TEXT("IntroDownlightLens")))
							TestTrue(TEXT("Department lenses also go dark during the blackout"), Part->IsHidden());
				}
				CaptureFinale(*Mode->FinaleCamera, TEXT("Outage_FarthestOff.png"));
				const auto& Times = Mode->FinaleOutageTimes;
				TestTrue(TEXT("Early outages accelerate and the final lamp holds for over two seconds"),
					Times.Num() >= 5 && Times[1] - Times[0] > Times[2] - Times[1]
					&& Times[2] - Times[1] > Times[3] - Times[2] && Times.Last() - Times[Times.Num()-2] >= 2.f);
				++State->Step;
			}
			break;
		case 18:
			if (!State->bCapturedLastLight && Mode->NextFinaleLight == Mode->FinaleLights.Num()-1)
			{
				State->bCapturedLastLight = true;
				CaptureFinale(*Mode->FinaleCamera, TEXT("Outage_LastLight.png"));
			}
			if (!State->bCapturedLastAfterglow && Mode->NextFinaleLight == Mode->FinaleLights.Num())
			{
				State->bCapturedLastAfterglow = true;
				TestTrue(TEXT("The last lamp briefly keeps its afterglow before the title fade"),
					Mode->FinaleLights.Last()->IsVisible() && Mode->FinaleLights.Last()->Intensity > 0.f
					&& Mode->FinaleLights.Last()->Intensity < Mode->FinaleLightIntensities.Last() * .1f);
				CaptureFinale(*Mode->FinaleCamera, TEXT("Outage_Afterglow.png"));
			}
			if (Mode->Phase == EPhase::TitleCard)
			{
				for (const auto& Light : Mode->FinaleLights)
					TestFalse(TEXT("The tutorial ends after every corridor light goes out"), Light->IsVisible());
				++State->Step;
			}
			break;
		default:
			break;
		}
		return false;
	}));
	return true;
}
#endif
