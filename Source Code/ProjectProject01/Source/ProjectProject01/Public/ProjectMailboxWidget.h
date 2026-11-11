#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "ProjectMailboxWidget.generated.h"

class FJsonObject;

/**
 * 우편함 (Docs/MailAndCoupons.md): mail with rewards to claim (one or all), expiry shown, claimed mail can be removed,
 * and a coupon field whose reward arrives here. Opened from the envelope on the lobby bar; Esc (or pad B) closes it.
 */
UCLASS()
class PROJECTPROJECT01_API UProjectMailboxWidget final : public UUserWidget
{
    GENERATED_BODY()
    friend class FProjectMailboxWidgetTest;
public:
    static constexpr int32 ViewportZOrder = 1300;
    static UProjectMailboxWidget* Open(APlayerController* Player);
    void Close();
    /** Shows a mailbox without the server (layout tests). */
    void ShowForTest(const TSharedPtr<FJsonObject>& Mailbox);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
    void Request(const FString& Verb, const FString& Path, const FString& Body = FString());
    void Apply(const TSharedPtr<FJsonObject>& Mailbox);
    void RebuildList();
    TSharedPtr<FJsonObject> Box;
    FString Status;
    bool bBusy = false, bTest = false;
    TSharedPtr<class SVerticalBox> List;
    TSharedPtr<class SEditableTextBox> CouponInput;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Pending;
};
