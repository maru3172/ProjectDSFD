#include "ProjectProject01SocialSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "ProjectProject01VersionContract.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "MultiplayTestPlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

namespace
{
// The owning GameInstance can be absent during teardown; never dereference it unchecked.
UProjectProject01AuthSubsystem* AuthOf(const UGameInstanceSubsystem* Owner)
{
    const UGameInstance* Instance=Owner?Owner->GetGameInstance():nullptr;
    return IsValid(Instance)?Instance->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;
}
UProjectProject01LobbySubsystem* LobbyOf(const UGameInstanceSubsystem* Owner)
{
    const UGameInstance* Instance=Owner?Owner->GetGameInstance():nullptr;
    return IsValid(Instance)?Instance->GetSubsystem<UProjectProject01LobbySubsystem>():nullptr;
}
}

void UProjectProject01SocialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UProjectProject01AuthSubsystem>();
    if(auto* Auth=AuthOf(this))Auth->OnLocalSessionCleared.AddUObject(this,&ThisClass::Clear);
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Tick),1.0f);
}
void UProjectProject01SocialSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if(auto* Auth=AuthOf(this))Auth->OnLocalSessionCleared.RemoveAll(this);
    Clear();Super::Deinitialize();
}
bool UProjectProject01SocialSubsystem::IsAuthenticated() const
{
    const auto* Auth=AuthOf(this);return Auth && Auth->IsSignedIn();
}
void UProjectProject01SocialSubsystem::Clear()
{
    for(auto* Request:{&ReadRequest,&WriteRequest,&PresenceRequest,&SearchRequest,&ProfileRequest,&ConversationRequest,&RoomContextRequest})if(Request->IsValid()){(*Request)->OnProcessRequestComplete().Unbind();(*Request)->CancelRequest();Request->Reset();}
    RoomContext.Reset();RoomContextUserId.Reset();
    Snapshot.Reset();SearchResults.Reset();Profile.Reset();Conversation.Reset();ConversationAccount.Reset();PendingReadId.Reset();SessionIdentity.Reset();Status.Reset();Presence=TEXT("Online");bLobbyVisible=false;bRoomRefreshPending=false;UntilPoll=0;OnChanged.Broadcast();
}
bool UProjectProject01SocialSubsystem::Tick(float Delta)
{
    auto* Auth=AuthOf(this);
    if(!Auth || !Auth->IsSignedIn())return true;
    if(!PendingReadId.IsEmpty()&&!ConversationAccount.IsEmpty()&&!WriteRequest.IsValid())Act(TEXT("ReadConversation"),ConversationAccount,PendingReadId);
    if(bRoomRefreshPending)if(auto* Lobby=LobbyOf(this))if(!Lobby->IsRequestInFlight()){bRoomRefreshPending=false;Lobby->RefreshCurrentRoom();}
    if(SessionIdentity!=Auth->GetAccessTokenForAuthenticatedRequest())
    {
        // Tokens also rotate on refresh: discard old in-flight responses but retain the UI context.
        SessionIdentity=Auth->GetAccessTokenForAuthenticatedRequest();Snapshot.Reset();UntilPoll=0;
    }
    UntilPoll-=Delta;
    if(UntilPoll<=0)
    {
        if(IsInMatchWorld())
        {
            // Social UI exists only in the lobby, so list/conversation reads pause during a match.
            // Presence still refreshes inside the backend's 20 s freshness window so friends do not see Offline.
            UntilPoll=InMatchPresenceIntervalSeconds;Act(TEXT("Presence"));
        }
        else{UntilPoll=3;Act(TEXT("Presence"));Refresh();if(bLobbyVisible&&!ConversationAccount.IsEmpty()&&!ConversationRequest.IsValid())Detail(2,TEXT("/api/social/conversations/")+FGenericPlatformHttp::UrlEncode(ConversationAccount));}
    }
    return true;
}
bool UProjectProject01SocialSubsystem::IsInMatchWorld() const
{
    const UGameInstance* Instance=GetGameInstance();
    const UWorld* World=Instance?Instance->GetWorld():nullptr;
    const APlayerController* Controller=World?World->GetFirstPlayerController():nullptr;
    return IsValid(Controller)&&Controller->IsA<AMultiplayTestPlayerController>();
}
void UProjectProject01SocialSubsystem::SetLobbyVisible(bool bVisible)
{
    bLobbyVisible=bVisible;UntilPoll=0;
    if(IsAuthenticated())Act(TEXT("Presence"));
    if(!bVisible && Snapshot.IsValid())Snapshot->SetArrayField(TEXT("messages"),{});
    if(!bVisible){OpenConversation(FString());if(Snapshot.IsValid())Snapshot->SetArrayField(TEXT("conversations"),{});}
}
void UProjectProject01SocialSubsystem::SetPresence(const FString& Value)
{
    Presence=Value;Act(TEXT("Presence"));
}
void UProjectProject01SocialSubsystem::Refresh(){if(IsAuthenticated()&&!ReadRequest.IsValid())Send(true,FString());}
void UProjectProject01SocialSubsystem::Act(const FString& Operation,const FString& Target,const FString& Text)
{
    if(!IsAuthenticated())return;
    const bool bPresence=Operation==TEXT("Presence");
    if((bPresence?PresenceRequest:WriteRequest).IsValid()){if(!bPresence){Status=TEXT("이전 요청 처리 중입니다. 잠시 후 다시 시도하세요.");OnChanged.Broadcast();}return;}
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("operation"),Operation);Json->SetStringField(TEXT("target"),Target);Json->SetStringField(TEXT("text"),Text);
    Json->SetStringField(TEXT("presence"),Presence);Json->SetStringField(TEXT("context"),bLobbyVisible?TEXT("Lobby"):TEXT("Other"));
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(false,Body,bPresence);
}
void UProjectProject01SocialSubsystem::Send(bool bRead,const FString& Body,bool bPresence,const FString& Path,const FString& Verb)
{
    auto* Auth=AuthOf(this);if(!Auth||!Auth->IsSignedIn())return;
    const FString Token=Auth->GetAccessTokenForAuthenticatedRequest();
    auto Request=FHttpModule::Get().CreateRequest();(bRead?ReadRequest:bPresence?PresenceRequest:WriteRequest)=Request;
    Request->SetURL(Auth->GetApiBaseUrlForAuthenticatedRequest()+(Path.IsEmpty()?(bRead?TEXT("/api/social"):TEXT("/api/social/action")):Path));
    Request->SetVerb(Verb.IsEmpty()?(bRead?TEXT("GET"):TEXT("POST")):Verb);Request->SetTimeout(5);Request->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);
    FProjectProject01VersionContract::ApplyToRequest(Request);Request->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));if(!bRead)Request->SetContentAsString(Body);
    Request->OnProcessRequestComplete().BindWeakLambda(this,[this,bRead,bPresence,Token,Body](FHttpRequestPtr Req,FHttpResponsePtr Response,bool Connected)
    {
        auto& Pending=bRead?ReadRequest:bPresence?PresenceRequest:WriteRequest;if(Pending!=Req)return;Pending.Reset();
        auto* CurrentAuth=AuthOf(this);
        if(!CurrentAuth||!CurrentAuth->IsSignedIn()||Token!=CurrentAuth->GetAccessTokenForAuthenticatedRequest())return;
        TSharedPtr<FJsonObject> Json;
        if(Response.IsValid())FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json);
        bool Ok=Connected&&Response.IsValid()&&EHttpResponseCodes::IsOk(Response->GetResponseCode())&&Json.IsValid();
        if(Ok&&bRead)Snapshot=Json;
        if(!bPresence&&(!bRead||!Ok)){Status=Ok?TEXT("처리되었습니다."):TEXT("서버 요청에 실패했습니다.");if(Json.IsValid())Json->TryGetStringField(TEXT("message"),Status);}
        if(Ok&&!bRead&&!bPresence)
        {
            TSharedPtr<FJsonObject> Sent;
            FString Op;
            if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Sent)&&Sent->TryGetStringField(TEXT("operation"),Op))
            {
                if(Op==TEXT("Whisper")){if(Sent->GetStringField(TEXT("target")).Equals(ConversationAccount,ESearchCase::IgnoreCase))OnWhisperSent.Broadcast(Sent->GetStringField(TEXT("text")));if(!ConversationAccount.IsEmpty())Detail(2,TEXT("/api/social/conversations/")+FGenericPlatformHttp::UrlEncode(ConversationAccount));}
                if(Op==TEXT("ReadConversation")&&Sent->GetStringField(TEXT("text"))==PendingReadId)PendingReadId.Reset();
                if(Op==TEXT("AcceptInvite"))bRoomRefreshPending=true;
            }
        }
        if(!bRead&&!bPresence){TSharedPtr<FJsonObject> Sent;FString Op=TEXT("SaveProfile");if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Sent))Sent->TryGetStringField(TEXT("operation"),Op);OnActionCompleted.Broadcast(Op,Ok);}
        if(Response.IsValid()&&Response->GetResponseCode()==401){CurrentAuth->RefreshSession([](bool){});}
        if(Ok&&!bRead)Refresh();if(!bPresence)OnChanged.Broadcast();
    });
    if(!Request->ProcessRequest()){(bRead?ReadRequest:bPresence?PresenceRequest:WriteRequest).Reset();if(!bPresence){Status=TEXT("서버에 접속할 수 없습니다.");OnChanged.Broadcast();}}
}

