#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FireShutterActor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "MannequinReactiveLight.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ProjectProject01RoleSpawnPoint.h"
#include "ProjectShutterLayout.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "Tests/AutomationEditorCommon.h"

/**
 * What the match maps look like with their lights and darkness (Docs/FireShutterLayout.md, lighting): eye-height views from
 * survivor starts toward the nearest way out, plus a view from above. Shutters show one rolled match (raised ones hidden).
 * Pictures: Saved/ShutterLayoutQA/Look_<Map>_<n>.png. Also measures how much of the picture is lit, so "too dark to play"
 * and "not dark at all" both show up as numbers.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMatchMapLookRender, "ProjectProject01.Editor.MatchMapLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectMatchMapLookRender::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsPlaySessionInProgress()) return false;
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("ShutterLayoutQA");
	IFileManager::Get().MakeDirectory(*Dir, true);
	for (const TCHAR* Level : { TEXT("MultiplayTest"), TEXT("MultiplayTest1"), TEXT("MultiplayTest2") })
	{
		const FString Package = TEXT("/Game/MyProject/Level/") + FString(Level);
		if (!FPackageName::DoesPackageExist(Package)) continue;
		FAutomationEditorCommonUtils::LoadMap(Package);
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!TestTrue(Package + TEXT(" loaded"), World && World->GetOutermost()->GetName() == Package)) continue;

		// One rolled match: hide the shutters that are up this match so the views are not walls of shutters.
		TArray<TWeakObjectPtr<AActor>> Hidden;
		if (AProjectShutterLayout* Layout = [&]() -> AProjectShutterLayout* { for (TActorIterator<AProjectShutterLayout> It(World); It; ++It) return *It; return nullptr; }())
		{
			const AProjectShutterLayout::FPlacement Placement = Layout->Collect();
			const FShutterLayoutResult R = ProjectShutterLayout::Generate(Placement.Graph, Placement.Settings, 7);
			const FShutterRuin Ruin = ProjectShutterLayout::RollRuin(Placement.Graph, R, Layout->RuinSettings(), 7);
			for (int32 S = 0; S < Placement.SlotShutters.Num(); ++S)
				if (Ruin.Slots.IsValidIndex(S) && Ruin.Slots[S].Look == EShutterLook::Tucked) Hidden.Add(Placement.SlotShutters[S]);
			for (AFireShutterActor* Shutter : Placement.OffLinkShutters) Hidden.Add(Shutter);
		}

		TArray<FVector> Starts, Exits;
		for (TActorIterator<AProjectProject01RoleSpawnPoint> It(World); It; ++It)
			if (It->GetSpawnRole() == EProjectProject01SpawnRole::Survivor) Starts.Add(It->GetActorLocation());
		int32 Fixtures = 0, Lit = 0;
		const FFloatProperty* OnIntensity = FindFProperty<FFloatProperty>(AMannequinReactiveLight::StaticClass(), TEXT("LightOnIntensity"));
		for (TActorIterator<AMannequinReactiveLight> It(World); It; ++It)
		{
			++Fixtures;
			if (It->GetActorLabel().EndsWith(TEXT("exit"))) Exits.Add(It->GetActorLocation());
			Lit += OnIntensity && OnIntensity->GetPropertyValue_InContainer(*It) > 0.f ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("%s: %d fixtures, %d lit, %d over ways out"), Level, Fixtures, Lit, Exits.Num()));

		// No fixture sticks into a shutter (layout shutters reach the ceiling).
		int32 Stuck = 0;
		for (TActorIterator<AMannequinReactiveLight> L(World); L; ++L)
			for (TActorIterator<AFireShutterActor> S(World); S; ++S)
			{
				if (S->GetLayoutRole() == EProjectShutterLayoutRole::None) continue;
				const FVector2D C(S->GetActorLocation()), Half = FVector2D(S->GetActorRightVector()).GetSafeNormal() * (150.f * FMath::Abs(S->GetActorScale3D().Y));
				const FVector2D P(L->GetActorLocation()), AB = Half * 2.f, A = C - Half;
				const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(1.0, AB.SizeSquared()), 0., 1.);
				Stuck += FVector2D::Distance(P, A + AB * T) < 150.f ? 1 : 0;
			}
		TestEqual(FString::Printf(TEXT("%s: no light fixture sticks into a shutter"), Level), Stuck, 0);

		// No fixture sticks into the modelled walls: nothing solid within 80 cm around it at its own height.
		FAssetCompilingManager::Get().FinishAllCompilation();
		int32 InWall = 0;
		for (TActorIterator<AMannequinReactiveLight> L(World); L; ++L)
		{
			const FVector At = L->GetActorLocation() - FVector(0, 0, 20);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(LightWall), true, *L);
			for (int32 D = 0; D < 8; ++D)
			{
				FHitResult Hit;
				const FVector Around = FRotator(0, D * 45.f, 0).Vector();
				if (World->LineTraceSingleByChannel(Hit, At, At + Around * 80.f, ECC_Visibility, Params) && !Cast<AFireShutterActor>(Hit.GetActor())) { ++InWall; break; }
			}
		}
		TestEqual(FString::Printf(TEXT("%s: no light fixture sticks into a wall"), Level), InWall, 0);

		// Meshes and textures build asynchronously after loading; a capture before that shows an empty map.
		FAssetCompilingManager::Get().FinishAllCompilation();
		ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>();
		USceneCaptureComponent2D* Comp = Capture->GetCaptureComponent2D();
		Comp->bCaptureEveryFrame = false; Comp->bCaptureOnMovement = false; Comp->CaptureSource = SCS_FinalColorLDR;
		Comp->bAlwaysPersistRenderingState = true; Comp->FOVAngle = 80.f; Comp->ShowFlags.SetMotionBlur(false);
		for (const TWeakObjectPtr<AActor>& A : Hidden) if (A.IsValid()) Comp->HiddenActors.Add(A.Get());
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
		Target->InitCustomFormat(1280, 720, PF_B8G8R8A8, false); Target->UpdateResourceImmediate(true); Comp->TextureTarget = Target;

		TArray<TPair<FVector, FRotator>> Views;
		for (int32 I = 0; I < FMath::Min(3, Starts.Num()); ++I)
		{
			// Look down the longest open line from this start (a corridor, not the wall next to it).
			const FVector Eye = Starts[I] + FVector(0, 0, 75);
			float BestYaw = 0.f, BestFree = -1.f;
			for (int32 Step = 0; Step < 24; ++Step)
			{
				const float Yaw = Step * 15.f;
				const FVector End = Eye + FRotator(0, Yaw, 0).Vector() * 6000.f;
				FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(LookView), true);
				const float Free = World->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Params) ? Hit.Distance : 6000.f;
				if (Free > BestFree) { BestFree = Free; BestYaw = Yaw; }
			}
			Views.Emplace(Eye, FRotator(-4.f, BestYaw, 0.f));
		}
		if (!Views.IsEmpty()) Views.Emplace(Views[0].Key + FVector(0, 0, 220), FRotator(-28.f, Views[0].Value.Yaw, 0.f)); // high, under the ceiling

		for (int32 V = 0; V < Views.Num(); ++V)
		{
			Capture->SetActorLocationAndRotation(Views[V].Key, Views[V].Value);
			for (int32 I = 0; I < 8; ++I) { Comp->CaptureScene(); FlushRenderingCommands(); if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation(); FAssetCompilingManager::Get().FinishAllCompilation(); }
			TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
			if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags)) continue;
			int32 Black = 0, Bright = 0; double Sum = 0;
			for (FColor& P : Pixels)
			{
				P.A = 255;
				const float L = (.2126f * P.R + .7152f * P.G + .0722f * P.B) / 255.f;
				Sum += L; Black += L < .02f ? 1 : 0; Bright += L > .35f ? 1 : 0;
			}
			const double Mean = Sum / FMath::Max(1, Pixels.Num());
			AddInfo(FString::Printf(TEXT("%s view %d: mean %.3f, near-black %.0f%%, bright %.1f%%"), Level, V, Mean,
				100. * Black / FMath::Max(1, Pixels.Num()), 100. * Bright / FMath::Max(1, Pixels.Num())));
			TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(1280, 720, Pixels, Png);
			FFileHelper::SaveArrayToFile(Png, *(Dir / FString::Printf(TEXT("Look_%s_%d.png"), Level, V)));
		}
		Capture->Destroy();
	}
	return true;
}
#endif
