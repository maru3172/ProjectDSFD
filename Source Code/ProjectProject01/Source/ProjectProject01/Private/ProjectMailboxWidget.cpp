#include "ProjectMailboxWidget.h"
#include "ProjectCaseRecords.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    FSlateFontInfo MailFont(int32 Size) { return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size); }
    TSharedRef<STextBlock> MailText(const FString& Value, int32 Size, FLinearColor Color, bool bWrap = false)
    {
        return SNew(STextBlock).Font(MailFont(Size)).ColorAndOpacity(Color).AutoWrapText(bWrap).Text(FText::FromString(Value));
    }
    TSharedRef<SButton> MailButton(const FString& Label, TFunction<void()> Act, TAttribute<bool> Enabled = true)
    {
        return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(12, 5)).HAlign(HAlign_Center).IsEnabled(Enabled)
            .OnClicked_Lambda([Act = MoveTemp(Act)] { Act(); return FReply::Handled(); })[MailText(Label, 15, ProjectDFSDUI::Text())];
    }
    /** "23일 남음", "5시간 남음", "기한 없음". */
    FString TimeLeft(const TSharedPtr<FJsonObject>& Mail)
    {
        FString Expires; FDateTime At;
        if (!Mail->TryGetStringField(TEXT("expiresUtc"), Expires) || Expires.IsEmpty() || !FDateTime::ParseIso8601(*Expires, At)) return TEXT("기한 없음");
        const FTimespan Left = At - FDateTime::UtcNow();
        return Left.GetTotalDays() >= 1 ? FString::Printf(TEXT("%d일 남음"), int32(Left.GetTotalDays())) : FString::Printf(TEXT("%d시간 남음"), FMath::Max(1, int32(Left.GetTotalHours())));
    }
    FString RewardLine(const TSharedPtr<FJsonObject>& Mail)
    {
        TArray<FString> Parts; double Credits = 0, Tickets = 0;
        Mail->TryGetNumberField(TEXT("credits"), Credits); Mail->TryGetNumberField(TEXT("tickets"), Tickets);
        if (Credits > 0) Parts.Add(FString::Printf(TEXT("크레딧 %d"), int32(Credits)));
        if (Tickets > 0) Parts.Add(FString::Printf(TEXT("뽑기권 %d장"), int32(Tickets)));
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (Mail->TryGetArrayField(TEXT("items"), Items))
            for (const auto& V : *Items) if (const auto O = V->AsObject()) Parts.Add(O->GetStringField(TEXT("name")));
        return FString::Join(Parts, TEXT(" · "));
    }
}

UProjectMailboxWidget* UProjectMailboxWidget::Open(APlayerController* Player)
{
    if (!Player) return nullptr;
    auto* Widget = CreateWidget<UProjectMailboxWidget>(Player, UProjectMailboxWidget::StaticClass());
    if (!Widget) return nullptr;
    Widget->SetIsFocusable(true);
    Widget->AddToViewport(ViewportZOrder);
    Widget->SetKeyboardFocus();
    return Widget;
}
void UProjectMailboxWidget::Close() { RemoveFromParent(); }
void UProjectMailboxWidget::ShowForTest(const TSharedPtr<FJsonObject>& Mailbox) { bTest = true; Apply(Mailbox); }
void UProjectMailboxWidget::NativeConstruct() { Super::NativeConstruct(); if (!bTest) Request(TEXT("GET"), TEXT("/api/mail")); }
void UProjectMailboxWidget::NativeDestruct() { if (Pending) { Pending->OnProcessRequestComplete().Unbind(); Pending->CancelRequest(); Pending.Reset(); } Super::NativeDestruct(); }
FReply UProjectMailboxWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { Close(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry, Event);
}
void UProjectMailboxWidget::ReleaseSlateResources(bool bReleaseChildren) { Super::ReleaseSlateResources(bReleaseChildren); List.Reset(); CouponInput.Reset(); }

void UProjectMailboxWidget::Request(const FString& Verb, const FString& Path, const FString& Body)
{
    auto* Auth = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>() : nullptr;
    if (!Auth || !Auth->IsSignedIn() || bBusy) return;
    bBusy = true;
    Pending = FHttpModule::Get().CreateRequest();
    Pending->SetURL(Auth->GetApiBaseUrlForAuthenticatedRequest() + Path); Pending->SetVerb(Verb); Pending->SetTimeout(6);
    Pending->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Auth->GetAccessTokenForAuthenticatedRequest());
    Pending->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
    FProjectProject01VersionContract::ApplyToRequest(Pending.ToSharedRef());
    if (Verb == TEXT("POST")) Pending->SetContentAsString(Body.IsEmpty() ? FString(TEXT("{}")) : Body);
    Pending->OnProcessRequestComplete().BindWeakLambda(this, [this, Path](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
    {
        bBusy = false; Pending.Reset();
        TSharedPtr<FJsonObject> Json;
        const bool bParsed = bConnected && Response && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json) && Json.IsValid();
        if (!bParsed) { Status = TEXT("우편함에 연결하지 못했습니다. 잠시 후 다시 시도하세요."); RebuildList(); return; }
        FString Message; Json->TryGetStringField(TEXT("message"), Message);
        if (!EHttpResponseCodes::IsOk(Response->GetResponseCode())) { Status = Message.IsEmpty() ? TEXT("처리하지 못했습니다.") : Message; RebuildList(); return; }
        const TSharedPtr<FJsonObject>* Mailbox = nullptr;
        Status = Path == TEXT("/api/mail") ? FString() : Message;
        if (Path.StartsWith(TEXT("/api/coupons"))) if (CouponInput.IsValid()) CouponInput->SetText(FText::GetEmpty());
        // Claims change credits and tickets: the lobby's numbers and the envelope's dot refresh with the records.
        if (Path != TEXT("/api/mail")) if (auto* Records = GetGameInstance()->GetSubsystem<UProjectCaseRecords>()) Records->Refresh();
        Apply(Json->TryGetObjectField(TEXT("mailbox"), Mailbox) ? *Mailbox : Json);
    });
    Pending->ProcessRequest();
    RebuildList();
}
void UProjectMailboxWidget::Apply(const TSharedPtr<FJsonObject>& Mailbox) { Box = Mailbox; RebuildList(); }

