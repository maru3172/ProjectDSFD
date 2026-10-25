#if WITH_DEV_AUTOMATION_TESTS
#include "ProjectMatchReplay.h"
#include "ProjectInstantReplay.h"
#include "ProjectProject01VersionContract.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/SaveGame.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectReplayContractTest,"ProjectProject01.Replays.BoundsAndIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectReplayContractTest::RunTest(const FString&)
{
    const FString Prefix=TEXT("DFSD_P")+FProjectProject01VersionContract::GetNetworkProtocolVersion()+TEXT("_");
    const FString Owned=Prefix+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TestTrue(TEXT("Only a canonical replay name belongs to this game protocol"),UProjectMatchReplay::IsOwnedName(Owned));
    for(const FString Bad:{FString(TEXT("../"))+Owned,FString(TEXT("C:/temp/"))+Owned,Prefix+TEXT("not-a-guid"),FString(TEXT("manual_developer_replay")),FString(TEXT("DFSD_P0_"))+FGuid::NewGuid().ToString(EGuidFormats::Digits)})
        TestFalse(TEXT("A path, developer recording or incompatible replay cannot be played/deleted"),UProjectMatchReplay::IsOwnedName(Bad));
    auto* Instance=NewObject<UGameInstance>();auto* Replays=NewObject<UProjectMatchReplay>(Instance);
    TestFalse(TEXT("Full-match disk recording starts disabled"),Replays->IsRecordingEnabled());
    TestFalse(TEXT("A replay cannot disconnect a non-existent/live match context"),Replays->Play(Owned));
    TestFalse(TEXT("Death cameras have no SaveGame class or disk persistence"),UProjectDeathCameraClip::StaticClass()->IsChildOf(USaveGame::StaticClass()));
    auto* Clip=NewObject<UProjectDeathCameraClip>();TestFalse(TEXT("An empty clip cannot hold the screen"),Clip->IsValidClip());
    Clip->Focus=1;auto& Body=Clip->Bodies.AddDefaulted_GetRef();Body.Key=1;Body.Mesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/ReplayFixture/Body.Body")));
    for(int32 I=0;I<3;++I)
    {auto& F=Clip->Frames.AddDefaulted_GetRef();F.Seconds=I*.1;auto& P=F.Poses.AddDefaulted_GetRef();P.Key=1;P.Bones.Add(FTransform::Identity);}
    TestTrue(TEXT("A short, finite recorded clip passes data validation"),Clip->IsValidClip());
    Clip->Frames[1].Fov=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Invalid FOV rejected before scene creation"),Clip->IsValidClip());Clip->Frames[1].Fov=90;
    Clip->Frames[1].Seconds=-1;TestFalse(TEXT("Non-monotonic replay time rejected"),Clip->IsValidClip());Clip->Frames[1].Seconds=.1;
    Clip->Frames[1].Poses[0].Key=2;TestFalse(TEXT("Unknown actor identities rejected"),Clip->IsValidClip());Clip->Frames[1].Poses[0].Key=1;
    Clip->Frames[1].Poses[0].Bones.SetNum(257);TestFalse(TEXT("Excessive skeleton size rejected"),Clip->IsValidClip());Clip->Frames[1].Poses[0].Bones.SetNum(1);
    Clip->Frames.Last().Seconds=20;TestFalse(TEXT("Unbounded clip duration rejected"),Clip->IsValidClip());
    auto* Death=NewObject<UProjectInstantReplay>(Instance);
    for(int32 I=0;I<100;++I){FProjectReplayFrame F;F.Seconds=I*.1;Death->AppendFrame(MoveTemp(F));}
    TestEqual(TEXT("The rolling buffer never exceeds 60 frames"),Death->Ring.Num(),60);
    Death->ClearMatch();
    for(int32 I=0;I<60;++I)
    {
        FProjectReplayFrame F;F.Seconds=I*.1;
        for(int32 J=0;J<24;++J){auto& P=F.Poses.AddDefaulted_GetRef();P.Key=J+1;P.Bones.Init(FTransform::Identity,256);}
        Death->AppendFrame(MoveTemp(F));
    }
    TestTrue(TEXT("Dense skeleton buffers stay within a 16 MiB data budget"),Death->RingBytes<=16LL*1024*1024);
    TestTrue(TEXT("The memory cap shortens history instead of dropping the latest frames"),Death->Ring.Num()<60&&FMath::IsNearlyEqual(Death->Ring.Last().Seconds,5.9));
    Death->bDeathQueued=true;Death->CancelPresentation();TestFalse(TEXT("Logout/travel cancellation cannot leave a queued death camera"),Death->IsDeathCameraPending());
    Death->ClearMatch();TestTrue(TEXT("A new match removes old actors, frames and byte counters"),Death->Ring.IsEmpty()&&Death->Bodies.IsEmpty()&&Death->RingBytes==0);
    return true;
}
#endif
