#include "ProjectDFSDTitleScene.h"

#include "Animation/AnimationAsset.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProjectProject01GameInstance.h"
#include "UObject/ConstructorHelpers.h"

float AProjectDFSDTitleScene::LightEnvelope(float Seconds, bool bDouble)
{
	if (Seconds < 0 || Seconds >= 1.8f) return 1.0f;
	// One or two distinct failures of the display lamp, not continuous high-frequency flutter.
	// Only the key lamp dims; the stable fill/rim and UI never flash or disappear.
	const auto Blink = [Seconds](float Start)
	{
		return FMath::SmoothStep(Start, Start + .09f, Seconds)
			* (1 - FMath::SmoothStep(Start + .19f, Start + .35f, Seconds));
	};
	const float Failure = FMath::Max(Blink(.24f), bDouble ? Blink(1.04f) : 0.0f);
	return FMath::Clamp(1 - .84f * Failure, .16f, 1.0f);
}

AProjectDFSDTitleScene::AProjectDFSDTitleScene()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TitleSet"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("TitleCamera")); Camera->SetupAttachment(RootComponent);
	Camera->SetRelativeLocation(FVector(-550, 0, 125)); Camera->SetRelativeRotation(FRotator(-1, 0, 0));
	Camera->SetFieldOfView(42); Camera->SetAspectRatio(16.0f / 9.0f); Camera->bConstrainAspectRatio = true;
	auto& PP = Camera->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true; PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true; PP.AutoExposureBias = -7;
	PP.bOverride_BloomIntensity = true; PP.BloomIntensity = 0.12f;
	PP.bOverride_VignetteIntensity = true; PP.VignetteIntensity = 0.38f;
	PP.bOverride_MotionBlurAmount = true; PP.MotionBlurAmount = 0;
	PP.bOverride_DynamicGlobalIlluminationMethod = true; PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	PP.bOverride_ReflectionMethod = true; PP.ReflectionMethod = EReflectionMethod::None;
	Camera->PostProcessBlendWeight = 1;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> Pose(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(TEXT("/Game/MyProject/Title/M_TitleSurface.M_TitleSurface"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mist(TEXT("/Game/MyProject/Title/M_TitleMist.M_TitleMist"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Speck(TEXT("/Game/MyProject/Title/M_TitleDust.M_TitleDust"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	IdlePose = Pose.Object; SurfaceMaterial = Surface.Object; MistMaterial = Mist.Object; DustMaterial = Speck.Object;
	Mannequin = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("DisplayMannequin"));
	Mannequin->SetupAttachment(RootComponent); Mannequin->SetSkeletalMesh(Body.Object);
	Mannequin->SetRelativeLocation(FVector(170, 160, 10)); Mannequin->SetRelativeRotation(FRotator(0, 70, 0));
	Mannequin->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mannequin->SetLightingChannels(false, true, false);
	Mannequin->SetCanEverAffectNavigation(false); Mannequin->SetGenerateOverlapEvents(false);
	Mannequin->bEnableUpdateRateOptimizations = true;
	const auto Block = [&](const TCHAR* Name, FVector Location, FVector Scale)
	{
		auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(FName(Name)); Mesh->SetupAttachment(RootComponent);
		Mesh->SetStaticMesh(Cube.Object); Mesh->SetRelativeLocation(Location); Mesh->SetRelativeScale3D(Scale);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetLightingChannels(false, true, false); Mesh->SetMobility(EComponentMobility::Movable);
		SetMeshes.Add(Mesh); return Mesh;
	};
	Block(TEXT("StoneFloor"), FVector(100, 0, -8), FVector(20, 18, .16));
	Block(TEXT("BackWall"), FVector(650, 0, 240), FVector(.25, 18, 5));
	Block(TEXT("Ceiling"), FVector(100, 0, 480), FVector(20, 18, .2));
	Block(TEXT("LeftWall"), FVector(100, -880, 240), FVector(20, .25, 5));
	Block(TEXT("RightWall"), FVector(100, 880, 240), FVector(20, .25, 5));
	Block(TEXT("DisplayPlinth"), FVector(170, 160, 5), FVector(1.5, 1.5, .10));
	Block(TEXT("ShowcaseLeft"), FVector(430, 30, 140), FVector(.12, .12, 2.8));
	Block(TEXT("ShowcaseRight"), FVector(430, 300, 140), FVector(.12, .12, 2.8));
	Block(TEXT("ShowcaseTop"), FVector(430, 165, 280), FVector(.12, 2.8, .10));
	Block(TEXT("ShowcaseBase"), FVector(430, 165, 12), FVector(1.4, 2.8, .12));
	for (int32 I = 0; I < 9; ++I)
		Block(*FString::Printf(TEXT("FloorJoint%d"), I), FVector(-450 + I * 150, 0, .05), FVector(.008, 18, .002));
	for (int32 I = 0; I < 11; ++I)
		Block(*FString::Printf(TEXT("CrossJoint%d"), I), FVector(100, -750 + I * 150, .06), FVector(20, .008, .002));

	KeyLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("FailingDisplayLight")); KeyLight->SetupAttachment(RootComponent);
	KeyLight->SetRelativeLocation(FVector(-100, -30, 340));
	// Aim beside the figure, not at its face: it watches from the edge of the light pool.
	KeyLight->SetRelativeRotation((FVector(210, 30, 50) - KeyLight->GetRelativeLocation()).Rotation());
	KeyLight->SetIntensityUnits(ELightUnits::Lumens); KeyLight->SetIntensity(BaseKeyIntensity);
	KeyLight->SetLightColor(FLinearColor(.72f, .84f, 1.0f)); KeyLight->SetAttenuationRadius(1200);
	KeyLight->SetInnerConeAngle(8); KeyLight->SetOuterConeAngle(27); KeyLight->SetLightingChannels(false, true, false);
	RimLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("EmergencyRim")); RimLight->SetupAttachment(RootComponent);
	RimLight->SetRelativeLocation(FVector(400, 300, 260));
	RimLight->SetRelativeRotation((FVector(170, 160, 110) - RimLight->GetRelativeLocation()).Rotation());
	RimLight->SetIntensityUnits(ELightUnits::Lumens); RimLight->SetIntensity(240);
	RimLight->SetLightColor(FLinearColor(1.0f, .20f, .09f)); RimLight->SetAttenuationRadius(850);
	RimLight->SetInnerConeAngle(18); RimLight->SetOuterConeAngle(32); RimLight->SetCastShadows(false);
	RimLight->SetLightingChannels(false, true, false);
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("SoftFrontFill")); FillLight->SetupAttachment(RootComponent);
	FillLight->SetRelativeLocation(FVector(-220, 160, 140)); FillLight->SetIntensityUnits(ELightUnits::Lumens);
	FillLight->SetIntensity(42); FillLight->SetLightColor(FLinearColor(.48f, .58f, .72f));
	FillLight->SetAttenuationRadius(650); FillLight->SetCastShadows(false); FillLight->SetLightingChannels(false, true, false);

	for (int32 I = 0; I < 4; ++I)
	{
		auto* Sheet = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("LowMist%d"), I));
		Sheet->SetupAttachment(RootComponent); Sheet->SetStaticMesh(Plane.Object); Sheet->SetMaterial(0, MistMaterial);
		const FVector Position(-20 + I * 65, 110 + (I % 2) * 55, 24 + (I % 3) * 12);
		Sheet->SetRelativeLocation(Position); Sheet->SetRelativeScale3D(FVector(3.2, .65, 1));
		Sheet->SetRelativeRotation(FRotationMatrix::MakeFromZ(Camera->GetRelativeLocation() - Position).Rotator());
		Sheet->SetCollisionEnabled(ECollisionEnabled::NoCollision); Sheet->SetCastShadow(false); Sheet->SetCanEverAffectNavigation(false);
		MistSheets.Add(Sheet); MistOrigins.Add(Position);
	}
	for (int32 I = 0; I < 24; ++I)
	{
		auto* Particle = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Ash%d"), I));
		Particle->SetupAttachment(RootComponent); Particle->SetStaticMesh(Plane.Object); Particle->SetMaterial(0, DustMaterial);
		const FVector Position(-140 + (I * 71 % 340), 30 + (I * 43 % 235), 35 + (I * 37 % 215));
		Particle->SetRelativeLocation(Position); Particle->SetRelativeScale3D(FVector(.004 + (I % 3) * .002));
		Particle->SetRelativeRotation(FRotationMatrix::MakeFromZ(Camera->GetRelativeLocation() - Position).Rotator());
		Particle->SetCollisionEnabled(ECollisionEnabled::NoCollision); Particle->SetCastShadow(false); Particle->SetCanEverAffectNavigation(false);
		Dust.Add(Particle); DustOrigins.Add(Position);
	}
}