void UProjectMailboxWidget::RebuildList()
{
    if (!List.IsValid()) return;
    List->ClearChildren();
    const TArray<TSharedPtr<FJsonValue>>* Mail = nullptr;
    if (!Box.IsValid() || !Box->TryGetArrayField(TEXT("mail"), Mail) || Mail->IsEmpty())
    {
        List->AddSlot().AutoHeight().Padding(8, 24)[MailText(Box.IsValid() ? TEXT("우편이 없습니다.") : TEXT("우편을 불러오는 중…"), 15, ProjectDFSDUI::Muted())];
        return;
    }
    for (const auto& V : *Mail)
    {
        const auto M = V->AsObject(); if (!M) continue;
        const int64 Id = int64(M->GetNumberField(TEXT("id")));
        const bool bClaimed = M->GetBoolField(TEXT("claimed"));
        const FString Rewards = RewardLine(M);
        TSharedRef<SWidget> Action = bClaimed || Rewards.IsEmpty()
            ? MailButton(TEXT("지우기"), [this, Id] { Request(TEXT("POST"), FString::Printf(TEXT("/api/mail/%lld/delete"), Id)); }, TAttribute<bool>::CreateLambda([this] { return !bBusy; }))
            : MailButton(TEXT("받기"), [this, Id] { Request(TEXT("POST"), FString::Printf(TEXT("/api/mail/%lld/claim"), Id)); }, TAttribute<bool>::CreateLambda([this] { return !bBusy; }));
        List->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(FMargin(14, 10))
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(MailFont(16)).ColorAndOpacity(ProjectDFSDUI::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                        .Text(FText::FromString(M->GetStringField(TEXT("title"))))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)[MailText(M->GetStringField(TEXT("body")), 13, ProjectDFSDUI::Muted(), true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)[MailText(Rewards.IsEmpty() ? FString(TEXT("보상 없음")) : Rewards, 14, ProjectDFSDUI::Accent(), true)]
                    + SVerticalBox::Slot().AutoHeight()[MailText(bClaimed ? FString(TEXT("받음")) : TimeLeft(M), 12, ProjectDFSDUI::Muted())]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[SNew(SBox).WidthOverride(86)[Action]]]];
    }
}

TSharedRef<SWidget> UProjectMailboxWidget::RebuildWidget()
{
    auto Root = SNew(SOverlay)
        + SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0, 0, 0, .55f))]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(640).MaxDesiredHeight(540)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Canvas()).Padding(18)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[MailText(TEXT("우편함"), 22, ProjectDFSDUI::Text())]
                        + SHorizontalBox::Slot().AutoWidth()[MailButton(TEXT("닫기"), [this] { Close(); })]]
                    // Coupon: the reward is mailed here.
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 4)[SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SAssignNew(CouponInput, SEditableTextBox).Style(&ProjectDFSDUI::InputStyle()).Font(MailFont(15))
                            .HintText(FText::FromString(TEXT("쿠폰 코드 입력"))).OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type How)
                            { if (How == ETextCommit::OnEnter && !Text.IsEmpty()) { auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("code"), Text.ToString()); FString B; FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&B)); Request(TEXT("POST"), TEXT("/api/coupons/redeem"), B); } })]
                        + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[MailButton(TEXT("쿠폰 사용"), [this]
                        {
                            if (!CouponInput.IsValid() || CouponInput->GetText().IsEmpty()) { Status = TEXT("쿠폰 코드를 입력하세요."); RebuildList(); return; }
                            auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("code"), CouponInput->GetText().ToString()); FString B; FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&B));
                            Request(TEXT("POST"), TEXT("/api/coupons/redeem"), B);
                        }, TAttribute<bool>::CreateLambda([this] { return !bBusy; }))]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)[SNew(STextBlock).Font(MailFont(13)).ColorAndOpacity(ProjectDFSDUI::Muted()).AutoWrapText(true)
                        .Text_Lambda([this] { return FText::FromString(Status); })]
                    + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(List, SVerticalBox)]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[MailButton(TEXT("모두 받기"), [this] { Request(TEXT("POST"), TEXT("/api/mail/claim-all")); },
                        TAttribute<bool>::CreateLambda([this] { double N = 0; return !bBusy && Box.IsValid() && Box->TryGetNumberField(TEXT("unclaimed"), N) && N > 0; }))]]]];
    RebuildList();
    return Root;
}
