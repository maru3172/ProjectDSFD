#include "ProjectMiniGames.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    UProjectProject01AuthSubsystem* Auth(const UProjectMiniGames* Self){auto* GI=Self->GetGameInstance();return GI?GI->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;}
}

void UProjectMiniGames::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UProjectProject01AuthSubsystem>();
    // A match starting (map change) or the account signing out pauses and saves the game first.
    PreLoadMapHandle=FCoreUObjectDelegates::PreLoadMap.AddUObject(this,&ThisClass::HandlePreLoadMap);
    if(auto* A=Auth(this))ClearingHandle=A->OnLocalSessionClearing.AddUObject(this,&ThisClass::SuspendActiveGame);
    if(auto* A=Auth(this))A->OnLocalSessionCleared.AddWeakLambda(this,[this]{Home.Reset();LocalSaves.Reset();bPhoneLandscape=false;});
}
void UProjectMiniGames::Deinitialize()
{
    FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
    if(auto* A=Auth(this)){A->OnLocalSessionClearing.Remove(ClearingHandle);A->OnLocalSessionCleared.RemoveAll(this);}
    Super::Deinitialize();
}
void UProjectMiniGames::HandlePreLoadMap(const FString&){SuspendActiveGame();bPhoneLandscape=false;}
void UProjectMiniGames::SetActiveGame(const void* Owner,TFunction<void()> PauseAndSave){ActiveOwner=Owner;ActivePause=MoveTemp(PauseAndSave);}
void UProjectMiniGames::ClearActiveGame(const void* Owner){if(ActiveOwner==Owner){ActiveOwner=nullptr;ActivePause=nullptr;}}
void UProjectMiniGames::SuspendActiveGame()
{
    // Taken out first: the callback may clear itself while it runs.
    if(auto Pause=MoveTemp(ActivePause)){ActivePause=nullptr;ActiveOwner=nullptr;Pause();}
}

void UProjectMiniGames::Send(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body,FReply Done)
{
    auto* A=Auth(this);
    if(!A||!A->IsSignedIn()){if(Done)Done(nullptr,TEXT("로그인이 필요합니다."));return;}
    const uint64 Generation=A->GetSessionGeneration();
    auto Request=FHttpModule::Get().CreateRequest();
    Request->SetURL(A->GetApiBaseUrlForAuthenticatedRequest()+Path);Request->SetVerb(Verb);Request->SetTimeout(6);
    Request->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+A->GetAccessTokenForAuthenticatedRequest());
    Request->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));FProjectProject01VersionContract::ApplyToRequest(Request);
    if(Body){FString Text;FJsonSerializer::Serialize(Body.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));Request->SetContentAsString(Text);}
    Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Generation,Done](FHttpRequestPtr,FHttpResponsePtr Response,bool bConnected)
    {
        const auto* Current=Auth(this);
        // A late answer for an account that has signed out (or another that signed in) is dropped.
        if(!Current||Current->GetSessionGeneration()!=Generation){return;}
        TSharedPtr<FJsonObject> Json;
        const bool bParsed=Response&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json)&&Json.IsValid();
        if(!Done)return;
        if(bConnected&&Response&&EHttpResponseCodes::IsOk(Response->GetResponseCode())&&bParsed){Done(Json,FString());return;}
        FString Message;if(bParsed)Json->TryGetStringField(TEXT("message"),Message);
        if(Message.IsEmpty())Message=!bConnected||!Response?TEXT("서버에 연결할 수 없습니다. 잠시 후 다시 시도하세요."):TEXT("요청을 처리하지 못했습니다.");
        Done(Json,Message);
    });
    Request->ProcessRequest();
}