void AProjectDFSDTitleScene::BeginPlay()
{
	Super::BeginPlay();
	// A fresh local stream per title entry: never replay the same failure sequence.
	Random.Initialize(static_cast<int32>(GetTypeHash(FGuid::NewGuid())));
	UntilDip = Random.FRandRange(2.5f, 5.5f);
	CreateSetMaterials();
	if (IdlePose)
	{
		Mannequin->PlayAnimation(IdlePose, false); Mannequin->SetPosition(.8f, false);
		Mannequin->TickAnimation(0, false); Mannequin->RefreshBoneTransforms();
	}
	// Freeze a decorative pose with an angled body but its head turned towards the viewer.
	// This edits only this title component's pose; the shared skeleton/animation assets are untouched.
	const auto FrozenPose = Mannequin->GetComponentSpaceTransforms();
	Mannequin->SetComponentSpaceTransformsDoubleBuffering(false);
	auto& Bones = Mannequin->GetEditableComponentSpaceTransforms(); Bones = FrozenPose;
	const int32 Neck = Mannequin->GetBoneIndex(TEXT("neck_01"));
	if (Bones.IsValidIndex(Neck))
	{
		const auto& Skeleton = Mannequin->GetSkeletalMeshAsset()->GetRefSkeleton();
		const FVector Pivot = Bones[Neck].GetLocation();
		const float Facing = Mannequin->GetRelativeRotation().Yaw + 90;
		const float Viewer = (Camera->GetRelativeLocation() - Mannequin->GetRelativeLocation()).Rotation().Yaw;
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(FMath::Clamp(FMath::FindDeltaAngleDegrees(Facing, Viewer), -48.f, 48.f)));
		for (int32 I = Neck; I < Bones.Num(); ++I)
			if (I == Neck || Skeleton.BoneIsChildOf(I, Neck))
			{
				Bones[I].SetLocation(Pivot + Turn.RotateVector(Bones[I].GetLocation() - Pivot));
				Bones[I].SetRotation((Turn * Bones[I].GetRotation()).GetNormalized());
			}
		Mannequin->MarkRenderDynamicDataDirty();
	}
	Mannequin->bPauseAnims = true; Mannequin->SetComponentTickEnabled(false);
	SetPresentationActive(false);
}

