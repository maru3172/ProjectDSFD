#include "ProjectPracticeEnvironment.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "ProjectProject01RoleSpawnPoint.h"
#include "ProjectProject01Tutorial.h"
#include "UObject/ConstructorHelpers.h"

AProjectPracticeEnvironment::AProjectPracticeEnvironment()
{
    PrimaryActorTick.bCanEverTick=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PracticeLayout")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    BlockMesh=Cube.Object;
}
int32 AProjectPracticeEnvironment::Build(const FTransform& Anchor)
{
    for(const auto& Block:Blocks)if(Block)Block->DestroyComponent();Blocks.Reset();
    auto* World=GetWorld();if(!World||!BlockMesh)return 0;
    // Only free practice can request this geometry, even if spawned from a console/Blueprint.
    const auto* Mode=World->GetAuthGameMode<AProjectProject01TutorialGameMode>();
    if(!Mode||!Mode->IsFreePractice())return 0;
    struct FPiece{FVector Center,Size;};
    const FPiece Pieces[]={
        {{-100,-350,0},{60,900,300}},   // L corner: break central sightline, two alternative lanes.
        {{-480,-800,0},{700,60,300}},
        {{1000,650,0},{60,900,300}},    // Second corner; exit approach remains open.
        {{1230,200,0},{400,60,300}},
        {{-550,150,0},{220,140,200}},   // Display islands: look around either edge, then disengage.
        {{650,-550,0},{240,240,220}},
        {{-250,1100,0},{240,140,220}}
    };
    for(const auto& Piece:Pieces)
    {
        const FVector XY=Anchor.TransformPositionNoScale(Piece.Center);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PracticeLayoutFloor),true,this);
        bool Supported=true;float FloorZ=0;
        // Reject absent/different-storey floor, not a solid block floating over a gap.
        for(const FVector Corner:{FVector::ZeroVector,FVector(-.5,-.5,0),FVector(-.5,.5,0),FVector(.5,-.5,0),FVector(.5,.5,0)})
        {
            const FVector P=XY+Anchor.TransformVectorNoScale(Corner*Piece.Size);FHitResult Hit;
            if(!World->LineTraceSingleByChannel(Hit,P+FVector(0,0,180),P-FVector(0,0,160),ECC_Visibility,Query))
            {Supported=false;break;}
            if(Corner.IsZero())FloorZ=Hit.ImpactPoint.Z;
            else if(FMath::Abs(Hit.ImpactPoint.Z-FloorZ)>12){Supported=false;break;}
        }
        if(!Supported)continue;
        const FVector Center(XY.X,XY.Y,FloorZ+Piece.Size.Z*.5+2);
        const FQuat Rotation=FRotator(0,Anchor.Rotator().Yaw,0).Quaternion();
        const FBox LocalBox(-Piece.Size*.5-FVector(100,100,0),Piece.Size*.5+FVector(100,100,200));
        const FTransform TestTransform(Rotation,Center);
        bool Reserved=false;
        for(TActorIterator<AActor> It(World);It;++It)
            if(It->IsA<APawn>()||It->IsA<AProjectProject01RoleSpawnPoint>()||It->IsA<AProjectProject01ObjectiveTrigger>())
                if(LocalBox.IsInsideOrOn(TestTransform.InverseTransformPosition(It->GetActorLocation()))){Reserved=true;break;}
        if(Reserved||World->OverlapBlockingTestByChannel(Center,Rotation,ECC_Pawn,FCollisionShape::MakeBox(Piece.Size*.5-FVector(1)),Query))continue;
        auto* Block=NewObject<UStaticMeshComponent>(this);
        AddInstanceComponent(Block);Block->SetupAttachment(RootComponent);
        Block->SetStaticMesh(BlockMesh);Block->SetCollisionProfileName(TEXT("BlockAll"));
        Block->SetCanEverAffectNavigation(true);Block->SetMobility(EComponentMobility::Static);
        Block->SetWorldLocationAndRotation(Center,Rotation);Block->SetWorldScale3D(Piece.Size/100.f);
        Block->RegisterComponent();Blocks.Add(Block);
    }
    return Blocks.Num();
}
