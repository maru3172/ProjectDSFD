#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectCaseRecords.h"
#include "ProjectPartyAdmissionWidget.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01SocialWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    TSharedRef<FJsonObject> Admission(const FString& Id,const FString& State,bool Leader,bool Accepted)
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),Id);J->SetStringField(TEXT("state"),State);J->SetStringField(TEXT("label"),TEXT("오픈매칭 · 서관 조사"));
        J->SetBoolField(TEXT("isLeader"),Leader);J->SetBoolField(TEXT("hasAccepted"),Accepted);J->SetNumberField(TEXT("required"),2);J->SetNumberField(TEXT("accepted"),Accepted?1:0);J->SetNumberField(TEXT("remainingSeconds"),30);J->SetStringField(TEXT("message"),TEXT("파티 입장이 취소되었습니다."));return J;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPartyAdmissionMenuTest,"ProjectProject01.PartyAdmission.ContextMenu",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectPartyAdmissionMenuTest::RunTest(const FString&)
{
    auto Contains=[](const auto& Actions,const FString& Op){return Actions.ContainsByPredicate([&](const auto& A){return A.Value==Op;});};
    const auto Self=UProjectProject01SocialWidget::PartyMenuActions(true,true),Member=UProjectProject01SocialWidget::PartyMenuActions(false,false),Leader=UProjectProject01SocialWidget::PartyMenuActions(false,true);
    TestTrue(TEXT("Own context menu only has profile"),Self.Num()==1&&Contains(Self,TEXT("Profile")));
    TestTrue(TEXT("Members retain profile, whisper and report"),Member.Num()==3&&Contains(Member,TEXT("Profile"))&&Contains(Member,TEXT("Whisper"))&&Contains(Member,TEXT("Report")));
    TestFalse(TEXT("Members cannot see transfer or kick"),Contains(Member,TEXT("Transfer"))||Contains(Member,TEXT("Kick")));
    TestTrue(TEXT("Leader's other-member menu adds transfer and kick"),Leader.Num()==5&&Contains(Leader,TEXT("Transfer"))&&Contains(Leader,TEXT("Kick")));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPartyAdmissionStateTest,"ProjectProject01.PartyAdmission.State",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectPartyAdmissionStateTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();auto* R=GI->GetSubsystem<UProjectCaseRecords>();ON_SCOPE_EXIT{GI->Shutdown();};
    const FString Id=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);int Resumes=0,Cancels=0;
    R->TrackPartyAdmission(Admission(Id,TEXT("Pending"),true,true),[&](const FString& Proof){TestEqual(TEXT("Only captured proposal authorizes continuation"),Proof,Id);++Resumes;return true;},[&]{++Cancels;});
    TestTrue(TEXT("202 enters finite pending state"),R->HasPendingPartyAdmission()&&R->PartyAdmissionSecondsLeft()>0);
    R->TickPartyAdmission(.5);TestEqual(TEXT("Leader cannot proceed without every vote"),Resumes,0);
    auto Json=MakeShared<FJsonObject>();Json->SetObjectField(TEXT("partyAdmission"),Admission(Id,TEXT("Ready"),true,true));R->ReadPartyAdmission(Json);R->TickPartyAdmission(.5);R->TickPartyAdmission(.5);
    TestEqual(TEXT("Ready replays once, not once per poll"),Resumes,1);
    Json->SetObjectField(TEXT("partyAdmission"),Admission(Id,TEXT("Cancelled"),true,true));R->ReadPartyAdmission(Json);R->ReadPartyAdmission(Json);
    TestEqual(TEXT("Cancellation releases waiting operation once"),Cancels,1);TestFalse(TEXT("Cancelled is no longer pending"),R->HasPendingPartyAdmission());
    R->TrackPartyAdmission(Admission(Id,TEXT("Pending"),false,false),[&](const FString&){++Resumes;return true;},[&]{++Cancels;});
    Json->SetObjectField(TEXT("partyAdmission"),Admission(Id,TEXT("Ready"),false,true));R->ReadPartyAdmission(Json);R->TickPartyAdmission(.5);TestEqual(TEXT("Recipient never commits the leader's request"),Resumes,1);
    R->AdmissionDeadline=FPlatformTime::Seconds()-1;R->TickPartyAdmission(.5);TestFalse(TEXT("Network silence cannot leave an infinite confirmation"),R->HasPendingPartyAdmission());TestEqual(TEXT("Timeout releases callback"),Cancels,2);
    R->ClearPartyAdmission();TestFalse(TEXT("Logout discards resumable payload and approval"),R->AdmissionResume||R->AdmissionCancel||R->PartyAdmission.IsValid());
    R->Snapshot=MakeShared<FJsonObject>();auto PS=MakeShared<FJsonObject>(),Party=MakeShared<FJsonObject>();Party->SetStringField(TEXT("leaderId"),TEXT("1"));Party->SetArrayField(TEXT("members"),{});PS->SetObjectField(TEXT("party"),Party);R->Snapshot->SetObjectField(TEXT("partyState"),PS);
    auto Meta=MakeShared<FJsonObject>();Meta->SetStringField(TEXT("partyLeaderId"),TEXT("2"));Meta->SetNumberField(TEXT("partyMemberCount"),2);R->PollIn=15;R->ReadPartyAdmission(Meta);
    TestEqual(TEXT("A leader/roster change immediately refreshes party presentation"),R->PollIn,0.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectPartyAdmissionWidgetTest,"ProjectProject01.PartyAdmission.Render",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectPartyAdmissionWidgetTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);auto* GI=NewObject<UGameInstance>();W->SetGameInstance(GI);GI->Init();auto* R=GI->GetSubsystem<UProjectCaseRecords>();auto* Auth=GI->GetSubsystem<UProjectProject01AuthSubsystem>();
    Auth->AuthState=EProjectProject01AuthState::SignedIn;Auth->AccessToken=TEXT("render-only-no-http");R->Request=FHttpModule::Get().CreateRequest();R->AdmissionRequest=FHttpModule::Get().CreateRequest();
    auto* Widget=NewObject<UProjectPartyAdmissionWidget>(W);Widget->AdmissionId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);static_cast<UUserWidget*>(Widget)->Initialize();auto Slate=Widget->TakeWidget();
    R->AdmissionRequest.Reset(); // Render actual enabled controls; no core ticker runs inside this synchronous fixture.
    ON_SCOPE_EXIT{Widget->ReleaseSlateResources(true);Auth->ForceClearLocalSession();GI->Shutdown();W->SetGameInstance(nullptr);W->DestroyWorld(false);};
    FWidgetRenderer Renderer(false);const FString Directory=FPaths::ProjectSavedDir()/TEXT("PartyAdmissionUIQA");IFileManager::Get().MakeDirectory(*Directory,true);
    for(int Mode=0;Mode<4;++Mode)for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
    {
        R->PartyAdmission=Admission(Widget->AdmissionId,Mode==3?TEXT("Cancelled"):TEXT("Pending"),Mode==0,Mode==0||Mode==2);R->AdmissionDeadline=FPlatformTime::Seconds()+30;
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
        for(int I=0;I<6;++I){Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),.016);FlushRenderingCommands();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);if(TestTrue(TEXT("Party consent renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))
        {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("Consent%d_%dx%d.png"),Mode,Size.X,Size.Y)));}
        bool Accept=false,Cancel=false,Decline=false,Confirm=false;
        TFunction<void(TSharedRef<SWidget>,bool)> Inspect=[&](TSharedRef<SWidget> Node,bool ParentVisible)
        {bool Visible=ParentVisible&&Node->GetVisibility().IsVisible();if(Node->GetType()==FName(TEXT("STextBlock"))&&Visible)
          {auto T=StaticCastSharedRef<STextBlock>(Node);FString S=T->GetText().ToString();Accept|=S==TEXT("수락");Cancel|=S==TEXT("입장 취소");Decline|=S==TEXT("거절");Confirm|=S==TEXT("확인");
            if(!S.IsEmpty()){auto G=Node->GetCachedGeometry();auto P=G.GetAbsolutePosition(),E=G.GetAbsoluteSize();TestTrue(TEXT("Consent text fits screen"),P.X>=0&&P.Y>=0&&P.X+E.X<=Size.X+1&&P.Y+E.Y<=Size.Y+1);}}
          if(auto* Children=Node->GetChildren())for(int I=0;I<Children->Num();++I)Inspect(Children->GetChildAt(I),Visible);};Inspect(Slate,true);
        TestEqual(TEXT("Only unanswered recipients receive Accept"),Accept,Mode==1);TestEqual(TEXT("Only leader receives cancellation"),Cancel,Mode==0);TestEqual(TEXT("Recipients can decline"),Decline,Mode==1||Mode==2);TestEqual(TEXT("Terminal cancellation has acknowledgement"),Confirm,Mode==3);Target->ReleaseResource();
    }
    return true;
}
#endif
