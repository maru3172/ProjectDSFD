#include "ProjectCaseRecords.h"
#include "ProjectProject01GameInstance.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectAnnouncementSubsystem.h"
#include "MultiplayTestPlayerController.h"
#include "ProjectProject01ResultOutbox.h"
#include "ProjectPhoneMessenger.h"
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
    // The tutorial step this is about: on the quest graph, the active tutorial step of that role (or, with no role, the
    // first active tutorial step); before the graph has loaded, or from an older server, the 탐사 step ids.
    FString QuestId,Id;
    for(const auto& Q:Rows(Obj(Snapshot,TEXT("questGraph")),TEXT("quests")))
    {
        for(const auto& V:Rows(Q->AsObject(),TEXT("steps")))
        {
            const auto Step=V->AsObject();const FString StepRole=Str(Step,TEXT("tutorialRole"));
            if(Flag(Step,TEXT("active"))&&!StepRole.IsEmpty()&&(Role.IsEmpty()||StepRole==Role)){QuestId=Str(Q->AsObject(),TEXT("id"));Id=Str(Step,TEXT("id"));break;}
        }
        if(!Id.IsEmpty())break;
    }
    if(Id.IsEmpty())
    {
        const auto Quest=Obj(Snapshot,TEXT("mainQuest"));const auto Steps=Rows(Quest,TEXT("steps"));const int32 Index=int32(Num(Quest,TEXT("stage")));
        if(!Tutorial&&Role.IsEmpty()&&!Steps.IsValidIndex(Index))return;
        // A role names the tutorial outright (finishing it, or skipping it from the first-login prompt before the quest has loaded).
        Id=Tutorial||!Role.IsEmpty()?(Role==TEXT("Survivor")?TEXT("survivor_tutorial"):TEXT("mannequin_tutorial")):Str(Steps[Index]->AsObject(),TEXT("id"));
        if(Id!=TEXT("survivor_tutorial")&&Id!=TEXT("mannequin_tutorial"))return;
    }
    if(!PrepareWrite())return;
    ObserveMainQuest(); // Establish the pre-write baseline before a completion can be returned.
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),Action);J->SetStringField(TEXT("expectedStep"),Id);J->SetStringField(TEXT("role"),Role);
    if(!QuestId.IsEmpty())J->SetStringField(TEXT("quest"),QuestId);
    TutorialRetryAt=FPlatformTime::Seconds()+5;bTalkInFlight=false;
    FString Body;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/case-records/main-quest"),Body);
}
void UProjectCaseRecords::MainQuestTalk(const FString& StepId,const TArray<int32>& Choices,const FString& QuestId)
{
    if(!IsAuthenticated()||StepId.IsEmpty())return;
    PendingTalkStep=StepId;PendingTalkQuest=QuestId;PendingTalkChoices=Choices;PendingTalkGeneration=GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>()->GetSessionGeneration();
    if(!PrepareWrite()){TalkRetryAt=0;return;} // Another write is running; Tick sends this when it ends.
    ObserveMainQuest(); // Baseline before the stage notice can come back.
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("action"),TEXT("TalkComplete"));J->SetStringField(TEXT("expectedStep"),StepId);
    if(!QuestId.IsEmpty())J->SetStringField(TEXT("quest"),QuestId); // Older builds leave it out: the main line.
    TArray<TSharedPtr<FJsonValue>> Picked;for(const int32 Choice:Choices)Picked.Add(MakeShared<FJsonValueNumber>(Choice));J->SetArrayField(TEXT("choices"),Picked);
    TalkRetryAt=FPlatformTime::Seconds()+5;bTalkInFlight=true;
    FString Body;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Body));Send(TEXT("/api/case-records/main-quest"),Body);
}
bool UProjectCaseRecords::HasUnreadTalk()const
{
    // Every open quest's conversations; one being reported already counts as read.
    for(const auto& Talk:FProjectTalkScript::FromSnapshot(Snapshot))if(!Talk.bTalked&&!(Talk.StepId==PendingTalkStep&&Talk.QuestId==PendingTalkQuest))return true;
    return false;
}
void UProjectCaseRecords::ApplyMainQuestUpdate(const TSharedPtr<FJsonObject>& Update)
{
    if(!IsAuthenticated()||!Update)return;const auto Quest=Obj(Update,TEXT("mainQuest"));const FString User=Str(Update,TEXT("userId"));if(!Quest||User.IsEmpty())return;
    const FString PreviousUser=Str(Snapshot,TEXT("userId"));if(!PreviousUser.IsEmpty()&&PreviousUser!=User)return;
    if(Num(Quest,TEXT("revision"))<Num(Obj(Snapshot,TEXT("mainQuest")),TEXT("revision")))return;
    if(Request&&Request->GetVerb()==TEXT("GET")){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}
    if(!Snapshot)Snapshot=MakeShared<FJsonObject>();Snapshot->SetStringField(TEXT("userId"),User);Snapshot->SetObjectField(TEXT("mainQuest"),Quest);
    if(const auto Graph=Obj(Update,TEXT("questGraph")))Snapshot->SetObjectField(TEXT("questGraph"),Graph);
    ObserveMainQuest(Update);PollIn=0;OnChanged.Broadcast();
}
void UProjectCaseRecords::ObserveMainQuest(const TSharedPtr<FJsonObject>& Response)
{
    const auto Quest=Obj(Snapshot,TEXT("mainQuest"));const FString Account=Str(Snapshot,TEXT("userId"));if(!Quest||Account.IsEmpty())return;
    const int64 Revision=int64(Num(Quest,TEXT("revision")));
    ObserveChainUnlocks(); // Before the early returns below, so the first look at an account records what is already open.
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
    auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();
    // Quest kinds: 메인 퀘스트 (several story lines such as 탐사, chosen in the list), 연계 퀘스트 (one line per NPC), 서브 퀘스트.
    // On the quest graph (Docs/QuestGraph.md) each tab lists the open quests of its kind: main lines, NPC lines (연계),
    // side quests. An older server sends only 탐사, shown as the one main line.
    const auto Graph=Obj(State,TEXT("questGraph"));const bool bGraph=Graph.IsValid();
    auto OfKind=[&Graph](const TCHAR* Kind){TArray<TSharedPtr<FJsonObject>> Out;for(const auto& V:Rows(Graph,TEXT("quests")))if(const auto Q=V.IsValid()?V->AsObject():nullptr;Q&&Str(Q,TEXT("kind"))==Kind)Out.Add(Q);return Out;};
    TArray<TSharedPtr<FJsonObject>> Chapters;
    if(bGraph)Chapters=OfKind(QuestCategory==TEXT("Chain")?TEXT("Chain"):QuestCategory==TEXT("Sub")?TEXT("Side"):TEXT("Main"));
    else if(QuestCategory==TEXT("Main"))if(const auto Exploration=Obj(State,TEXT("mainQuest")))Chapters.Add(Exploration);
    const TArray<TSharedPtr<FJsonObject>> MainLines=bGraph?OfKind(TEXT("Main")):Chapters;
    auto Kinds=SNew(SHorizontalBox);
    for(const auto& Kind:TArray<TPair<FString,FString>>{{TEXT("Main"),TEXT("메인 퀘스트")},{TEXT("Chain"),TEXT("연계 퀘스트")},{TEXT("Sub"),TEXT("서브 퀘스트")}})
    {
        const FString Id=Kind.Key;auto Tab=Button(Kind.Value,[this,Id]{if(QuestCategory!=Id){QuestCategory=Id;SelectedQuestChapter=0;SelectedQuestStage=-1;}RenderedFingerprint.Reset();RefreshPanel();});
        Tab->SetButtonStyle(&QuestButtonStyle(QuestCategory==Id));Tab->SetContentPadding(FMargin(12,3));StaticCastSharedRef<STextBlock>(Tab->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15));StaticCastSharedRef<STextBlock>(Tab->GetContent())->SetAutoWrapText(false);
        Kinds->AddSlot().AutoWidth().Padding(0,0,6,0)[Tab];
    }
    Body->AddSlot().AutoHeight().Padding(4,4,4,8)[Kinds]; // The selected tab names the page, so there is no separate title.
    auto Columns=[this](TSharedRef<SWidget> Left,TSharedRef<SWidget> Right)
    {
        Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(300)[SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(0,0,18,0)[SNew(SBox).WidthOverride(260)[Left]]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Canvas()).Padding(18)[Right]]]];
    };
    if(QuestCategory==TEXT("Chain")&&Chapters.IsEmpty())
    {
        // One line per NPC; each opens once that NPC's main quest talk is finished. Their quests are added later.
        static const TArray<FString> Npcs{TEXT("캘린"),TEXT("아테아")};SelectedChainQuest=FMath::Clamp(SelectedChainQuest,0,Npcs.Num()-1);
        auto IsOpen=[&MainLines](const FString& Npc){for(const auto& C:MainLines)for(const auto& V:Rows(C,TEXT("steps")))if(Str(V->AsObject(),TEXT("npc"))==Npc&&Flag(V->AsObject(),TEXT("completed")))return true;return false;};
        auto List=SNew(SVerticalBox);
        for(int32 I=0;I<Npcs.Num();++I)
        {
            auto Row=Button(Npcs[I]+(IsOpen(Npcs[I])?TEXT(""):TEXT(" (잠김)")),[this,I]{SelectedChainQuest=I;RenderedFingerprint.Reset();RefreshPanel();});Row->SetButtonStyle(&QuestButtonStyle(I==SelectedChainQuest));Row->SetContentPadding(FMargin(8,4));
            List->AddSlot().AutoHeight().Padding(0,2)[SNew(SBox).HeightOverride(32)[Row]];
        }
        const FString Npc=Npcs[SelectedChainQuest];const bool Open=IsOpen(Npc);
        Columns(List,SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[Text(TEXT("연계 퀘스트 · ")+Npc,21)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Text(Open?TEXT("열람 가능"):TEXT("잠김"),16)]
            +SVerticalBox::Slot().AutoHeight()[Text(Open?TEXT("아직 진행할 수 있는 연계 퀘스트가 없습니다."):FString::Printf(TEXT("메인 퀘스트에서 %s와(과) 대화하면 열립니다."),*Npc),16)]);
        return;
    }
    if(QuestCategory==TEXT("Sub")&&Chapters.IsEmpty()){Body->AddSlot().AutoHeight().Padding(4,10)[Text(TEXT("아직 받은 서브 퀘스트가 없습니다."),16)];return;}

    if(Chapters.IsEmpty())return;
    SelectedQuestChapter=FMath::Clamp(SelectedQuestChapter,0,Chapters.Num()-1);const auto Quest=Chapters[SelectedQuestChapter];
    const auto Steps=Rows(Quest,TEXT("steps"));if(Steps.IsEmpty())return;
    // A graph quest lists only the steps actually reached (in the order played), the active one last.
    int32 Current=int32(Num(Quest,TEXT("stage")));
    if(bGraph){Current=Steps.Num()-1;for(int32 I=0;I<Steps.Num();++I)if(Flag(Steps[I]->AsObject(),TEXT("active")))Current=I;}
    const int32 LastUnlocked=FMath::Clamp(Current,0,Steps.Num()-1);if(SelectedQuestStage<0)SelectedQuestStage=LastUnlocked;SelectedQuestStage=FMath::Clamp(SelectedQuestStage,0,LastUnlocked);
    auto List=SNew(SVerticalBox);
    // Every main quest line is a heading; the chosen one opens to show its steps.
    for(int32 C=0;C<Chapters.Num();++C)
    {
        const bool Open=C==SelectedQuestChapter;FString Name=Str(Chapters[C],TEXT("name")).IsEmpty()?TEXT("탐사"):Str(Chapters[C],TEXT("name"));
        if(const FString Npc=Str(Chapters[C],TEXT("npc"));!Npc.IsEmpty())Name=Npc+TEXT(" · ")+Name; // 연계: whose line it is.
        if(Flag(Chapters[C],TEXT("completed")))Name+=TEXT(" (완료)");
        auto Heading=Button(Name,[this,C]{if(SelectedQuestChapter!=C){SelectedQuestChapter=C;SelectedQuestStage=-1;}RenderedFingerprint.Reset();RefreshPanel();});
        Heading->SetButtonStyle(&QuestButtonStyle(Open));Heading->SetContentPadding(FMargin(8,3));StaticCastSharedRef<STextBlock>(Heading->GetContent())->SetAutoWrapText(false);
        List->AddSlot().AutoHeight().Padding(0,0,0,4)[Heading];
        if(!Open)continue;
        for(int32 I=0;I<Steps.Num();++I)
        {
            if(I>LastUnlocked)break;
            auto Row=Button(FString::Printf(TEXT("%d. "),I+1)+Str(Steps[I]->AsObject(),TEXT("title")),[this,I]{SelectedQuestStage=I;RenderedFingerprint.Reset();RefreshPanel();});
            Row->SetButtonStyle(&QuestButtonStyle(I==SelectedQuestStage));Row->SetContentPadding(FMargin(8,4));
            StaticCastSharedRef<STextBlock>(Row->GetContent())->SetAutoWrapText(false);StaticCastSharedRef<STextBlock>(Row->GetContent())->SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);StaticCastSharedRef<STextBlock>(Row->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
            List->AddSlot().AutoHeight().Padding(14,2,0,2)[SNew(SBox).HeightOverride(32)[Row]];
        }
    }
    const auto Selected=Steps[SelectedQuestStage]->AsObject();auto Details=SNew(SVerticalBox);
    Details->AddSlot().AutoHeight().Padding(0,0,0,12)[Text(Str(Selected,TEXT("title")),21)];
    Details->AddSlot().AutoHeight().Padding(0,0,0,10)[Text(Flag(Selected,TEXT("completed"))?TEXT("단계 완료"):Flag(Selected,TEXT("active"))?TEXT("진행 중"):TEXT("미진행"),16)];
    for(const auto& V:Rows(Selected,TEXT("goals"))){const auto G=V->AsObject();Details->AddSlot().AutoHeight().Padding(0,4)[Text(FString::Printf(TEXT("%s · %.0f/%.0f"),*Str(G,TEXT("label")),Num(G,TEXT("progress")),Num(G,TEXT("target"))),16)];}
    if(Obj(Selected,TEXT("talk")))
    {
        // Talk steps open the phone on that conversation (also reachable from the phone button at the bottom of the lobby).
        const FString StepId=(bGraph?Str(Quest,TEXT("id"))+TEXT("/"):FString())+Str(Selected,TEXT("id"));const bool Read=Flag(Selected,TEXT("talked"));
        Details->AddSlot().AutoHeight().Padding(0,10)[Button(Read?TEXT("대화 다시 보기"):TEXT("휴대폰으로 대화하기"),[this,StepId]{UProjectPhoneWidget::Open(GetOwningPlayer(),StepId);})];
    }
    if(Flag(Selected,TEXT("canSkip")))
    {
        auto* Lobby=GetGameInstance()->GetSubsystem<UProjectProject01LobbySubsystem>();const bool Ready=R->IsAuthenticated()&&Lobby&&!Lobby->HasCurrentRoom()&&!Lobby->IsMatchmaking();
        const bool Mannequin=Str(Selected,TEXT("tutorialRole"))==TEXT("Mannequin")||Str(Selected,TEXT("id"))==TEXT("mannequin_tutorial");
        Details->AddSlot().AutoHeight().Padding(0,10)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[Button(TEXT("튜토리얼 시작"),[this,Mannequin]
        {
            const FName Map(TEXT("/Game/MyProject/Level/MultiplayerTutorial"));if(!FPackageName::DoesPackageExist(Map.ToString()))return;
            auto* GI=GetGameInstance<UProjectProject01GameInstance>();if(!GI||!GetWorld())return;SetIsEnabled(false);GI->FadeEntireScreenToBlack(.6f);
            FTimerHandle Timer;const TWeakObjectPtr<UProjectProject01GameInstance> Weak(GI);GetWorld()->GetTimerManager().SetTimer(Timer,[Weak,Map,Mannequin]{if(auto* Instance=Weak.Get())UGameplayStatics::OpenLevel(Instance,Map,true,Mannequin?TEXT("TutorialRole=Mannequin"):TEXT("TutorialRole=Survivor"));},.65f,false);
        },Ready)]+SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("건너뛰기"),[R,Mannequin]{R->MainQuestAction(TEXT("SkipTutorial"),Mannequin?TEXT("Mannequin"):TEXT("Survivor"));},Ready)]];
    }
    if(Flag(Quest,TEXT("completed")))Details->AddSlot().AutoHeight().Padding(0,10)[Text((Str(Quest,TEXT("name")).IsEmpty()?FString(TEXT("탐사")):Str(Quest,TEXT("name")))+TEXT(" 퀘스트 완료"),20)];
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
void UProjectCaseRecords::ObserveChainUnlocks()
{
    const auto Quest=Obj(Snapshot,TEXT("mainQuest"));const FString Account=Str(Snapshot,TEXT("userId"));if(!Quest||Account.IsEmpty())return;
    const bool First=ChainAccount!=Account;if(First){ChainAccount=Account;OpenChains.Reset();}
    auto* UI=GetGameInstance()->GetSubsystem<UProjectAnnouncementSubsystem>();
    for(const auto& V:Rows(Quest,TEXT("steps")))
    {
        const auto Step=V.IsValid()?V->AsObject():nullptr;const FString Npc=Str(Step,TEXT("npc"));
        if(Npc.IsEmpty()||!Flag(Step,TEXT("completed"))||OpenChains.Contains(Npc))continue;
        OpenChains.Add(Npc);
        if(!First&&UI)UI->NotifyQuest(TEXT("연계 퀘스트 · ")+Npc,TEXT("Unlock"),Npc+TEXT("의 연계 퀘스트를 열람할 수 있습니다."),Account+TEXT(":Chain:")+Npc);
    }
}
