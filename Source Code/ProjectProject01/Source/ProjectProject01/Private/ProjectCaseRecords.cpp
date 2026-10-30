#include "ProjectCaseRecords.h"
#include "ProjectCaseStoryArchive.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01LoginWidget.h"
#include "ProjectProject01VersionContract.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProjectAnnouncementSubsystem.h"

namespace
{
    const FString CampaignAchievementSlotName=TEXT("ProjectDFSD_CampaignAchievements");
    bool ValidCampaign(const FString& Code){return Code==TEXT("campaign_checkpoint")||Code==TEXT("campaign_escape")||Code==TEXT("campaign_complete");}
    UProjectProject01AuthSubsystem* Auth(const UProjectCaseRecords* Self){auto* GI=Self->GetGameInstance();return GI?GI->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;}
}
bool UProjectCaseRecords::ShouldCreateSubsystem(UObject* Outer)const{return !IsRunningDedicatedServer()&&Super::ShouldCreateSubsystem(Outer);}
void UProjectCaseRecords::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Collection.InitializeDependency<UProjectProject01AuthSubsystem>();
    bPersistence=!FApp::IsUnattended();
#if WITH_EDITOR
    bPersistence=bPersistence&&!GIsEditor;
#endif
    if(bPersistence)for(const auto& Candidate:{CampaignAchievementSlotName,CampaignAchievementSlotName+TEXT("_GenerationA"),CampaignAchievementSlotName+TEXT("_GenerationB")})
    {
        if(!UGameplayStatics::DoesSaveGameExist(Candidate,0))continue;
        auto* Saved=Cast<UProjectCampaignAchievementSave>(UGameplayStatics::LoadGameFromSlot(Candidate,0));FGuid SavedId;
        if(Saved&&Saved->Version==1&&Saved->Generation>=0&&Saved->Generation<1000000000000LL&&Saved->Earned.Num()<=64&&FGuid::Parse(Saved->SaveId,SavedId)&&SavedId.IsValid()&&(!Local||Saved->Generation>Local->Generation))
        {Local=Saved;CurrentLocalSlot=Candidate;}
    }
    if(!Local||Local->Version!=1)Local=NewObject<UProjectCampaignAchievementSave>(this);
    FGuid Id;if(!FGuid::Parse(Local->SaveId,Id)||!Id.IsValid())Local->SaveId=FGuid::NewGuid().ToString();
    for(auto It=Local->Earned.CreateIterator();It;++It)if(!ValidCampaign(*It))It.RemoveCurrent();
    if(Local->PublicStoryArchivesJson.Len()<=8*1024*1024)FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Local->PublicStoryArchivesJson),StoryArchives);
    if(auto* A=Auth(this))A->OnLocalSessionCleared.AddUObject(this,&ThisClass::ClearSession);
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::Tick),.5f);
}
void UProjectCaseRecords::Deinitialize()
{
    if(AchievementRateRequest){AchievementRateRequest->OnProcessRequestComplete().Unbind();AchievementRateRequest->CancelRequest();AchievementRateRequest.Reset();}
    if(ArchiveRequest){ArchiveRequest->OnProcessRequestComplete().Unbind();ArchiveRequest->CancelRequest();ArchiveRequest.Reset();}
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);if(auto* A=Auth(this))A->OnLocalSessionCleared.RemoveAll(this);ClearSession();Super::Deinitialize();
}
void UProjectCaseRecords::ClearSession()
{
    ClearPartyAdmission();
    if(Request){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}
    Snapshot.Reset();DrawResult.Reset();LastReceipt.Reset();PendingDrawId.Reset();PendingDrawOperation.Reset();PollIn=0;bImportAttempted=false;Status.Reset();OnChanged.Broadcast(); // Never erase the offline achievement save.
    AchievementAccount.Reset();SeenOnlineAchievements.Reset();
    QuestAccount.Reset();SeenQuestRevision=0;
}
bool UProjectCaseRecords::IsAuthenticated()const{const auto* A=Auth(this);return A&&A->IsSignedIn();}
bool UProjectCaseRecords::Tick(float Delta)
{
    if(!IsAuthenticated()){if(Snapshot)ClearSession();return true;}
    TickPartyAdmission(Delta);
    if(!Request&&!bImportAttempted&&CanImport()){bImportAttempted=true;ImportCampaign();return true;}
    PollIn-=Delta;if(PollIn<=0){PollIn=15;Refresh();}return true;
}
void UProjectCaseRecords::Refresh(){if(IsAuthenticated()&&!Request)Send(TEXT("/api/case-records"));}
void UProjectCaseRecords::ApplyDrawResponse(const TSharedPtr<FJsonObject>& Response)
{
    if(!Response)return;
    const TSharedPtr<FJsonObject>* State=nullptr,*Draw=nullptr,*Result=nullptr,*Collection=nullptr;
    const bool FullState=Response->TryGetObjectField(TEXT("snapshot"),State);if(FullState)Snapshot=*State;
    if(Snapshot&&Snapshot->TryGetObjectField(TEXT("collection"),Collection)&&Response->TryGetObjectField(TEXT("draw"),Draw))(*Collection)->SetObjectField(TEXT("draw"),*Draw);
    if(Response->TryGetObjectField(TEXT("result"),Result))DrawResult=*Result;
    PendingDrawId.Reset();PendingDrawOperation.Reset();PollIn=FullState?15:0;
}
void UProjectCaseRecords::DrawAction(const FString& Operation,int32 Count)
{
    if(!PrepareWrite()||!Snapshot)return;
    if(Operation==TEXT("Exchange")){if(Count<1||Count>2000000)return;}
    else if(Operation==TEXT("EditorReset"))
    {
#if WITH_EDITOR
        if(!GIsEditor||Count!=1)return;
#else
        return;
#endif
    }
    else if(Operation!=TEXT("Draw")||(Count!=1&&Count!=5))return;
    const TSharedPtr<FJsonObject>* Collection=nullptr,*Draw=nullptr;
    if(!Snapshot->TryGetObjectField(TEXT("collection"),Collection)||!(*Collection)->TryGetObjectField(TEXT("draw"),Draw))return;
    if(!PendingDrawId.IsEmpty()&&(PendingDrawOperation!=Operation||PendingDrawCount!=Count))
    {Status=TEXT("이전 요청의 결과를 먼저 확인하세요.");OnChanged.Broadcast();return;}
    if(PendingDrawId.IsEmpty()){PendingDrawId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);PendingDrawOperation=Operation;PendingDrawCount=Count;(*Draw)->TryGetNumberField(TEXT("revision"),PendingDrawRevision);}
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("operation"),Operation);Json->SetNumberField(TEXT("count"),Count);Json->SetStringField(TEXT("requestId"),PendingDrawId);Json->SetNumberField(TEXT("revision"),PendingDrawRevision);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/case-records/draw"),Body);
}
void UProjectCaseRecords::SaveFeatured(const TArray<FString>& Codes,const FString& Bio)
{
    if(!PrepareWrite())return;auto Json=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Values;
    for(const auto& Code:Codes)Values.Add(MakeShared<FJsonValueString>(Code));Json->SetArrayField(TEXT("codes"),Values);Json->SetStringField(TEXT("bio"),Bio);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/case-records/featured"),Body);
}
TSharedPtr<FJsonObject> UProjectCaseRecords::GetAchievementRates()const
{const auto* A=Auth(this);return A&&A->GetApiBaseUrlForAuthenticatedRequest()==AchievementRateBase?AchievementRates:nullptr;}
void UProjectCaseRecords::RefreshAchievementRates()
{
    if(FApp::IsUnattended())return;
    auto* A=Auth(this);if(!A)return;const FString Base=A->GetApiBaseUrlForAuthenticatedRequest();
    if(AchievementRateBase!=Base)
    {
        if(AchievementRateRequest){AchievementRateRequest->OnProcessRequestComplete().Unbind();AchievementRateRequest->CancelRequest();AchievementRateRequest.Reset();}
        AchievementRates.Reset();AchievementRateBase=Base;LastAchievementRateFetchAt=-100;
    }
    if(AchievementRateRequest||FPlatformTime::Seconds()-LastAchievementRateFetchAt<30)return;
    LastAchievementRateFetchAt=FPlatformTime::Seconds();auto Pending=FHttpModule::Get().CreateRequest();AchievementRateRequest=Pending;
    Pending->SetURL(Base+TEXT("/api/case-records/achievement-rates"));Pending->SetVerb(TEXT("GET"));Pending->SetTimeout(5);FProjectProject01VersionContract::ApplyToRequest(Pending);
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this,Base](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Connected)
    {
        if(AchievementRateRequest!=Completed)return;AchievementRateRequest.Reset();
        const auto* Current=Auth(this);if(!Current||Current->GetApiBaseUrlForAuthenticatedRequest()!=Base)return;
        TSharedPtr<FJsonObject> Json;AchievementRates.Reset();
        if(Connected&&Response&&EHttpResponseCodes::IsOk(Response->GetResponseCode())&&Response->GetContentLength()<=65536&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json)&&Json&&Json->HasTypedField<EJson::Array>(TEXT("achievements")))AchievementRates=Json;
        OnChanged.Broadcast();
    });
    if(!Pending->ProcessRequest())AchievementRateRequest.Reset();
}
void UProjectCaseRecords::RefreshStoryArchives()
{
    if(FApp::IsUnattended())return;
    auto* A=Auth(this);if(!A||ArchiveRequest||FPlatformTime::Seconds()-LastArchiveFetchAt<30)return;
    LastArchiveFetchAt=FPlatformTime::Seconds();auto Pending=FHttpModule::Get().CreateRequest();ArchiveRequest=Pending;
    Pending->SetURL(A->GetApiBaseUrlForAuthenticatedRequest()+TEXT("/api/case-records/story-archives"));Pending->SetVerb(TEXT("GET"));Pending->SetTimeout(5);FProjectProject01VersionContract::ApplyToRequest(Pending);
    // This endpoint contains only publicly archived seasons. Never persist the account snapshot or unfinished stories.
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Connected)
    {
        if(ArchiveRequest!=Completed)return;ArchiveRequest.Reset();TSharedPtr<FJsonObject> Json;
        if(Connected&&Response&&EHttpResponseCodes::IsOk(Response->GetResponseCode())&&Response->GetContentLength()<=16*1024*1024&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json)&&Json&&Json->HasTypedField<EJson::Array>(TEXT("archives")))
        {
            FString Encoded;TSharedPtr<FJsonObject> Merged;
            if(ProjectCaseStoryArchive::Merge(StoryArchives,Json,Merged,Encoded))
            {StoryArchives=Merged;if(Local&&Local->PublicStoryArchivesJson!=Encoded){Local->PublicStoryArchivesJson=Encoded;PersistLocal();}}
        }
        OnChanged.Broadcast();
    });
    if(!Pending->ProcessRequest())ArchiveRequest.Reset();
}
bool UProjectCaseRecords::PrepareWrite()
{
    if(!IsAuthenticated())return false;
    if(Request&&Request->GetVerb()==TEXT("GET")){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}
    return !Request;
}
bool UProjectCaseRecords::IsWriting()const{return Request&&Request->GetVerb()!=TEXT("GET");}
void UProjectCaseRecords::Action(const FString& Op,const FString& Id,const FString& Mode,int32 ExpectedRerolls)
{
    if(Op==TEXT("DebugAddPoints"))
    {
#if WITH_EDITOR
        if(!GIsEditor)return;
#else
        return;
#endif
    }
    if(!PrepareWrite())return;auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("action"),Op);Json->SetStringField(TEXT("id"),Id);Json->SetStringField(TEXT("mode"),Mode);
    if(ExpectedRerolls>=0)Json->SetNumberField(TEXT("expectedRerolls"),ExpectedRerolls);
    if((Op==TEXT("RerollMission")||Op==TEXT("ClaimMission"))&&Snapshot)
    {
        const TArray<TSharedPtr<FJsonValue>>* Missions=nullptr;
        if(Snapshot->TryGetArrayField(TEXT("missions"),Missions))for(const auto& Value:*Missions)
        {
            const auto Mission=Value->AsObject();FString Slot,MatchMode,Period;
            if(Mission&&Mission->TryGetStringField(TEXT("id"),Slot)&&Mission->TryGetStringField(TEXT("mode"),MatchMode)&&Slot==Id&&MatchMode==Mode&&Mission->TryGetStringField(TEXT("period"),Period))
            {Json->SetStringField(TEXT("expectedPeriod"),Period);break;}
        }
    }
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/case-records/action"),Body);
}
void UProjectCaseRecords::PartyAction(const FString& Op,const FString& Target,const FString& Mode,const FString& Role)
{
    if(!PrepareWrite())return;auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("action"),Op);Json->SetStringField(TEXT("target"),Target);Json->SetStringField(TEXT("mode"),Mode);Json->SetStringField(TEXT("role"),Role);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/parties/action"),Body);
}
bool UProjectCaseRecords::CanImport()const
{
    if(!IsAuthenticated()||!Local||Local->Earned.IsEmpty()||!Snapshot)return false;
    FString Account;Snapshot->TryGetStringField(TEXT("userId"),Account);
    return !Account.IsEmpty()&&(Local->BoundAccountId.IsEmpty()||Local->BoundAccountId==Account);
}
void UProjectCaseRecords::ImportCampaign()
{
    if(!CanImport()||!PrepareWrite())return;bImportAttempted=true;
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("saveId"),Local->SaveId);Json->SetBoolField(TEXT("confirmed"),true);
    TArray<TSharedPtr<FJsonValue>> Codes;for(const auto& Code:Local->Earned)Codes.Add(MakeShared<FJsonValueString>(Code));Json->SetArrayField(TEXT("codes"),Codes);
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/case-records/import-campaign"),Body);
}
void UProjectCaseRecords::Send(const FString& Path,const FString& Body)
{
    auto* A=Auth(this);if(!A||!A->IsSignedIn()||Request)return;
    const uint64 Generation=A->GetSessionGeneration();
    auto Pending=FHttpModule::Get().CreateRequest();Request=Pending;
    Pending->SetURL(A->GetApiBaseUrlForAuthenticatedRequest()+Path);Pending->SetVerb(Body.IsEmpty()?TEXT("GET"):TEXT("POST"));Pending->SetTimeout(5);
    Pending->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+A->GetAccessTokenForAuthenticatedRequest());Pending->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));FProjectProject01VersionContract::ApplyToRequest(Pending);
