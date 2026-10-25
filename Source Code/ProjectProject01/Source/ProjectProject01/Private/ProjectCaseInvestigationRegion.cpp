#include "ProjectCaseInvestigationRegion.h"
#include "ProjectProject01GameInstance.h"
#include "MultiplayTestGameMode.h"
#include "MultiplayTestPlayerController.h"
#include "PlayerCharacter.h"
#include "ProjectProject01ResultOutbox.h"
#include "ProjectProject01VersionContract.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

AProjectCaseInvestigationRegion::AProjectCaseInvestigationRegion()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.5f;PrimaryActorTick.bAllowTickOnDedicatedServer=true;
    bReplicates=false;Region=CreateDefaultSubobject<UBoxComponent>(TEXT("EvidenceRegion"));SetRootComponent(Region);
    Region->InitBoxExtent(FVector(120,120,120));Region->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Region->SetCollisionResponseToAllChannels(ECR_Ignore);Region->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);Region->SetGenerateOverlapEvents(true);
}
void AProjectCaseInvestigationRegion::BeginPlay()
{
    Super::BeginPlay();SetActorTickEnabled(HasAuthority()&&GetNetMode()!=NM_Standalone&&!RegionId.IsNone());
}
void AProjectCaseInvestigationRegion::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(!HasAuthority()||GetNetMode()==NM_Client||RegionId.IsNone())return;
    const auto* Mode=GetWorld()->GetAuthGameMode<AMultiplayTestGameMode>();
    if(!Mode||!Mode->IsAuthoritativeMatchInProgress())return;
    TArray<AActor*> Overlaps;Region->GetOverlappingActors(Overlaps,APawn::StaticClass());
    for(auto* Actor:Overlaps)if(auto* Pawn=Cast<APawn>(Actor))Submit(Pawn);
}
bool AProjectCaseInvestigationRegion::Submit(APawn* Pawn)
{
    auto* PC=Pawn?Cast<AMultiplayTestPlayerController>(Pawn->GetController()):nullptr;
    auto* GI=GetGameInstance<UProjectProject01GameInstance>();
    if(!HasAuthority()||!PC||PC->IsServerControlledBot()||PC->GetPawn()!=Pawn||!Region->IsOverlappingActor(Pawn)||!GI||!GI->IsBackendUrlAllowed())return false;
    const FString& PlayerRole=PC->GetAuthenticatedRole();
    if(PlayerRole==TEXT("Survivor"))
    {const auto* Survivor=Cast<APlayerCharacter>(Pawn);if(!Survivor||Survivor->IsGameOver()||PC->GetSurvivorState()!=EMultiplayTestSurvivorState::Active)return false;}
    else if(PlayerRole!=TEXT("Mannequin"))return false;
    const FString& Match=PC->GetAuthenticatedMatchId();const FString& User=PC->GetAuthenticatedUserId();
    if(!FProjectResultOutbox::IsProductionMatchId(Match)||User.IsEmpty())return false;
    const FString Key=Match+TEXT(":")+User;const double Now=GetWorld()->GetTimeSeconds();
    if(Completed.Contains(Key)||Pending.Contains(Key)||RetryAfter.FindRef(Key)>Now)return false;
    const FString Secret=GI->LoadGameServerSharedSecret();if(Secret.Len()<32)return false;
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("matchId"),Match);Json->SetStringField(TEXT("userId"),User);
    Json->SetStringField(TEXT("region"),RegionId.ToString());
    Json->SetStringField(TEXT("map"),FPackageName::GetLongPackagePath(GetWorld()->GetPackage()->GetName())/UGameplayStatics::GetCurrentLevelName(this,true));
    Json->SetNumberField(TEXT("serverPort"),GetWorld()->URL.Port);Json->SetStringField(TEXT("observedAtUtc"),FDateTime::UtcNow().ToIso8601());
    FString Body;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));Pending.Add(Key);
    TFunction<void(bool)> Finish=[Weak=TWeakObjectPtr<AProjectCaseInvestigationRegion>(this),Key](bool Success)
    {
        if(auto* Self=Weak.Get())
        {Self->Pending.Remove(Key);if(Success)Self->Completed.Add(Key);else if(Self->GetWorld())Self->RetryAfter.Add(Key,Self->GetWorld()->GetTimeSeconds()+120);}
    };
    // An ordinary locked part/week limit is not a failed durable result. Keep
    // those attempts off disk; only a transient delivery failure enters recovery.
    const FString Backend=GI->SecurityApiBaseUrl;auto Request=FHttpModule::Get().CreateRequest();
    Request->SetURL(Backend+TEXT("/api/server/case-records/visit"));Request->SetVerb(TEXT("POST"));Request->SetTimeout(5);
    Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"),Secret);FProjectProject01VersionContract::ApplyToRequest(Request);Request->SetContentAsString(Body);
    Request->OnProcessRequestComplete().BindLambda([Backend,Body,Secret,Finish,Controller=TWeakObjectPtr<AMultiplayTestPlayerController>(PC),Match](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected)
    {
        const int32 Code=Connected&&Response?Response->GetResponseCode():0;
        if(Code>=200&&Code<300)
        {
            TSharedPtr<FJsonObject> Result;double Count=0;
            if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Result)&&Result&&Result->TryGetNumberField(TEXT("newCount"),Count)&&Count>0)
                if(auto* Owner=Controller.Get();Owner&&Owner->GetAuthenticatedMatchId()==Match)Owner->ClientNotifyCaseDiscovery(FMath::Clamp(int32(Count),1,9));
            Finish(true);
        }
        else if(FProjectResultOutbox::IsTransient(Code))FProjectResultOutbox::Submit(Backend,TEXT("/api/server/case-records/visit"),Body,Secret,Finish);
        else Finish(false);
    });
    if(!Request->ProcessRequest()){Request->OnProcessRequestComplete().Unbind();FProjectResultOutbox::Submit(Backend,TEXT("/api/server/case-records/visit"),Body,Secret,MoveTemp(Finish));}
    return true;
}