void AProjectDFSDTitleScene::CreateSetMaterials()
{
	const auto Make = [&](FLinearColor Color, float Roughness, float Metallic)
	{
		auto* M = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
		if (M) { M->SetVectorParameterValue(TEXT("Color"), Color); M->SetScalarParameterValue(TEXT("Roughness"), Roughness); M->SetScalarParameterValue(TEXT("Metallic"), Metallic); MaterialInstances.Add(M); }
		return M;
	};
	auto* Floor = Make(FLinearColor(.085f, .09f, .095f), .28f, .08f);
	auto* Dark = Make(FLinearColor(.008f, .01f, .013f), .68f, 0);
	auto* Metal = Make(FLinearColor(.045f, .05f, .055f), .35f, .45f);
	auto* Plastic = Make(FLinearColor(.34f, .33f, .30f), .4f, .02f);
	for (int32 I = 0; I < SetMeshes.Num(); ++I) SetMeshes[I]->SetMaterial(0, I == 0 ? Floor : (I >= 5 && I < 10 ? Metal : Dark));
	for (int32 I = 0; I < Mannequin->GetNumMaterials(); ++I) Mannequin->SetMaterial(I, Plastic);
	auto* Mist = UMaterialInstanceDynamic::Create(MistMaterial, this); EffectMaterials.Add(Mist);
	auto* Specks = UMaterialInstanceDynamic::Create(DustMaterial, this); EffectMaterials.Add(Specks);
	if (Mist) Mist->SetVectorParameterValue(TEXT("Color"), FLinearColor(12, 16, 21));
	if (Specks) Specks->SetVectorParameterValue(TEXT("Color"), FLinearColor(40, 45, 48));
	for (auto Sheet : MistSheets) Sheet->SetMaterial(0, Mist);
	for (auto Particle : Dust) Particle->SetMaterial(0, Specks);
}

