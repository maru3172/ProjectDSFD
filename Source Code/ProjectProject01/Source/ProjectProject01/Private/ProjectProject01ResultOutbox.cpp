#include "ProjectProject01ResultOutbox.h"
#include "ProjectProject01VersionContract.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString Sha(const FString& Text)
{
    FTCHARToUTF8 Utf8(*Text);FSHAHash Hash;FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Hash.Hash);return Hash.ToString();
}
struct FPendingResult : TSharedFromThis<FPendingResult>
{
    FString Backend,Route,Body,Secret,Path;int32 Attempts=0;bool bFinished=false;TFunction<void(bool)> Completion;
    TUniquePtr<IFileHandle> Lease;
    void Finish(bool Success){if(bFinished)return;bFinished=true;Lease.Reset();Secret.Reset();if(Completion){auto Done=MoveTemp(Completion);Done(Success);}}
    void Try()
    {
        if(bFinished)return;
        ++Attempts;
        // An OS handle (not a stale lock-file flag) protects against another game process/backend worker.
        Lease.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*(Path+TEXT(".lock")),false,false));
        if(!Lease){Retry();return;}
        auto Request=FHttpModule::Get().CreateRequest();Request->SetURL(Backend+Route);Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"),Secret);
        FProjectProject01VersionContract::ApplyToRequest(Request);Request->SetTimeout(5);Request->SetContentAsString(Body);
        auto Self=AsShared();Request->OnProcessRequestComplete().BindLambda([Self](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected)
        {
            if(Self->bFinished)return;
            int32 Code=Connected&&Response?Response->GetResponseCode():0;
            if(Code>=200&&Code<300){IFileManager::Get().Delete(*Self->Path);Self->Finish(true);}
            else if(FProjectResultOutbox::IsTransient(Code)){Self->Lease.Reset();Self->Retry();}
            else
            {
                IFileManager::Get().Move(*(Self->Path+TEXT(".rejected")),*Self->Path,true);
                UE_LOG(LogTemp,Error,TEXT("Match outbox rejected HTTP %d; journal retained: %s"),Code,*Self->Path);Self->Finish(false);
            }
        });
        if(!Request->ProcessRequest()){Lease.Reset();Retry();}
    }
    void Retry()
    {
        if(bFinished)return;
        // Bound this live UI attempt. The journal remains available to the host worker indefinitely.
        if(Attempts>=8){UE_LOG(LogTemp,Warning,TEXT("Match result remains queued for backend recovery: %s"),*Path);Finish(false);return;}
        auto Self=AsShared();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float){Self->Try();return false;}),FMath::Min(30.0f,FMath::Pow(2.0f,Self->Attempts-1)));
    }
};
}
FString FProjectResultOutbox::Directory()
{
    FString Override;FParse::Value(FCommandLine::Get(),TEXT("ProjectProject01ResultOutbox="),Override);
    return Override.IsEmpty()?FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("MatchResultOutbox"))):FPaths::ConvertRelativePathToFull(Override);
}
bool FProjectResultOutbox::IsTransient(int32 Code){return Code==0||Code==408||Code==429||Code>=500;}
bool FProjectResultOutbox::IsProductionMatchId(const FString& MatchId)
{
    FGuid Parsed;return FGuid::Parse(MatchId,Parsed);
}
bool FProjectResultOutbox::Persist(const FString& Root,const FString& Backend,const FString& Route,const FString& Body,FString& OutPath)
{
    OutPath.Reset();
    if(Route!=TEXT("/api/server/matches/results")&&Route!=TEXT("/api/server/matches/complete")&&Route!=TEXT("/api/server/case-records/visit")&&Route!=TEXT("/api/server/main-quest/floor"))return false;
    TSharedPtr<FJsonObject> ParsedBody;FString MatchId;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),ParsedBody)||!ParsedBody.IsValid()||
        !ParsedBody->TryGetStringField(TEXT("matchId"),MatchId)||!IsProductionMatchId(MatchId))return false;
    auto Payload=MakeShared<FJsonObject>();Payload->SetNumberField(TEXT("schema"),1);Payload->SetStringField(TEXT("backend"),Backend);
    Payload->SetStringField(TEXT("route"),Route);Payload->SetStringField(TEXT("body"),Body);
    auto Headers=MakeShared<FJsonObject>();
    Headers->SetStringField(TEXT("X-ProjectProject01-Client-Build"),FProjectProject01VersionContract::GetClientBuildVersion());
    Headers->SetStringField(TEXT("X-ProjectProject01-Server-Build"),FProjectProject01VersionContract::GetDedicatedServerBuildVersion());
    Headers->SetStringField(TEXT("X-ProjectProject01-Api-Version"),FProjectProject01VersionContract::GetApiVersion());
    Headers->SetStringField(TEXT("X-ProjectProject01-Game-Data-Version"),FProjectProject01VersionContract::GetGameDataVersion());
    Headers->SetStringField(TEXT("X-ProjectProject01-Network-Protocol"),FProjectProject01VersionContract::GetNetworkProtocolVersion());
    Payload->SetObjectField(TEXT("headers"),Headers);FString Encoded;FJsonSerializer::Serialize(Payload,TJsonWriterFactory<>::Create(&Encoded));
    auto Envelope=MakeShared<FJsonObject>();Envelope->SetStringField(TEXT("payload"),Encoded);Envelope->SetStringField(TEXT("sha1"),Sha(Encoded));
    FString File;FJsonSerializer::Serialize(Envelope,TJsonWriterFactory<>::Create(&File));if(File.Len()>65536)return false;
    IFileManager::Get().MakeDirectory(*Root,true);OutPath=FPaths::Combine(Root,Sha(Encoded)+TEXT(".json"));
    if(IFileManager::Get().FileExists(*OutPath))
    {
        FString Existing;if(FFileHelper::LoadFileToString(Existing,*OutPath)&&Existing==File)return true;
        // Preserve a damaged journal instead of silently treating it as a durable enqueue.
        if(!IFileManager::Get().Move(*(OutPath+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".corrupt")),*OutPath,false))return false;
    }
    FString Temp=OutPath+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");
    if(!FFileHelper::SaveStringToFile(File,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))return false;
    if(IFileManager::Get().Move(*OutPath,*Temp,false,true,false,true))return true;
    IFileManager::Get().Delete(*Temp);return IFileManager::Get().FileExists(*OutPath);
}
void FProjectResultOutbox::Submit(const FString& Backend,const FString& Route,const FString& Body,const FString& Secret,TFunction<void(bool)> Completion)
{
    auto Pending=MakeShared<FPendingResult>();Pending->Backend=Backend;Pending->Route=Route;Pending->Body=Body;Pending->Secret=Secret;Pending->Completion=MoveTemp(Completion);
    if(!Persist(Directory(),Backend,Route,Body,Pending->Path)){UE_LOG(LogTemp,Error,TEXT("Cannot persist authoritative result journal."));Pending->Finish(false);return;}
    // Publish every journal in the GameMode's result batch before any request can
    // acknowledge match completion and let the host retire this ephemeral process.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Pending](float){Pending->Try();return false;}));
}
