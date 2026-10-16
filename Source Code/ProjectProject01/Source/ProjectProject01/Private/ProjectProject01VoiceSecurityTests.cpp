#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectVoiceAudibilityTest,"ProjectProject01.Multiplayer.SurvivorVoice.AuthoritativeDistanceGain",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectVoiceAudibilityTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>();auto* Voice=NewObject<UProjectProject01VoiceChatSubsystem>(Instance);
    TestEqual(TEXT("Close voice is full gain"),Voice->CalculateDistanceGain(350),1.0f);
    TestEqual(TEXT("Midway voice is half gain"),Voice->CalculateDistanceGain(1075),0.5f);
    TestEqual(TEXT("Far voice is silent"),Voice->CalculateDistanceGain(1800),0.0f);
    TestEqual(TEXT("Invalid distance is silent"),Voice->CalculateDistanceGain(std::numeric_limits<float>::quiet_NaN()),0.0f);
    TestEqual(TEXT("Invalid configuration is silent"),ProjectVoiceAudibility::DistanceGain(0,0,std::numeric_limits<float>::infinity(),1),0.0f);
    TestTrue(TEXT("Extreme finite configuration cannot produce NaN"),FMath::IsFinite(ProjectVoiceAudibility::DistanceGain(10,MAX_flt,MAX_flt,1)));
    Voice->bConfiguredForSurvivor=true;Voice->MatchId=TEXT("match");Voice->ParticipantAccountIds.Add(TEXT("peer"),TEXT("2"));
    TArray<FProjectVoiceAudibility> Peers;FProjectVoiceAudibility Peer;Peer.UserId=TEXT("2");Peer.Gain=0.5f;Peers.Add(Peer);
    Voice->UpdateAuthoritativeAudibility(TEXT("another-match"),true,Peers);
    TestFalse(TEXT("Other match cannot open receiver"),Voice->HasFreshAudibility());
    Voice->UpdateAuthoritativeAudibility(TEXT("match"),true,Peers);
    TestTrue(TEXT("Matching authority update is fresh"),Voice->HasFreshAudibility());
    TestEqual(TEXT("Authenticated account maps to EOS peer gain"),Voice->GetParticipantDistanceGain(TEXT("peer")),0.5f);
    TestEqual(TEXT("Unknown voice identity fails closed"),Voice->GetParticipantDistanceGain(TEXT("outsider")),0.0f);
    Voice->ParticipantControls.Volumes.Add(TEXT("peer"),0.4f);
    TestEqual(TEXT("Personal gain multiplies rather than replaces distance"),Voice->GetParticipantVolume(TEXT("peer"))*Voice->GetParticipantDistanceGain(TEXT("peer")),0.2f);
    Voice->AppliedPeerGains.Add(TEXT("peer"),0.2f);Voice->ResetParticipantPreferences();
    TestTrue(TEXT("Preference reset invalidates SDK cache, not distance permission"),Voice->AppliedPeerGains.IsEmpty() && Voice->GetParticipantDistanceGain(TEXT("peer"))==0.5f);
    Voice->ParticipantControls.Volumes.Add(TEXT("peer"),0.4f);
    Voice->LastAudibilityUpdateSeconds=FPlatformTime::Seconds()-2;
    TestEqual(TEXT("Stale authority stops receiving"),Voice->GetParticipantDistanceGain(TEXT("peer")),0.0f);
    Voice->UpdateAuthoritativeAudibility(TEXT("match"),false,Peers);
    TestEqual(TEXT("Inactive survivor cannot receive"),Voice->GetParticipantDistanceGain(TEXT("peer")),0.0f);
    Voice->UpdateAuthoritativeAudibility(TEXT("match"),true,Peers);Voice->SuspendForLevelTravel();
    TestFalse(TEXT("Travel waits for new server authority"),Voice->HasFreshAudibility());
    TestTrue(TEXT("Travel retains personal preferences"),Voice->ParticipantControls.Volumes.Contains(TEXT("peer")));
    Voice->LeaveMatch();TestTrue(TEXT("Logout clears account voice mapping"),Voice->ParticipantAccountIds.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectStaleFrontendResponseTest,"ProjectProject01.Frontend.StaleHttpResponseIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectStaleFrontendResponseTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>();auto* Auth=NewObject<UProjectProject01AuthSubsystem>(Instance);
    auto Old=FHttpModule::Get().CreateRequest();auto Current=FHttpModule::Get().CreateRequest();
    Auth->ActiveRequest=Current;Auth->AuthState=EProjectProject01AuthState::SigningIn;Auth->AccessToken=TEXT("current-fixture");
    Auth->HandleRequestComplete(Old,nullptr,false,UProjectProject01AuthSubsystem::EAuthOperation::Login,TEXT(""),TEXT(""),0);
    TestTrue(TEXT("Old login response cannot clear new request/state"),Auth->ActiveRequest==Current && Auth->AuthState==EProjectProject01AuthState::SigningIn && Auth->AccessToken==TEXT("current-fixture"));
    Auth->ServiceStatusRequest=Current;Auth->bServiceReachable=true;Auth->ServiceAnnouncement=TEXT("current");
    Auth->HandleServiceStatusRequestComplete(Old,nullptr,false);
    TestTrue(TEXT("Old service status cannot overwrite current reachability/notice"),Auth->ServiceStatusRequest==Current && Auth->bServiceReachable && Auth->ServiceAnnouncement==TEXT("current"));
    Auth->ActiveRequest.Reset();Auth->ServiceStatusRequest.Reset();Auth->AuthState=EProjectProject01AuthState::SignedOut;
    Auth->HandleRequestComplete(Old,nullptr,false,UProjectProject01AuthSubsystem::EAuthOperation::Refresh,TEXT(""),TEXT(""),0);
    TestTrue(TEXT("Canceled request cannot revive logged out session"),Auth->AuthState==EProjectProject01AuthState::SignedOut);
    auto* Lobby=NewObject<UProjectProject01LobbySubsystem>(Instance);Lobby->ActiveRequest=Current;
    Lobby->HandleRequestComplete(Old,nullptr,false,EProjectProject01LobbyOperation::RefreshRooms,TEXT("GET"),TEXT("/api/rooms"),TEXT(""),0);
    TestTrue(TEXT("Old lobby failure cannot clear new pending request or retry"),Lobby->ActiveRequest==Current);
    const uint64 Generation=Lobby->RequestGeneration;Lobby->ForceClearLocalSession();
    TestTrue(TEXT("Forced logout invalidates delayed refresh continuation"),Lobby->RequestGeneration!=Generation && !Lobby->ActiveRequest.IsValid());
    return true;
}
#endif
