#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectConfirmDialog.generated.h"

/** Lobby/room modal: a yes/no question before an action, or a notice with one confirm button. */
UCLASS()
class PROJECTPROJECT01_API UProjectConfirmDialogWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    /** "예" runs OnYes; "아니오" only closes. With a SkipKey the question offers "오늘 하루 다시 묻지 않기": once
     *  answered "예" with it ticked, that question is not asked again today and OnYes runs at once. Deleting,
     *  quitting the game and forfeiting never get a SkipKey. */
    void Ask(const FString& Question,TFunction<void()> OnYes,const FString& SkipKey=FString());
    /** One "확인" button that closes the notice. */
    void Notify(const FString& Message);
    bool IsOpen()const{return GetVisibility()==ESlateVisibility::Visible;}
    /** Same as "아니오" / "확인" (ESC). */
    void Dismiss(){Close(false);}
    /** Keyboard focus goes back here when the dialog closes (so ESC keeps working on that screen). */
    void SetReturnFocus(UUserWidget* Widget){ReturnFocusTo=Widget;}
    /** Today's opt-out for one question (local, kept in the user settings file). */
    static bool IsSkippedToday(const FString& SkipKey);
    static void SkipToday(const FString& SkipKey);
    /** Viewport layer for every dialog: above any screen it is asked from (the match result sits at 1200). */
    static constexpr int32 ViewportZOrder=5000;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    void Close(bool bYes);
    FString Text,SkipKey;
    bool bQuestion=false,bSkipChecked=false;
    TFunction<void()> PendingYes;
    TWeakObjectPtr<UUserWidget> ReturnFocusTo;
};