void UProjectMiniGames::Refresh(FReply Done)
{
    Send(TEXT("GET"),TEXT("/api/minigames"),nullptr,[this,Done](TSharedPtr<FJsonObject> Json,const FString& Error){if(Error.IsEmpty())Home=Json;if(Done)Done(Json,Error);});
}
void UProjectMiniGames::Start(const FString& Game,const FString& Stage,FReply Done)
{
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("game"),Game);if(!Stage.IsEmpty())Body->SetStringField(TEXT("stage"),Stage);
    Send(TEXT("POST"),TEXT("/api/minigames/start"),Body,MoveTemp(Done));
}
void UProjectMiniGames::Save(const FString& RunId,int32 ActiveMs,const TSharedRef<FJsonObject>& State,FReply Done)
{
    LocalSaves.Add(RunId,TPair<int32,TSharedPtr<FJsonObject>>(ActiveMs,State));
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("runId"),RunId);Body->SetNumberField(TEXT("activeMs"),ActiveMs);Body->SetObjectField(TEXT("state"),State);
    Send(TEXT("POST"),TEXT("/api/minigames/save"),Body,[this,RunId,ActiveMs,Done](TSharedPtr<FJsonObject> Json,const FString& Error)
    {
        // The server has it now; a newer local save made meanwhile stays.
        if(Error.IsEmpty())if(const auto* Local=LocalSaves.Find(RunId);Local&&Local->Key==ActiveMs)LocalSaves.Remove(RunId);
        if(Done)Done(Json,Error);
    });
}
void UProjectMiniGames::SpotAnswer(const FString& RunId,int32 Round,const TArray<int32>& Picks,int32 ActiveMs,FReply Done)
{
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("runId"),RunId);Body->SetNumberField(TEXT("round"),Round);Body->SetNumberField(TEXT("activeMs"),ActiveMs);
    TArray<TSharedPtr<FJsonValue>> List;for(const int32 P:Picks)List.Add(MakeShared<FJsonValueNumber>(P));Body->SetArrayField(TEXT("picks"),List);
    Send(TEXT("POST"),TEXT("/api/minigames/spot/answer"),Body,MoveTemp(Done));
}
void UProjectMiniGames::Finish(const FString& RunId,bool bCleared,int32 ActiveMs,int32 Battery,const TArray<FString>& Moves,FReply Done,int32 Strikes,int32 Hints,int32 Score)
{
    LocalSaves.Remove(RunId);
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("runId"),RunId);Body->SetBoolField(TEXT("cleared"),bCleared);
    Body->SetNumberField(TEXT("activeMs"),ActiveMs);Body->SetNumberField(TEXT("battery"),Battery);Body->SetNumberField(TEXT("strikes"),Strikes);Body->SetNumberField(TEXT("hints"),Hints);Body->SetNumberField(TEXT("score"),Score);
    TArray<TSharedPtr<FJsonValue>> List;for(const FString& M:Moves)List.Add(MakeShared<FJsonValueString>(M));Body->SetArrayField(TEXT("moves"),List);
    Send(TEXT("POST"),TEXT("/api/minigames/finish"),Body,MoveTemp(Done));
}
void UProjectMiniGames::ListenAnswer(const FString& RunId,int32 Round,int32 Direction,int32 ActiveMs,FReply Done)
{
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("runId"),RunId);Body->SetNumberField(TEXT("round"),Round);
    Body->SetNumberField(TEXT("direction"),Direction);Body->SetNumberField(TEXT("activeMs"),ActiveMs);
    Send(TEXT("POST"),TEXT("/api/minigames/listen/answer"),Body,MoveTemp(Done));
}
void UProjectMiniGames::Abandon(const FString& Game,FReply Done)
{
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("game"),Game);
    Send(TEXT("POST"),TEXT("/api/minigames/abandon"),Body,MoveTemp(Done));
}
void UProjectMiniGames::Leaderboard(const FString& Game,const FString& Board,bool bWeekly,FReply Done)
{
    Send(TEXT("GET"),FString::Printf(TEXT("/api/minigames/leaderboard?game=%s&board=%s&period=%s"),*FGenericPlatformHttp::UrlEncode(Game),*FGenericPlatformHttp::UrlEncode(Board),bWeekly?TEXT("weekly"):TEXT("all")),nullptr,MoveTemp(Done));
}