void UProjectProject01SocialSubsystem::SearchPlayers(const FString& Query)
{
    if(Query.TrimStartAndEnd().Len()<2){Status=TEXT("검색어는 2자 이상 입력하세요.");OnChanged.Broadcast();return;}
    SearchResults.Reset();Detail(0,TEXT("/api/social/search?q=")+FGenericPlatformHttp::UrlEncode(Query.TrimStartAndEnd()));
}
void UProjectProject01SocialSubsystem::LoadProfile(const FString& Account)
{
    Profile.Reset();Detail(1,TEXT("/api/social/profiles/")+FGenericPlatformHttp::UrlEncode(Account));
}
void UProjectProject01SocialSubsystem::LoadRoomProfile(const FString& UserId)
{
    Profile.Reset();Detail(1,TEXT("/api/social/room-profiles/")+FGenericPlatformHttp::UrlEncode(UserId));
}
void UProjectProject01SocialSubsystem::LoadRoomContext(const FString& UserId)
{
    RoomContext.Reset();RoomContextUserId=UserId;
    Detail(3,TEXT("/api/social/room-profiles/")+FGenericPlatformHttp::UrlEncode(UserId));
}
void UProjectProject01SocialSubsystem::SubmitRoomReport(const FString& UserId,const FString& Reason,const FString& Details)
{
    if(!IsAuthenticated())return;
    if(WriteRequest.IsValid()){Status=TEXT("이전 요청 처리 중입니다. 잠시 후 다시 시도하세요.");OnChanged.Broadcast();return;}
    if(Reason.IsEmpty()||Details.TrimStartAndEnd().IsEmpty()||Details.Len()>1000){Status=TEXT("신고 사유를 선택하고 상세 내용을 1~1000자로 입력하세요.");OnChanged.Broadcast();return;}
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("operation"),TEXT("Report"));
    Json->SetStringField(TEXT("targetUserId"),UserId);Json->SetStringField(TEXT("reason"),Reason);Json->SetStringField(TEXT("details"),Details);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(false,Body,false,TEXT("/api/social/reports"));
}
void UProjectProject01SocialSubsystem::SubmitAccountReport(const FString& Account,const FString& Reason,const FString& Details)
{
    if(!IsAuthenticated())return;
    if(WriteRequest.IsValid()){Status=TEXT("이전 요청 처리 중입니다. 잠시 후 다시 시도하세요.");OnChanged.Broadcast();return;}
    const FString Target=Account.TrimStartAndEnd();
    if(Target.Len()<3||Target.Len()>32||Reason.IsEmpty()||Details.TrimStartAndEnd().IsEmpty()||Details.Len()>1000)
    {Status=TEXT("상대 계정 아이디, 신고 사유와 상세 내용(1~1000자)을 입력하세요.");OnChanged.Broadcast();return;}
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("operation"),TEXT("Report"));Json->SetStringField(TEXT("targetAccount"),Target);
    Json->SetStringField(TEXT("reason"),Reason);Json->SetStringField(TEXT("details"),Details);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(false,Body,false,TEXT("/api/social/reports"));
}
void UProjectProject01SocialSubsystem::OpenConversation(const FString& Account)
{
    if(ConversationRequest.IsValid()){ConversationRequest->OnProcessRequestComplete().Unbind();ConversationRequest->CancelRequest();ConversationRequest.Reset();}
    ConversationAccount=Account.TrimStartAndEnd();Conversation.Reset();PendingReadId.Reset();
    if(!ConversationAccount.IsEmpty()&&bLobbyVisible)Detail(2,TEXT("/api/social/conversations/")+FGenericPlatformHttp::UrlEncode(ConversationAccount));
}
void UProjectProject01SocialSubsystem::SaveProfile(const FString& Message,const FString& Bio,const FString& Accent,const FString& Title)
{
    if(!IsAuthenticated()||WriteRequest.IsValid())return;
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("statusMessage"),Message);Json->SetStringField(TEXT("bio"),Bio);Json->SetStringField(TEXT("accent"),Accent);Json->SetStringField(TEXT("title"),Title);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(false,Body,false,TEXT("/api/social/profile"),TEXT("PUT"));
}
void UProjectProject01SocialSubsystem::Detail(int32 Kind,const FString& Path)
{
    auto* Auth=AuthOf(this);if(!Auth||!Auth->IsSignedIn())return;
    auto& Pending=Kind==0?SearchRequest:Kind==1?ProfileRequest:Kind==2?ConversationRequest:RoomContextRequest;
    if(Pending.IsValid()){Pending->OnProcessRequestComplete().Unbind();Pending->CancelRequest();}
    const FString Token=Auth->GetAccessTokenForAuthenticatedRequest();auto Request=FHttpModule::Get().CreateRequest();Pending=Request;
    Request->SetURL(Auth->GetApiBaseUrlForAuthenticatedRequest()+Path);Request->SetVerb(TEXT("GET"));Request->SetTimeout(5);
    Request->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);FProjectProject01VersionContract::ApplyToRequest(Request);
    Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Kind,Token](FHttpRequestPtr Req,FHttpResponsePtr Response,bool Connected)
    {
        auto& Pending=Kind==0?SearchRequest:Kind==1?ProfileRequest:Kind==2?ConversationRequest:RoomContextRequest;if(Pending!=Req)return;Pending.Reset();
        auto* Auth=AuthOf(this);if(!Auth||!Auth->IsSignedIn()||Auth->GetAccessTokenForAuthenticatedRequest()!=Token)return;
        TSharedPtr<FJsonObject> Json;if(Response.IsValid())FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json);
        const bool Ok=Connected&&Response.IsValid()&&EHttpResponseCodes::IsOk(Response->GetResponseCode())&&Json.IsValid();
        if(Ok)(Kind==0?SearchResults:Kind==1?Profile:Kind==2?Conversation:RoomContext)=Json;
        else{Status=TEXT("정보를 불러오지 못했습니다.");if(Json.IsValid())Json->TryGetStringField(TEXT("message"),Status);}
        if(Kind==3)OnRoomContextReady.Broadcast(RoomContextUserId,Ok);
        OnChanged.Broadcast();
    });
    if(!Request->ProcessRequest()){Pending.Reset();Status=TEXT("서버에 연결할 수 없습니다.");if(Kind==3)OnRoomContextReady.Broadcast(RoomContextUserId,false);OnChanged.Broadcast();}
}
