#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectPhoneMessenger.generated.h"

class FJsonObject;

/**
 * One phone conversation of a quest step (server "talk", Docs/QuestGraph.md): NPC messages and player choices with the
 * NPC's replies. It is a graph: every message and every option names the line that comes next (INDEX_NONE ends it),
 * so a choice can lead to a different part of the conversation. Older one-way talks simply link each line to the next.
 */
struct PROJECTPROJECT01_API FProjectTalkScript
{
    struct FOption{FString Text;TArray<FString> Replies;int32 Next=INDEX_NONE;};
    struct FLine{FString Text;TArray<FOption> Options;int32 Next=INDEX_NONE;bool IsChoice()const{return Options.Num()>0;}};
    FString QuestId,StepId,Npc;
    TArray<FLine> Lines;
    int32 Start=0;
    /** Identifies the conversation across quests (step ids are only unique within a quest). */
    FString Key()const{return QuestId+TEXT("/")+StepId;}
    bool bTalked=false;
    TArray<int32> SavedChoices;
    bool bActive=false;
    static bool Parse(const TSharedPtr<FJsonObject>& Step,FProjectTalkScript& Out);
    /** Conversations the main quest has unlocked for this account, in quest order. */
    static TArray<FProjectTalkScript> FromSnapshot(const TSharedPtr<FJsonObject>& Snapshot);
};

/**
 * Plays a conversation like a messenger: NPC messages arrive one by one after a short typing pause, the player
 * answers by picking an option, and the NPC's replies to that option follow. Pure logic (no Slate), so it is tested
 * directly; the phone widget only draws what this reports.
 */
struct PROJECTPROJECT01_API FProjectTalkPlayback
{
    struct FBubble{FString Text;bool bPlayer=false;};
    void Start(const FProjectTalkScript& InScript);
    /** Rebuilds the finished conversation instantly from saved choices (reading an earlier talk). */
    void Replay(const FProjectTalkScript& InScript,const TArray<int32>& Choices);
    /** Advances time; returns true when a new bubble appeared. */
    bool Tick(double Seconds);
    /** Shows the pending NPC message now (click to hurry). */
    void Hurry(){Wait=0;}
    bool Choose(int32 Option);
    bool IsTyping()const{return !bDone&&Choices()==nullptr&&HasPendingMessage();}
    const TArray<FProjectTalkScript::FOption>* Choices()const;
    bool IsFinished()const{return bDone;}
    const TArray<FBubble>& Bubbles()const{return Shown;}
    const TArray<int32>& Chosen()const{return Picked;}
    static double TypingSeconds(const FString& Text){return FMath::Clamp(.45+Text.Len()*.025,.6,1.8);}
private:
    bool HasPendingMessage()const;
    bool NextMessage(FString& Out)const;
    void Schedule();
    FProjectTalkScript Script;
    TArray<FBubble> Shown;
    TArray<FString> Replies;
    TArray<int32> Picked;
    int32 Cursor=0;
    int32 Steps=0;
    double Wait=0;
    bool bDone=false;
};

/** A phone held over the current screen: contact list of NPCs the main quest unlocked, and the chat with each. */
UCLASS()
class PROJECTPROJECT01_API UProjectPhoneWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    static constexpr int32 ViewportZOrder=1300; // Above the records window (1180) and lobby menus.
    /** Opens the phone; with a step id it goes straight to that conversation. */
    static UProjectPhoneWidget* Open(APlayerController* Player,const FString& StepId=FString());
    void Close();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
    FString InitialStep;
    TSharedPtr<class SProjectPhone> Phone;
    TWeakObjectPtr<APlayerController> OwnerController;
};
