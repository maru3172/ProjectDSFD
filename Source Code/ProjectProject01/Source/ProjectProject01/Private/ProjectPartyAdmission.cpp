#include "ProjectCaseRecords.h"
#include "ProjectPartyAdmissionWidget.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ProjectDFSDUITheme.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
    FString Text(const TSharedPtr<FJsonObject>& J,const TCHAR* K){FString S;if(J)J->TryGetStringField(K,S);return S;}
    bool Flag(const TSharedPtr<FJsonObject>& J,const TCHAR* K){bool B=false;if(J)J->TryGetBoolField(K,B);return B;}
    int32 Count(const TSharedPtr<FJsonObject>& J,const TCHAR* K){double N=0;if(J)J->TryGetNumberField(K,N);return FMath::IsFinite(N)?int32(FMath::Clamp(N,0.,100.)):0;}
    UProjectCaseRecords* Records(const UUserWidget* W){auto* GI=W->GetGameInstance();return GI?GI->GetSubsystem<UProjectCaseRecords>():nullptr;}
    TSharedRef<STextBlock> Label(const FString& S,int Size=18){return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true);}
}
bool UProjectCaseRecords::HasPendingPartyAdmission()const
{const auto State=Text(PartyAdmission,TEXT("state"));return State==TEXT("Pending")||State==TEXT("Ready");}
int32 UProjectCaseRecords::PartyAdmissionSecondsLeft()const
{return FMath::Max(0,FMath::CeilToInt(AdmissionDeadline-FPlatformTime::Seconds()));}
void UProjectCaseRecords::ClearPartyAdmission()
{
    if(AdmissionRequest){AdmissionRequest->OnProcessRequestComplete().Unbind();AdmissionRequest->CancelRequest();AdmissionRequest.Reset();}
    AdmissionResume=nullptr;AdmissionCancel=nullptr;PartyAdmission.Reset();TrackedAdmissionId.Reset();DismissedAdmissionId.Reset();CompletedAdmissionId.Reset();AdmissionDeadline=0;AdmissionPollIn=0;
    if(AdmissionWidget)AdmissionWidget->RemoveFromParent();AdmissionWidget=nullptr;
}
void UProjectCaseRecords::TrackPartyAdmission(const TSharedPtr<FJsonObject>& View,TFunction<bool(const FString&)> Resume,TFunction<void()> Cancel)
{
    if(!View)return;TrackedAdmissionId=Text(View,TEXT("id"));AdmissionResume=MoveTemp(Resume);AdmissionCancel=MoveTemp(Cancel);DismissedAdmissionId.Reset();
    auto J=MakeShared<FJsonObject>();J->SetObjectField(TEXT("partyAdmission"),View);ReadPartyAdmission(J);AdmissionPollIn=0;
}
void UProjectCaseRecords::DismissPartyAdmission(const FString& Id){DismissedAdmissionId=Id;}
void UProjectCaseRecords::ReadPartyAdmission(const TSharedPtr<FJsonObject>& Json)
{
    FString Leader;const TSharedPtr<FJsonObject>* PartyStateJson=nullptr,*Party=nullptr;const TArray<TSharedPtr<FJsonValue>>* Members=nullptr;
    if(Json&&Json->TryGetStringField(TEXT("partyLeaderId"),Leader)&&Snapshot&&Snapshot->TryGetObjectField(TEXT("partyState"),PartyStateJson)&&(*PartyStateJson)->TryGetObjectField(TEXT("party"),Party))
        if(Text(*Party,TEXT("leaderId"))!=Leader||!(*Party)->TryGetArrayField(TEXT("members"),Members)||Members->Num()!=Count(Json,TEXT("partyMemberCount")))PollIn=0;
    const TSharedPtr<FJsonObject>* View=nullptr;
    if(!Json||!Json->TryGetObjectField(TEXT("partyAdmission"),View))return;
    FGuid Parsed;const FString Id=Text(*View,TEXT("id")),State=Text(*View,TEXT("state"));
    if(!FGuid::Parse(Id,Parsed)||(State!=TEXT("Pending")&&State!=TEXT("Ready")&&State!=TEXT("Cancelled")&&State!=TEXT("Completed")))return;
    const bool Same=Id==Text(PartyAdmission,TEXT("id"));const double Deadline=FPlatformTime::Seconds()+Count(*View,TEXT("remainingSeconds"));
    AdmissionDeadline=Same&&AdmissionDeadline>0?FMath::Min(AdmissionDeadline,Deadline):Deadline;PartyAdmission=*View;
    if(State==TEXT("Cancelled")&&Id==TrackedAdmissionId)
    {AdmissionResume=nullptr;TrackedAdmissionId.Reset();auto Cancel=MoveTemp(AdmissionCancel);AdmissionCancel=nullptr;if(Cancel)Cancel();}
    if(State==TEXT("Completed")&&CompletedAdmissionId!=Id)
    {
        CompletedAdmissionId=Id;AdmissionResume=nullptr;AdmissionCancel=nullptr;TrackedAdmissionId.Reset();PollIn=0;
        if(auto* L=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();L&&!L->HasCurrentRoom()&&!L->IsRequestInFlight())L->RefreshCurrentRoom();
    }
    OnChanged.Broadcast();
}
void UProjectCaseRecords::TickPartyAdmission(float Delta)
{
    if(HasPendingPartyAdmission()&&PartyAdmissionSecondsLeft()==0)
    {PartyAdmission->SetStringField(TEXT("state"),TEXT("Cancelled"));PartyAdmission->SetStringField(TEXT("message"),TEXT("응답 시간이 지나 입장을 취소했습니다."));AdmissionResume=nullptr;TrackedAdmissionId.Reset();auto Cancel=MoveTemp(AdmissionCancel);AdmissionCancel=nullptr;if(Cancel)Cancel();OnChanged.Broadcast();}
    if(Text(PartyAdmission,TEXT("state"))==TEXT("Ready")&&TrackedAdmissionId==Text(PartyAdmission,TEXT("id"))&&Flag(PartyAdmission,TEXT("isLeader"))&&AdmissionResume)
        if(AdmissionResume(TrackedAdmissionId))AdmissionResume=nullptr;
    const FString Id=Text(PartyAdmission,TEXT("id"));const FString State=Text(PartyAdmission,TEXT("state"));
    if(!Id.IsEmpty()&&Id!=DismissedAdmissionId&&(HasPendingPartyAdmission()||State==TEXT("Cancelled"))&&(!AdmissionWidget||!AdmissionWidget->IsInViewport()))
        if(auto* P=GetGameInstance()->GetFirstLocalPlayerController())
        {AdmissionWidget=CreateWidget<UProjectPartyAdmissionWidget>(P);if(AdmissionWidget){AdmissionWidget->AdmissionId=Id;AdmissionWidget->Open();}}
    AdmissionPollIn-=Delta;if(AdmissionPollIn>0||AdmissionRequest)return;
    bool Party=false;const TSharedPtr<FJsonObject>* S=nullptr,*P=nullptr;const TArray<TSharedPtr<FJsonValue>>* Members=nullptr;
    if(Snapshot&&Snapshot->TryGetObjectField(TEXT("partyState"),S)&&(*S)->TryGetObjectField(TEXT("party"),P)&&(*P)->TryGetArrayField(TEXT("members"),Members))Party=Members->Num()>1;
    auto* L=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();
    if(!HasPendingPartyAdmission()&&(!Party||(L&&L->HasCurrentRoom()&&(L->GetCurrentRoom().bStarted||L->GetCurrentRoom().bStarting||L->GetCurrentRoom().bServerPreparing))))return;
    AdmissionPollIn=HasPendingPartyAdmission()?1:2;SendPartyAdmission();
}
void UProjectCaseRecords::AnswerPartyAdmission(const FString& Action,const FString& Id)
{
    if(Id!=Text(PartyAdmission,TEXT("id")))return;
    if(AdmissionRequest){AdmissionRequest->OnProcessRequestComplete().Unbind();AdmissionRequest->CancelRequest();AdmissionRequest.Reset();}
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),Id);J->SetStringField(TEXT("action"),Action);FString Body;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Body));SendPartyAdmission(Body);
}
void UProjectCaseRecords::SendPartyAdmission(const FString& Body)
{
    auto* A=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();if(AdmissionRequest||!A||!A->IsSignedIn())return;
    const auto Generation=A->GetSessionGeneration();auto Pending=FHttpModule::Get().CreateRequest();AdmissionRequest=Pending;
    Pending->SetURL(A->GetApiBaseUrlForAuthenticatedRequest()+TEXT("/api/parties/admission"));Pending->SetVerb(Body.IsEmpty()?TEXT("GET"):TEXT("POST"));Pending->SetTimeout(5);
    Pending->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+A->GetAccessTokenForAuthenticatedRequest());FProjectProject01VersionContract::ApplyToRequest(Pending);
    if(!Body.IsEmpty()){Pending->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Pending->SetContentAsString(Body);}
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this,Generation](FHttpRequestPtr Done,FHttpResponsePtr Response,bool Connected)
    {
        if(AdmissionRequest!=Done)return;AdmissionRequest.Reset();auto* Auth=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>();if(!Auth||!Auth->IsSignedIn()||Auth->GetSessionGeneration()!=Generation)return;
        if(Response&&Response->GetResponseCode()==401){Auth->InvalidateSessionFromServerFailure(TEXT("로그인이 만료되었습니다. 다시 로그인하세요."));return;}
        TSharedPtr<FJsonObject> J;
        if(Connected&&Response&&EHttpResponseCodes::IsOk(Response->GetResponseCode())&&Response->GetContentLength()<65536&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J))ReadPartyAdmission(J);
        AdmissionPollIn=HasPendingPartyAdmission()?1:2;OnChanged.Broadcast();
    });
    if(!Pending->ProcessRequest()){AdmissionRequest.Reset();AdmissionPollIn=2;}
}
void UProjectPartyAdmissionWidget::Open()
{
    SetIsFocusable(true);if(FSlateApplication::IsInitialized())PreviousFocus=FSlateApplication::Get().GetUserFocusedWidget(0);
    AddToViewport(1280);if(auto* P=GetOwningPlayer())SetUserFocus(P);
}
TSharedRef<SWidget> UProjectPartyAdmissionWidget::RebuildWidget()
{
    auto View=[this]{auto* R=Records(this);return R?R->PartyAdmission:nullptr;};
    auto Cancelled=[View]{return Text(View(),TEXT("state"))==TEXT("Cancelled");};
    auto Body=SNew(SVerticalBox);
    Body->AddSlot().AutoHeight().Padding(0,0,0,16)[Label(TEXT("파티 입장"),24)];
    Body->AddSlot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text_Lambda([View]{return FText::FromString(Text(View(),TEXT("label")));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true)];
    Body->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text_Lambda([this,View,Cancelled]
    {auto J=View();if(Cancelled())return FText::FromString(Text(J,TEXT("message")));auto* R=Records(this);return FText::FromString(FString::Printf(TEXT("수락 %d/%d · %d초"),Count(J,TEXT("accepted")),Count(J,TEXT("required")),R?R->PartyAdmissionSecondsLeft():0));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true)];
    auto Accept=SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(12)
      .Visibility_Lambda([View,Cancelled]{return !Cancelled()&&!Flag(View(),TEXT("isLeader"))&&!Flag(View(),TEXT("hasAccepted"))?EVisibility::Visible:EVisibility::Collapsed;})
      .IsEnabled_Lambda([this]{auto* R=Records(this);return R&&!R->IsPartyAdmissionBusy();})
      .OnClicked_Lambda([this]{if(auto* R=Records(this))R->AnswerPartyAdmission(TEXT("Accept"),AdmissionId);return FReply::Handled();})[Label(TEXT("수락"))];
    Body->AddSlot().AutoHeight().Padding(0,6)[Accept];
    Body->AddSlot().AutoHeight()[SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(12)
      .IsEnabled_Lambda([this,Cancelled]{auto* R=Records(this);return R&&(Cancelled()||!R->IsPartyAdmissionBusy());})
      .OnClicked_Lambda([this,View,Cancelled]{if(auto* R=Records(this)){if(Cancelled()){R->DismissPartyAdmission(AdmissionId);Close();}else R->AnswerPartyAdmission(Flag(View(),TEXT("isLeader"))?TEXT("Cancel"):TEXT("Decline"),AdmissionId);}return FReply::Handled();})
      [SNew(STextBlock).Text_Lambda([View,Cancelled]{return FText::FromString(Cancelled()?TEXT("확인"):Flag(View(),TEXT("isLeader"))?TEXT("입장 취소"):TEXT("거절"));}).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18)).ColorAndOpacity(ProjectDFSDUI::Text())]];
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0,0,0,.7f)).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(20)
      [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[SNew(SBox).WidthOverride(540)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(24)[Body]]]];
}
void UProjectPartyAdmissionWidget::Close(){RemoveFromParent();}
void UProjectPartyAdmissionWidget::NativeTick(const FGeometry& G,float D)
{Super::NativeTick(G,D);auto* R=Records(this);if(!R||!R->IsAuthenticated()||Text(R->PartyAdmission,TEXT("id"))!=AdmissionId||Text(R->PartyAdmission,TEXT("state"))==TEXT("Completed"))Close();}
void UProjectPartyAdmissionWidget::NativeDestruct()
{if(FSlateApplication::IsInitialized())if(auto Prior=PreviousFocus.Pin();Prior&&FSlateApplication::Get().FindWidgetWindow(Prior.ToSharedRef()).IsValid())FSlateApplication::Get().SetUserFocus(0,Prior);Super::NativeDestruct();}