void AProjectDFSDTitleScene::SetPresentationActive(bool bActive)
{
	bPresentationActive = bActive; SetActorTickEnabled(bActive);
}

void AProjectDFSDTitleScene::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPresentationActive || !FMath::IsFinite(DeltaSeconds)) return;
	const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, .05f); Clock += Dt;
	const auto* Settings = UProjectProject01GameUserSettings::Get();
	const float Flash = Settings ? Settings->GetLocalFlashScale() : 1;
	const float Motion = Settings ? Settings->GetScreenShakeScale() : 1;
	const float Effects = Settings ? Settings->GetLocalVFXScale() : 1;
	if (DipTime >= 0) { DipTime += Dt; if (DipTime >= 1.8f) { DipTime = -1; UntilDip = Random.FRandRange(7, 13); } }
	else { UntilDip -= Dt; if (UntilDip <= 0) { DipTime = 0; bDoubleDip = Random.FRand() < .5f; } }
	const float Gain = FMath::Lerp(1.0f, LightEnvelope(DipTime, bDoubleDip), FMath::Clamp(Flash, 0.0f, 1.0f));
	KeyLight->SetIntensity(BaseKeyIntensity * Gain);
	Camera->SetRelativeLocation(FVector(-550 + Motion * 1.5f * FMath::Sin(Clock * .19f), Motion * 1.2f * FMath::Sin(Clock * .13f), 125));
	Camera->SetRelativeRotation(FRotator(-1 + Motion * .04f * FMath::Sin(Clock * .17f), Motion * .09f * FMath::Sin(Clock * .11f), 0));
	for (int32 I = 0; I < MistSheets.Num(); ++I)
		MistSheets[I]->SetRelativeLocation(MistOrigins[I] + FVector(0, FMath::Sin(Clock * .12f + I) * 14, FMath::Sin(Clock * .1f + I) * 2));
	for (int32 I = 0; I < Dust.Num(); ++I)
		Dust[I]->SetRelativeLocation(DustOrigins[I] + FVector(FMath::Sin(Clock * .15f + I) * 4, FMath::Sin(Clock * .11f + I * 2) * 8, FMath::Sin(Clock * .09f + I) * 12));
	if (!FMath::IsNearlyEqual(Effects, LastEffectsScale) && EffectMaterials.IsValidIndex(0) && EffectMaterials[0])
	{
		EffectMaterials[0]->SetScalarParameterValue(TEXT("Opacity"), .13f * Effects); LastEffectsScale = Effects;
	}
	if (!FMath::IsNearlyEqual(Effects * Gain, LastDustGain) && EffectMaterials.IsValidIndex(1) && EffectMaterials[1])
	{
		EffectMaterials[1]->SetScalarParameterValue(TEXT("Opacity"), .25f * Effects * Gain); LastDustGain = Effects * Gain;
	}
}