#if WITH_EDITOR
    if(GIsEditor)Pending->SetHeader(TEXT("X-ProjectProject01-Client"),TEXT("Editor"));
#endif
    if(!Body.IsEmpty())Pending->SetContentAsString(Body);
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this,Generation,Path](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Connected)
    {
        if(Request!=Completed)return;Request.Reset();const auto* Current=Auth(this);if(!Current||!Current->IsSignedIn()||Current->GetSessionGeneration()!=Generation)return;
        if(Response&&Response->GetResponseCode()==401){if(auto* A=Auth(this))A->InvalidateSessionFromServerFailure(TEXT("로그인이 만료되었습니다. 다시 로그인하세요."));return;}
        TSharedPtr<FJsonObject> Json;const bool Ok=Connected&&Response&&EHttpResponseCodes::IsOk(Response->GetResponseCode())&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json)&&Json.IsValid();
        if(Ok)
        {
            if(Path==TEXT("/api/case-records"))
            {
                Snapshot=Json;
                const TSharedPtr<FJsonObject>* Season=nullptr,*Investigation=nullptr;
                if(Snapshot->TryGetObjectField(TEXT("season"),Season)&&(*Season)->TryGetObjectField(TEXT("investigation"),Investigation))
                {bool Ended=false;if((*Investigation)->TryGetBoolField(TEXT("ended"),Ended)&&Ended)RefreshStoryArchives();}
                // A party leader can atomically admit the whole party. Members discover the
                // authoritative membership without submitting a second (duplicate) join.
                const TSharedPtr<FJsonObject>* Party=nullptr;FString Room;
                if(Snapshot->TryGetObjectField(TEXT("partyState"),Party)&&(*Party)->TryGetStringField(TEXT("currentRoomId"),Room)&&!Room.IsEmpty())
                    if(auto* Lobby=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();Lobby&&!Lobby->HasCurrentRoom()&&!Lobby->IsRequestInFlight())Lobby->RefreshCurrentRoom();
            }
            else if(Path==TEXT("/api/case-records/draw"))
            {
                ApplyDrawResponse(Json);
            }
            else if(Path==TEXT("/api/case-records/featured")){PollIn=0;}
            else if(Path==TEXT("/api/parties/action"))
            {const TSharedPtr<FJsonObject>* Party=nullptr;if(Snapshot&&Json->TryGetObjectField(TEXT("partyState"),Party))Snapshot->SetObjectField(TEXT("partyState"),*Party);PollIn=0;}
            else if(Path.EndsWith(TEXT("import-campaign")))
            {if(Snapshot)Snapshot->TryGetStringField(TEXT("userId"),Local->BoundAccountId);PersistLocal();PollIn=0;}
            else {const TSharedPtr<FJsonObject>* State=nullptr;if(Json->TryGetObjectField(TEXT("snapshot"),State))Snapshot=*State;}
            Status=Path==TEXT("/api/case-records")?FString():TEXT("처리되었습니다.");
            if(Path==TEXT("/api/case-records/draw")){FString Operation;if(DrawResult&&DrawResult->TryGetStringField(TEXT("operation"),Operation)&&Operation==TEXT("EditorReset"))Status=TEXT("이 계정의 뽑기 획득 기록을 초기화했습니다.");}
            ObserveAchievements();
            ObserveMainQuest();
            const TSharedPtr<FJsonObject>* Receipt=nullptr;
            if(Json->TryGetObjectField(TEXT("receipt"),Receipt)&&Receipt&&Receipt->IsValid())
            {LastReceipt=*Receipt;OnReceipt.Broadcast();}
            else if(Path==TEXT("/api/case-records/draw"))
            {
                FString Operation;double Count=0;
                if(DrawResult&&DrawResult->TryGetStringField(TEXT("operation"),Operation)&&Operation==TEXT("Exchange")&&DrawResult->TryGetNumberField(TEXT("count"),Count)&&Count>0)
                {
                    auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),TEXT("뽑기 재화"));Item->SetNumberField(TEXT("count"),Count);
                    LastReceipt=MakeShared<FJsonObject>();LastReceipt->SetStringField(TEXT("title"),TEXT("구매하셨습니다"));
                    LastReceipt->SetArrayField(TEXT("items"),{MakeShared<FJsonValueObject>(Item)});OnReceipt.Broadcast();
                }
            }
        }
        else
        {
            if(Path==TEXT("/api/case-records/draw"))
            {if(Response&&Response->GetResponseCode()>=400&&Response->GetResponseCode()<500){PendingDrawId.Reset();PendingDrawOperation.Reset();}}
            Status=TEXT("서버 응답을 확인하지 못했습니다. 다시 시도하세요.");if(Response&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json)&&Json)Json->TryGetStringField(TEXT("message"),Status);
            if(Response&&Response->GetResponseCode()==409)PollIn=0;
        }
        OnChanged.Broadcast();
    });
    if(!Body.IsEmpty())OnChanged.Broadcast();
    if(!Pending->ProcessRequest()){Request.Reset();Status=TEXT("요청을 시작하지 못했습니다.");OnChanged.Broadcast();}
}
bool UProjectCaseRecords::RecordCampaignAchievement(FName Code)
{
    if(!Local||!ValidCampaign(Code.ToString())||!GetWorld()||GetWorld()->GetNetMode()!=NM_Standalone||GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>())return false;
    if(Local->Earned.Contains(Code.ToString()))return true;Local->Earned.Add(Code.ToString());bImportAttempted=false;PersistLocal();
    if(auto* Notices=GetGameInstance()->GetSubsystem<UProjectAnnouncementSubsystem>())Notices->NotifyAchievement(Code.ToString());
    OnChanged.Broadcast();return true;
}
void UProjectCaseRecords::ObserveAchievements()
{
    if(!Snapshot)return;FString Account;Snapshot->TryGetStringField(TEXT("userId"),Account);if(Account.IsEmpty())return;
    const bool First=AchievementAccount!=Account;if(First){AchievementAccount=Account;SeenOnlineAchievements.Reset();}
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;if(!Snapshot->TryGetArrayField(TEXT("achievements"),Entries))return;
    for(const auto& Value:*Entries)
    {
        FString Code;const auto Entry=Value->AsObject();if(!Entry||!Entry->TryGetStringField(TEXT("code"),Code)||SeenOnlineAchievements.Contains(Code))continue;
        SeenOnlineAchievements.Add(Code);
        if(!First&&!GetLocalAchievements().Contains(Code))if(auto* Notices=GetGameInstance()->GetSubsystem<UProjectAnnouncementSubsystem>())Notices->NotifyAchievement(Code);
    }
}
bool UProjectCaseRecords::HasCampaignAchievement(FName Code)const{return Local&&Local->Earned.Contains(Code.ToString());}
const TSet<FString>& UProjectCaseRecords::GetLocalAchievements()const{static const TSet<FString> Empty;return Local?Local->Earned:Empty;}
void UProjectCaseRecords::PersistLocal()
{
    if(!bPersistence||!Local)return;if(bSaving){bSaveDirty=true;return;}bSaving=true;bSaveDirty=false;
    const FString Target=CampaignAchievementSlotName+(CurrentLocalSlot.EndsWith(TEXT("_GenerationA"))?TEXT("_GenerationB"):TEXT("_GenerationA"));
    ++Local->Generation;
    FAsyncSaveGameToSlotDelegate Complete;Complete.BindWeakLambda(this,[this,Target](const FString&,int32,bool Success)
    {bSaving=false;if(!Success){Status=TEXT("로컬 업적·도전과제 저장에 실패했습니다. 마지막 정상 기록은 유지됩니다.");OnChanged.Broadcast();}else{CurrentLocalSlot=Target;if(bSaveDirty)PersistLocal();}});
    // Never overwrite the newest valid generation; a failed write cannot erase every earned achievement.
    UGameplayStatics::AsyncSaveGameToSlot(Local,Target,0,Complete);
}
void AMultiplayTestPlayerController::ClientNotifyCaseDiscovery_Implementation(int32 Count)
{
    if(!IsLocalController()||GetMatchPhase()!=EMultiplayTestMatchPhase::InProgress||Count<1||Count>9)return;
    if(auto* Notice=CreateWidget<UProjectProject01TransientNoticeWidget>(this))
    {Notice->AddToViewport(160);Notice->ShowNotice(FString::Printf(TEXT("사건 기록 조각 %d개를 발견했습니다."),Count),2.f,1.5f);}
    if(auto* GI=GetGameInstance())if(auto* R=GI->GetSubsystem<UProjectCaseRecords>())R->Refresh();
}
