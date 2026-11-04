// File: Source/ProjectProject01/Private/ProjectItemRoguelikeTests.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ProjectItemRoguelike.h"
#include "ProjectItemRoguelikeRuntime.h"
#include "ProjectRoguePick.h"
#include "ProjectAffixRuntime.h"
#include "MultiplayTestGameMode.h"
#include "PlayerCharacter.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

namespace
{
    FMultiplayTestServerPlayerRecord MakeRecord(const TCHAR* Id,const TCHAR* Role,bool bBot=false,EMultiplayTestSurvivorState State=EMultiplayTestSurvivorState::Active)
    {
        FMultiplayTestServerPlayerRecord R;R.UserId=Id;R.DisplayName=Id;R.Role=Role;R.bServerBot=bBot;
        R.SurvivorState=FString(Role)==TEXT("Survivor")?State:EMultiplayTestSurvivorState::NotApplicable;return R;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectItemRoguelikeTest,"ProjectProject01.ItemRoguelike.Rules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectItemRoguelikeTest::RunTest(const FString&)
{
    using namespace ProjectItemRoguelike;
    // The card table: 15 items per side, unique ids that never collide with an affix id, every line written.
    int32 Survivor=0,Mannequin=0;TSet<FString> Ids;
    for(const auto& I:Items())
    {
        Survivor+=I.Kind==EKind::SurvivorItem;Mannequin+=I.Kind==EKind::MannequinItem;
        TestFalse(TEXT("Item ids are unique"),Ids.Contains(I.Id));Ids.Add(I.Id);
        TestNull(TEXT("Item ids never shadow an affix id"),ProjectAffixes::Find(I.Id));
        TestTrue(TEXT("Every item has a name, effect, tell and weak spot"),*I.Name&&*I.Effect&&*I.Tell&&*I.Weak&&*I.Glyph);
    }
    TestEqual(TEXT("15 survivor items"),Survivor,15);TestEqual(TEXT("15 mannequin items"),Mannequin,15);
    TestTrue(TEXT("Survivors hand B buffs to the mannequin"),IsBuffFor(TEXT("B04"),true)&&!IsBuffFor(TEXT("A04"),true));
    TestTrue(TEXT("The mannequin hands A buffs to the survivors"),IsBuffFor(TEXT("A04"),false)&&!IsBuffFor(TEXT("B04"),false));
    TestFalse(TEXT("Environment affixes are never cards"),IsBuffFor(TEXT("C05"),true)||IsBuffFor(TEXT("C05"),false));
    TestFalse(TEXT("A side cannot take the other side's items"),IsCardFor(TEXT("M01"),true)||IsCardFor(TEXT("S01"),false));

    // Rolling: three different cards for the right side, at least one item and one buff, nothing owned or in play.
    FRandomStream Rng(7);
    for(int32 Round=0;Round<200;++Round)
    {
        const bool bSurvivor=Round%2==0;const TArray<FString> Owned={TEXT("S01"),TEXT("M01"),TEXT("B01")},Active={TEXT("B02"),TEXT("A02")};
        const TArray<FString> Offer=Roll(bSurvivor,Owned,Active,Rng);
        TSet<FString> Seen;int32 ItemCount=0,BuffCount=0;
        for(const FString& Id:Offer)
        {
            TestTrue(TEXT("Card fits the picker's side"),IsCardFor(Id,bSurvivor));TestFalse(TEXT("No owned or active card"),Owned.Contains(Id)||Active.Contains(Id));
            TestFalse(TEXT("No duplicate in one offer"),Seen.Contains(Id));Seen.Add(Id);
            (FindItem(Id)?ItemCount:BuffCount)++;
        }
        if(!TestEqual(TEXT("Three cards"),Offer.Num(),OfferSize))break;
        TestTrue(TEXT("At least one item and one buff"),ItemCount>=1&&BuffCount>=1);
        const TArray<FString> Rerolled=Roll(bSurvivor,Owned,Active,Rng,Offer);
        for(const FString& Id:Rerolled)TestFalse(TEXT("A reroll shows new cards while enough remain"),Offer.Contains(Id));
    }
    TArray<FString> AllButOne;for(const auto& I:Items())if(I.Kind==EKind::SurvivorItem)AllButOne.Add(I.Id);
    for(const auto& A:ProjectAffixes::All())if(IsBuffFor(A.Id,true))AllButOne.Add(A.Id);
    const FString Last=AllButOne.Pop();
    const TArray<FString> Tail=Roll(true,AllButOne,{},Rng);
    TestTrue(TEXT("A nearly empty pool offers what is left"),Tail.Num()==1&&Tail[0]==Last);

    // The server runtime: offers, authority, reroll, time out, carryover.
    auto* Rogue=NewObject<UProjectItemRoguelikeRuntime>();FString Error;const FString Match=FGuid::NewGuid().ToString();
    TestFalse(TEXT("Normal mode is not active"),Rogue->IsActive());
    TestTrue(TEXT("The ticket's mode is accepted"),Rogue->Accept(Match,EProjectMatchMode::ItemRoguelike,Error));
    TestTrue(TEXT("Same match, same mode: idempotent"),Rogue->Accept(Match,EProjectMatchMode::ItemRoguelike,Error));
    TestFalse(TEXT("A later ticket cannot change the mode"),Rogue->Accept(Match,EProjectMatchMode::Normal,Error));
    Rogue->SeedForTest(11);
    const TArray<FMultiplayTestServerPlayerRecord> Records={MakeRecord(TEXT("s1"),TEXT("Survivor")),MakeRecord(TEXT("s2"),TEXT("Survivor")),
        MakeRecord(TEXT("m"),TEXT("Mannequin")),MakeRecord(TEXT("dead"),TEXT("Survivor"),false,EMultiplayTestSurvivorState::Eliminated)};
    Rogue->OpenForTest(Records,100.0);
    TestTrue(TEXT("The pick is open"),Rogue->IsPicking());
    const FProjectRoguePlayer* S1=Rogue->FindPlayer(TEXT("s1"));const FProjectRoguePlayer* S2=Rogue->FindPlayer(TEXT("s2"));const FProjectRoguePlayer* M=Rogue->FindPlayer(TEXT("m"));
    if(!TestTrue(TEXT("Everyone in play gets cards"),S1&&S2&&M&&S1->Offer.Num()==3&&S2->Offer.Num()==3&&M->Offer.Num()==3))return false;
    TestNull(TEXT("A caught survivor gets no cards"),Rogue->FindPlayer(TEXT("dead")));
    for(const FString& Id:S1->Offer)if(ProjectAffixes::Find(Id))TestFalse(TEXT("Teammates never see the same buff"),S2->Offer.Contains(Id));
    const int32 Serial=S1->Serial;
    TestFalse(TEXT("A stale serial is refused"),Rogue->PickForTest(TEXT("s1"),Serial+1,0));
    TestFalse(TEXT("An index outside the offer is refused"),Rogue->PickForTest(TEXT("s1"),Serial,3));
    TestFalse(TEXT("A stranger cannot pick"),Rogue->PickForTest(TEXT("nobody"),Serial,0));
    const TArray<FString> Before=S1->Offer;
    TestTrue(TEXT("One reroll"),Rogue->RerollForTest(TEXT("s1"),Serial));
    TestFalse(TEXT("No second reroll"),Rogue->RerollForTest(TEXT("s1"),S1->Serial));
    for(const FString& Id:S1->Offer)TestFalse(TEXT("The reroll brings new cards"),Before.Contains(Id));
    TestFalse(TEXT("The old serial dies with the reroll"),Rogue->PickForTest(TEXT("s1"),Serial,0));
    const FString Chosen=S1->Offer[1];
    TestTrue(TEXT("A valid pick"),Rogue->PickForTest(TEXT("s1"),S1->Serial,1));
    TestFalse(TEXT("One pick per floor"),Rogue->PickForTest(TEXT("s1"),S1->Serial,0));
    TestTrue(TEXT("The pick is recorded"),S1->Picks.Num()==1&&S1->Picks[0]==Chosen&&!S1->Auto[0]);
    TestTrue(TEXT("Others still choosing keep the gate shut"),Rogue->TickForTest(110.0));
    TestFalse(TEXT("Time out: the server picks for the rest and opens the gate"),Rogue->TickForTest(100.0+PickSeconds+.1));
    TestTrue(TEXT("Auto picks are marked"),S2->Picks.Num()==1&&S2->Auto[0]&&M->Picks.Num()==1&&M->Auto[0]);
    TestEqual(TEXT("One round done"),Rogue->GetRounds(),1);
    for(const FProjectRoguePlayer* P:{S1,S2,M})
        if(ProjectAffixes::Find(P->Picks[0]))TestTrue(TEXT("A buff pick is in play"),Rogue->HasBuff(*P->Picks[0]));
        else TestTrue(TEXT("An item pick belongs to its owner"),Rogue->HasItem(P->UserId,*P->Picks[0])&&!Rogue->HasItem(TEXT("nobody"),*P->Picks[0]));

    // The next floor keeps the picks; its own round adds one more and never repeats a card.
    const FProjectRogueCarry Carry=Rogue->Export();
    auto* Next=NewObject<UProjectItemRoguelikeRuntime>();Next->Import(Carry);Next->SeedForTest(3);
    TestTrue(TEXT("Carryover keeps mode and rounds"),Next->IsActive()&&Next->GetRounds()==1);
    Next->OpenForTest(Records,200.0);
    const FProjectRoguePlayer* N1=Next->FindPlayer(TEXT("s1"));
    if(TestTrue(TEXT("Next floor offers again"),N1&&N1->Offer.Num()==3))
    {
        TestFalse(TEXT("Owned card not offered again"),N1->Offer.Contains(Chosen));
        TestTrue(TEXT("Next floor pick"),Next->PickForTest(TEXT("s1"),N1->Serial,0));
        const FProjectRoguePickRecord Record=Next->RecordFor(TEXT("s1"));
        TestTrue(TEXT("Result lists both picks in order"),Record.Ids.Num()==2&&Record.Ids[0]==Chosen);
        TestFalse(TEXT("The summary names the cards"),ProjectRoguePickSummary(Record,false).IsEmpty());
    }

    // The game mode: the mode is fixed per match, never with bot affixes, and normal matches see no roguelike rules.
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};World->SetGameInstance(NewObject<UGameInstance>());
    FURL URL;URL.AddOption(TEXT("game=/Script/ProjectProject01.MultiplayTestGameMode"));if(!TestTrue(TEXT("Game mode fixture"),World->SetGameMode(URL)))return false;
    auto* Mode=World->GetAuthGameMode<AMultiplayTestGameMode>();
    TestTrue(TEXT("Normal ticket"),Mode->AcceptMatchMode(Match,EProjectMatchMode::Normal,Error));
    TestFalse(TEXT("Normal match: no roguelike rules"),Mode->IsItemRoguelikeMatch()||Mode->AreAffixRulesActive()||Mode->HasAffixRule(TEXT("B01")));
    TestFalse(TEXT("A later ticket cannot switch the match to roguelike"),Mode->AcceptMatchMode(Match,EProjectMatchMode::ItemRoguelike,Error));
    TestTrue(TEXT("Normal match: items change nothing"),Mode->GetAffixRuntime()->ManualAttackRangeScale()==1.f&&!Mode->GetAffixRuntime()->MannequinSeesPings());
    auto* Bot=World->SpawnActor<AMultiplayTestGameMode>();FProjectMatchAffixes Affixes;Affixes.Mode=EProjectAffixMode::MasterPlus;Affixes.Ids={TEXT("C05")};
    const FString BotMatch=FGuid::NewGuid().ToString();
    TestTrue(TEXT("Bot affixes accepted"),Bot->AcceptMatchAffixes(BotMatch,Affixes,Error));
    TestFalse(TEXT("Item roguelike never runs with bot affixes"),Bot->AcceptMatchMode(BotMatch,EProjectMatchMode::ItemRoguelike,Error));
    auto* Rogueish=World->SpawnActor<AMultiplayTestGameMode>();const FString RogueMatch=FGuid::NewGuid().ToString();
    TestTrue(TEXT("Roguelike ticket"),Rogueish->AcceptMatchMode(RogueMatch,EProjectMatchMode::ItemRoguelike,Error));
    TestTrue(TEXT("Roguelike match runs the affix runtime"),Rogueish->IsItemRoguelikeMatch()&&Rogueish->AreAffixRulesActive());
    Rogueish->GetItemRoguelike()->SeedForTest(5);
    Rogueish->GetItemRoguelike()->OpenForTest({MakeRecord(TEXT("only"),TEXT("Survivor"))},0.0);
    const FProjectRoguePlayer* Only=Rogueish->GetItemRoguelike()->FindPlayer(TEXT("only"));
    if(TestTrue(TEXT("Solo offer"),Only&&Only->Offer.Num()==3))
    {
        int32 Buff=Only->Offer.IndexOfByPredicate([](const FString& Id){return ProjectAffixes::Find(Id)!=nullptr;});
        const FString BuffId=Buff!=INDEX_NONE?Only->Offer[Buff]:FString();
        TestTrue(TEXT("Solo pick"),Rogueish->GetItemRoguelike()->PickForTest(TEXT("only"),Only->Serial,Buff!=INDEX_NONE?Buff:0));
        if(!BuffId.IsEmpty())TestTrue(TEXT("A buff picked in the roguelike counts as an affix rule"),Rogueish->HasAffixRule(*BuffId)&&!Rogueish->GetMatchAffixes().Has(*BuffId));
    }
    FMultiplayTestServerPlayerRecord Escaped=MakeRecord(TEXT("only"),TEXT("Survivor"),false,EMultiplayTestSurvivorState::Escaped);
    const FMultiplayTestMatchResult Result=Rogueish->BuildFinalResult(nullptr,Escaped);
    TestTrue(TEXT("The result carries the picks and is unofficial"),Result.bCustomMatch&&Result.RoguePicks.Num()==1&&Result.RoguePicks[0].Ids.Num()==1);
    const FMultiplayTestMatchResult Plain=Mode->BuildFinalResult(nullptr,Escaped);
    TestTrue(TEXT("A normal result has no picks and stays official"),Plain.RoguePicks.IsEmpty()&&!Plain.bCustomMatch);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectItemRoguelikeRenderTest,"ProjectProject01.ItemRoguelike.PickRender",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectItemRoguelikeRenderTest::RunTest(const FString&)
{
    FProjectRogueOffer Offer;Offer.Serial=1;Offer.Floor=1;Offer.Cards={TEXT("S03"),TEXT("B07"),TEXT("S13")};Offer.Owned={TEXT("S01")};Offer.SecondsLeft=14;Offer.Waiting=2;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("ItemRoguelikeQA");IFileManager::Get().MakeDirectory(*Directory,true);FWidgetRenderer Renderer(false);
    for(FIntPoint Size:{FIntPoint(1280,720),FIntPoint(800,600)})
    {
        TSharedRef<SWidget> Slate=UProjectRoguePickComponent::MakePanelForTest(Offer,TEXT("Survivor"));
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false);const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
        for(int I=0;I<6;++I){Renderer.DrawWidget(Target,Slate,Dpi,FVector2D(Size),.016f);FlushRenderingCommands();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(true);
        if(TestTrue(TEXT("Pick panel renders"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))
        {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/FString::Printf(TEXT("Pick_%dx%d.png"),Size.X,Size.Y)));}
        // The panel is the blackout: every pixel is dark except the drawn UI (no scene shows through).
        int32 Dark=0;for(const FColor& C:Pixels)Dark+=C.R<40&&C.G<40&&C.B<40;
        TestTrue(TEXT("The screen stays black around the cards"),Pixels.Num()>0&&Dark>Pixels.Num()*6/10);
        TArray<FSlateRect> Buttons;int32 Names=0;
        TFunction<void(TSharedRef<SWidget>)> Inspect=[&](TSharedRef<SWidget> Node)
        {
            const auto G=Node->GetCachedGeometry();const FVector2D P=G.GetAbsolutePosition(),S=G.GetAbsoluteSize();
            if(Node->GetType()==FName(TEXT("SButton"))&&S.X>150)Buttons.Add(FSlateRect(P,P+S));
            if(Node->GetType()==FName(TEXT("STextBlock")))
            {
                const FString Text=StaticCastSharedRef<STextBlock>(Node)->GetText().ToString();
                if(!Text.IsEmpty())TestTrue(TEXT("Text fits the viewport: ")+Text,P.X>=0&&P.Y>=0&&P.X+S.X<=Size.X+1&&P.Y+S.Y<=Size.Y+1);
                Names+=Text==TEXT("방화복 조각")||Text==TEXT("드러나는 시선")||Text==TEXT("방독면");
            }
            if(auto* C=Node->GetChildren())for(int32 I=0;I<C->Num();++I)Inspect(C->GetChildAt(I));
        };
        Inspect(Slate);
        TestEqual(TEXT("All three cards are named"),Names,3);
        TestTrue(TEXT("Three cards are drawn"),Buttons.Num()>=3);
        for(int32 A=0;A<Buttons.Num();++A)for(int32 B=A+1;B<Buttons.Num();++B)
            TestFalse(TEXT("Cards and buttons never overlap"),FSlateRect::DoRectanglesIntersect(Buttons[A].InsetBy(FMargin(1)),Buttons[B].InsetBy(FMargin(1))));
        Target->ReleaseResource();
    }
    return true;
}
#endif
