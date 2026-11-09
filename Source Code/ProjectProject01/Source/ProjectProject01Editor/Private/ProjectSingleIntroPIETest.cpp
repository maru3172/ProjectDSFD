#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraActor.h"
#include "Components/LightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
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
#include "ProjectProject01FrontendGameMode.h"
#include "ProjectProject01TutorialEditorLibrary.h"
#include "ProjectSingleIntro.h"
#include "RenderingThread.h"

namespace
{
	void CaptureFinale(const ACameraActor& Camera, const TCHAR* File)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("ReviewIntroFinale"))) return;
		UWorld* World = Camera.GetWorld();
		auto* Capture = World->SpawnActor<ASceneCapture2D>(Camera.GetActorLocation(), Camera.GetActorRotation());
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
		FVector WatchedLocation = FVector::ZeroVector;
	};
	const auto State = MakeShared<FProgress>();
	const double Deadline = FPlatformTime::Seconds() + 100.;
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
			TestTrue(TEXT("The ending returns to the title automatically"),
				IsValid(Cast<AProjectProject01TitlePlayerController>(World->GetFirstPlayerController())));
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
			if (Mode->Phase == EPhase::Phone && Mode->Elapsed >= .75f)
			{
				TestTrue(TEXT("Phone overlay locks the player's movement and view"), PC.IsMoveInputIgnored() && PC.IsLookInputIgnored());
				TestTrue(TEXT("The opening uses a low external tracking camera"), Mode->OpeningCamera
					&& PC.GetViewTarget() == Mode->OpeningCamera
					&& FVector::Dist2D(Mode->OpeningCamera->GetActorLocation(), Pawn.GetActorLocation()) < 380.f);
				if (Mode->OpeningCamera) CaptureFinale(*Mode->OpeningCamera, TEXT("Opening_Tracking.png"));
				++State->Step;
			}
			break;
		case 1:
			if (Mode->Phase == EPhase::Phone && Mode->Elapsed >= 5.2f && !State->bCapturedWideShot)
			{
				State->bCapturedWideShot = true;
				TestTrue(TEXT("The camera pulls away to frame the facade without looking back"),
					Mode->OpeningCamera && PC.GetViewTarget() == Mode->OpeningCamera
					&& FVector::Dist2D(Mode->OpeningCamera->GetActorLocation(), Pawn.GetActorLocation()) > 1100.f
					&& FVector::DotProduct(Mode->OpeningCamera->GetActorForwardVector(), FVector::RightVector) > .85f);
				if (Mode->OpeningCamera) CaptureFinale(*Mode->OpeningCamera, TEXT("Opening_Wide.png"));
			}
			if (Mode->Phase == EPhase::Shopping)
			{
				TestTrue(TEXT("Shopping returns to the player's camera and unlocks input"), PC.GetViewTarget() == &Pawn
					&& !PC.IsMoveInputIgnored() && !PC.IsLookInputIgnored());
				TestTrue(TEXT("The player walked through the entrance while the phone was up"),
					FVector::DistSquared(Pawn.GetActorLocation(), Mode->OpeningDestination->GetActorLocation()) < FMath::Square(100.f));
				TestTrue(TEXT("The exterior entrance opens on the first floor, below the shopping aisle"), Pawn.GetActorLocation().Z < -700.f);
				Place(FVector(-2060,-2630,-804), FVector(-2060,-1430,-354));
				State->Step = 20;
			}
			break;
		case 2:
			Mode->Interaction->TryInteract();
			TestTrue(TEXT("Using a shelf updates the optional purchase list"), Mode->PickedItems.Contains(TEXT("IntroHandkerchief")));
			Place(Mode->HatCounter->GetActorLocation() + FVector(0,0,-14), Mode->Hat->GetActorLocation());
			++State->Step;
			break;
		case 3:
			TestTrue(TEXT("Arriving at the back counter starts the scripted inspection automatically"), Mode->Phase == EPhase::InspectHat);
			++State->Step;
			break;
		case 4:
			if (Mode->Phase == EPhase::Meet)
			{
				TestTrue(TEXT("The shutter descended by the full door height"),
					Mode->Shutter->GetActorLocation().Equals(Mode->ShutterOpen - FVector(0,0,Mode->ShutterDrop), .5f));
				Place(FVector(-810,-295,96), Mode->Partner->GetActorLocation() + FVector(0,0,125));
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
				Place(FVector(-900,80,96), FVector(-900,1500,140));
				++State->Step;
			}
			break;
		case 7:
			if (Mode->Phase == EPhase::WalkToShutter)
			{
				Place(FVector(-900,825,96), FVector(-900,1500,140));
				++State->Step;
			}
			break;
		case 8:
			if (Mode->Phase == EPhase::NoticeMannequin && Mode->Elapsed >= .35f)
			{
				TestTrue(TEXT("The encounter uses its own shot before returning camera control"),
					Mode->EncounterCamera && PC.GetViewTarget() == Mode->EncounterCamera && PC.IsLookInputIgnored());
				++State->Step;
			}
			break;
		case 9:
			if (Mode->Phase == EPhase::PassedMannequin)
			{
				TestTrue(TEXT("The player can look and move again after the encounter shot"),
					PC.GetViewTarget() == &Pawn && !PC.IsLookInputIgnored());
				Place(FVector(-900,1130,96), Mode->Mannequin->GetActorLocation() + FVector(0,0,125));
				State->WatchedLocation = Mode->Mannequin->GetActorLocation();
				++State->Step;
			}
			break;
		case 10:
			if (Mode->Phase == EPhase::ApproachPartner)
			{
				TestTrue(TEXT("The approach starts at least five seconds after the encounter"), World->GetTimeSeconds() >= Mode->EncounterAt + 5.);
				State->ResumeAt = World->GetTimeSeconds() + .4f;
				++State->Step;
			}
			break;
		case 11:
			TestTrue(TEXT("Looking at the mannequin prevents its approach"),
				Mode->Phase == EPhase::ApproachPartner && Mode->Mannequin->GetActorLocation().Equals(State->WatchedLocation, 1.f));
			AimAt(FVector(-900,1500,140));
			++State->Step;
			break;
		case 12:
			if (Mode->Phase == EPhase::Captured)
			{
				TestTrue(TEXT("The mannequin reaches the partner when the player looks away"), Mode->Partner->GetActorRotation().Pitch < -10.f);
				TestFalse(TEXT("The partner cannot be rescued while the mannequin is outside the player's gaze"),
					Mode->CanUse(*Mode->Partner, &Pawn));
				const FVector Partner = Mode->Partner->GetActorLocation();
				Place(FVector(Partner.X, Partner.Y+168,96), Partner + FVector(0,0,105));
				++State->Step;
			}
			break;
		case 13:
			Mode->Interaction->TryInteract();
			TestTrue(TEXT("Looking and interacting frees the partner"), Mode->Phase == EPhase::AfterRescue);
			++State->Step;
			break;
		case 14:
			if (Mode->Phase == EPhase::ReadyToLeave)
			{
				TestTrue(TEXT("The corridor crowd stays hidden until the shutter scene"),
					Mode->FinaleCrowd.Num() > Mode->FinaleLights.Num() && Mode->FinaleCrowd[0]->IsHidden());
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
				for (const auto& Figure : Mode->FinaleCrowd)
					TestFalse(TEXT("The cluster of mannequins is visible"), Figure->IsHidden());
				for (const auto& Light : Mode->FinaleLights)
					TestTrue(TEXT("Each corridor light starts on"), Light->IsVisible());
				for (const auto& Diffuser : Mode->FinaleDiffusers)
					TestTrue(TEXT("Each fixture has a visible diffuser"), Diffuser && !Diffuser->IsHidden());
				CaptureFinale(*Mode->FinaleCamera, TEXT("Outage_AllLights.png"));
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
