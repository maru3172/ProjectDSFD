#include "ProjectCaseRecords.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectAnnouncementSubsystem.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01ResultOutbox.h"
#include "Interfaces/IHttpRequest.h"
#include "ProjectDFSDUITheme.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& J,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;return J&&J->TryGetObjectField(K,V)?*V:nullptr;}
    FString Str(const TSharedPtr<FJsonObject>& J,const TCHAR* K){FString V;if(J)J->TryGetStringField(K,V);return V;}
    double Num(const TSharedPtr<FJsonObject>& J,const TCHAR* K){double V=0;if(J)J->TryGetNumberField(K,V);return V;}
    bool Flag(const TSharedPtr<FJsonObject>& J,const TCHAR* K){bool V=false;if(J)J->TryGetBoolField(K,V);return V;}
    TArray<TSharedPtr<FJsonValue>> Rows(const TSharedPtr<FJsonObject>& J,const TCHAR* K){const TArray<TSharedPtr<FJsonValue>>* V=nullptr;return J&&J->TryGetArrayField(K,V)?*V:TArray<TSharedPtr<FJsonValue>>{};}
    TSharedRef<STextBlock> Text(const FString& Value,int32 Size=16){return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true);}
    const FButtonStyle& QuestButtonStyle(bool Selected=false)
    {
        static const FButtonStyle Normal=[] {auto S=ProjectDFSDUI::ButtonStyle();S.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return S;}();
        static const FButtonStyle Active=[] {auto S=ProjectDFSDUI::ButtonStyle(true);S.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));return S;}();
        return Selected?Active:Normal;
    }
    TSharedRef<SButton> Button(const FString& Value,TFunction<void()> Action,bool Enabled=true){return SNew(SButton).ButtonStyle(&QuestButtonStyle()).ContentPadding(FMargin(10,6)).IsEnabled(Enabled).OnClicked_Lambda([Action=MoveTemp(Action)]{Action();return FReply::Handled();})[Text(Value)];}
}
void UProjectCaseRecords::MainQuestAction(const FString& Action,const FString& Role)
{
    if(!IsAuthenticated())return;
    const bool Tutorial=Action==TEXT("TutorialComplete");
    if(Tutorial)
    {
        if(Role!=TEXT("Survivor")&&Role!=TEXT("Mannequin"))return;
        PendingTutorialRole=Role;PendingTutorialGeneration=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>()->GetSessionGeneration();
    }
    const auto Quest=Obj(Snapshot,TEXT("mainQuest"));const auto Steps=Rows(Quest,TEXT("steps"));const int32 Index=int32(Num(Quest,TEXT("stage")));
    if(!Tutorial&&!Steps.IsValidIndex(Index))return;
    if(!PrepareWrite())return;
    const FString Id=Tutorial?(Role==TEXT("Survivor")?TEXT("survivor_tutorial"):TEXT("mannequin_tutorial")):Str(Steps[Index]->AsObject(),TEXT("id"));
    if(Id!=TEXT("survivor_tutorial")&&Id!=TEXT("mannequin_tutorial"))return;
    ObserveMainQuest(); // Establish the pre-write baseline before a completion can be returned.
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),Action);J->SetStringField(TEXT("expectedStep"),Id);J->SetStringField(TEXT("role"),Role);
    TutorialRetryAt=FPlatformTime::Seconds()+5;
    FString Body;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/case-records/main-quest"),Body);
}
void UProjectCaseRecords::ApplyMainQuestUpdate(const TSharedPtr<FJsonObject>& Update)
{
    if(!IsAuthenticated()||!Update)return;const auto Quest=Obj(Update,TEXT("mainQuest"));const FString User=Str(Update,TEXT("userId"));if(!Quest||User.IsEmpty())return;
    const FString PreviousUser=Str(Snapshot,TEXT("userId"));if(!PreviousUser.IsEmpty()&&PreviousUser!=User)return;
    if(Num(Quest,TEXT("revision"))<Num(Obj(Snapshot,TEXT("mainQuest")),TEXT("revision")))return;
    if(Request&&Request->GetVerb()==TEXT("GET")){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}
    if(!Snapshot)Snapshot=MakeShared<FJsonObject>();Snapshot->SetStringField(TEXT("userId"),User);Snapshot->SetObjectField(TEXT("mainQuest"),Quest);
    ObserveMainQuest(Update);PollIn=0;OnChanged.Broadcast();
}
void UProjectCaseRecords::ObserveMainQuest(const TSharedPtr<FJsonObject>& Response)
{
    const auto Quest=Obj(Snapshot,TEXT("mainQuest"));const FString Account=Str(Snapshot,TEXT("userId"));if(!Quest||Account.IsEmpty())return;
    const int64 Revision=int64(Num(Quest,TEXT("revision")));
    const bool Mutation=Response&&Response->HasTypedField<EJson::Array>(TEXT("questNotices"));
    if(QuestAccount!=Account){QuestAccount=Account;SeenQuestRevision=Mutation?0:Revision;if(!Mutation)return;}
    auto* UI=GetGameInstance()->GetSubsystem<UProjectAnnouncementSubsystem>();if(!UI)return;
    auto Notices=Rows(Mutation?Response:Quest,Mutation?TEXT("questNotices"):TEXT("notices"));Notices.Sort([](const auto& L,const auto& R){return Num(L->AsObject(),TEXT("revision"))<Num(R->AsObject(),TEXT("revision"));});
    for(const auto& Value:Notices)
    {
        const auto Notice=Value->AsObject();const int64 At=int64(Num(Notice,TEXT("revision")));if(At<=SeenQuestRevision)continue;
        UI->NotifyQuest(Str(Notice,TEXT("questName")),Str(Notice,TEXT("kind")),Str(Notice,TEXT("stepTitle")),Account+TEXT(":Main:")+FString::Printf(TEXT("%lld"),At));
        SeenQuestRevision=At;
    }
    SeenQuestRevision=FMath::Max(SeenQuestRevision,Revision);
}
void UProjectProject01GameInstance::ReportMainQuestFloor(const FString& MatchId,const FString& UserId,const FString& Map)
{ReportMainQuestEvent(MatchId,UserId,TEXT("Floor"),1,Map);}
void UProjectProject01GameInstance::ReportMainQuestEvent(const FString& MatchId,const FString& UserId,const FString& Kind,int32 Count,const FString& Map)
{
    if(!FProjectResultOutbox::IsProductionMatchId(MatchId)||UserId.IsEmpty()||!GetWorld()||GetWorld()->GetNetMode()==NM_Client||!IsBackendUrlAllowed())return;
    for(TCHAR Ch:UserId)if(!FChar::IsDigit(Ch))return;
    const bool Floor=Kind==TEXT("Floor");
    if(Floor){if(Map!=TEXT("/Game/MyProject/Level/MultiplayTest")&&Map!=TEXT("/Game/MyProject/Level/MultiplayTest1"))return;}
    else if((Kind!=TEXT("Capture")&&Kind!=TEXT("Escape"))||Count<1)return;
    const FString Secret=LoadGameServerSharedSecret();if(Secret.Len()<32)return;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("matchId"),MatchId);J->SetStringField(TEXT("userId"),UserId);J->SetStringField(TEXT("map"),Map);J->SetNumberField(TEXT("serverPort"),GetWorld()->URL.Port);J->SetStringField(TEXT("observedAtUtc"),FDateTime::UtcNow().ToIso8601());
    if(!Floor){J->SetStringField(TEXT("kind"),Kind);J->SetNumberField(TEXT("count"),FMath::Clamp(Count,1,100));}
    FString Body;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Body));
    const TWeakObjectPtr<UProjectProject01GameInstance> Weak(this);
    FProjectResultOutbox::Submit(SecurityApiBaseUrl,Floor?TEXT("/api/server/main-quest/floor"):TEXT("/api/server/main-quest/event"),Body,Secret,[Weak,UserId](bool Success)
    {
        if(!Success||!Weak.IsValid()||!Weak->GetWorld())return;
        for(auto It=Weak->GetWorld()->GetPlayerControllerIterator();It;++It)if(auto* PC=Cast<AMultiplayTestPlayerController>(It->Get());PC&&PC->GetAuthenticatedUserId()==UserId)PC->ClientRefreshCaseProgress();
    });
}
void AMultiplayTestPlayerController::ClientRefreshCaseProgress_Implementation()
{if(auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>())R->Refresh();}
void UProjectCaseRecordWidget::BuildMainQuest(const TSharedPtr<FJsonObject>& State)
{
    auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();const auto Quest=Obj(State,TEXT("mainQuest"));
    Body->AddSlot().AutoHeight().Padding(4,6)[Text(TEXT("메인 퀘스트 · ")+(Str(Quest,TEXT("name")).IsEmpty()?TEXT("탐사"):Str(Quest,TEXT("name"))),24)];
    const auto Steps=Rows(Quest,TEXT("steps"));if(Steps.IsEmpty())return;
    const int32 Current=int32(Num(Quest,TEXT("stage")));const int32 LastUnlocked=FMath::Clamp(Current,0,Steps.Num()-1);if(SelectedQuestStage<0)SelectedQuestStage=LastUnlocked;SelectedQuestStage=FMath::Clamp(SelectedQuestStage,0,LastUnlocked);
    auto List=SNew(SVerticalBox);
    for(int32 I=0;I<Steps.Num();++I)
    {
        if(I>LastUnlocked)break;
        auto Row=Button(FString::Printf(TEXT("%d. "),I+1)+Str(Steps[I]->AsObject(),TEXT("title")),[this,I]{SelectedQuestStage=I;RenderedFingerprint.Reset();RefreshPanel();});
        Row->SetButtonStyle(&QuestButtonStyle(I==SelectedQuestStage));Row->SetContentPadding(FMargin(8,4));
        StaticCastSharedRef<STextBlock>(Row->GetContent())->SetAutoWrapText(false);StaticCastSharedRef<STextBlock>(Row->GetContent())->SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);StaticCastSharedRef<STextBlock>(Row->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
        List->AddSlot().AutoHeight().Padding(0,2)[SNew(SBox).HeightOverride(32)[Row]];
    }
    const auto Selected=Steps[SelectedQuestStage]->AsObject();auto Details=SNew(SVerticalBox);
    Details->AddSlot().AutoHeight().Padding(0,0,0,12)[Text(Str(Selected,TEXT("title")),21)];
    Details->AddSlot().AutoHeight().Padding(0,0,0,10)[Text(Flag(Selected,TEXT("completed"))?TEXT("단계 완료"):Flag(Selected,TEXT("active"))?TEXT("진행 중"):TEXT("미진행"),16)];
    for(const auto& V:Rows(Selected,TEXT("goals"))){const auto G=V->AsObject();Details->AddSlot().AutoHeight().Padding(0,4)[Text(FString::Printf(TEXT("%s · %.0f/%.0f"),*Str(G,TEXT("label")),Num(G,TEXT("progress")),Num(G,TEXT("target"))),16)];}
    if(Flag(Selected,TEXT("canSkip")))
    {
        auto* Lobby=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();const bool Ready=R->IsAuthenticated()&&Lobby&&!Lobby->HasCurrentRoom()&&!Lobby->IsMatchmaking();
        const bool Mannequin=Str(Selected,TEXT("id"))==TEXT("mannequin_tutorial");
        Details->AddSlot().AutoHeight().Padding(0,10)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[Button(TEXT("튜토리얼 시작"),[this,Mannequin]
        {
            const FName Map(TEXT("/Game/MyProject/Level/MultiplayerTutorial"));if(!FPackageName::DoesPackageExist(Map.ToString()))return;
            auto* GI=GetGameInstance<UProjectProject01GameInstance>();if(!GI||!GetWorld())return;SetIsEnabled(false);GI->FadeEntireScreenToBlack(.6f);
            FTimerHandle Timer;const TWeakObjectPtr<UProjectProject01GameInstance> Weak(GI);GetWorld()->GetTimerManager().SetTimer(Timer,[Weak,Map,Mannequin]{if(auto* Instance=Weak.Get())UGameplayStatics::OpenLevel(Instance,Map,true,Mannequin?TEXT("TutorialRole=Mannequin"):TEXT("TutorialRole=Survivor"));},.65f,false);
        },Ready)]+SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("건너뛰기"),[R]{R->MainQuestAction(TEXT("SkipTutorial"));},Ready)]];
    }
    if(Flag(Quest,TEXT("completed")))Details->AddSlot().AutoHeight().Padding(0,10)[Text(TEXT("탐사 퀘스트 완료"),20)];
    FString Story=Str(Quest,TEXT("baseStory"));for(const auto& V:Steps){const FString Part=Str(V->AsObject(),TEXT("story"));if(!Part.IsEmpty())Story+=(Story.IsEmpty()?TEXT(""):TEXT("\n\n"))+Part;}
    TSharedRef<SWidget> Right=SNew(SScrollBox)+SScrollBox::Slot()[Details];
    if(!Story.IsEmpty())
    {
        auto StoryText=Text(Story,16);StoryText->SetLineHeightPercentage(1.3f);
        Right=SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(SBox).MaxDesiredHeight(150)[SNew(SScrollBox)+SScrollBox::Slot()[Details]]]
            +SVerticalBox::Slot().FillHeight(1).Padding(0,12,0,0)[SNew(SScrollBox)+SScrollBox::Slot()[StoryText]];
    }
    Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(300)[SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(0,0,18,0)[SNew(SBox).WidthOverride(260)[List]]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Canvas()).Padding(18)[Right]]]];
}
