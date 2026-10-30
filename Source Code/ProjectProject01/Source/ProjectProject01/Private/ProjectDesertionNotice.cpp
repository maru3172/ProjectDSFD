#include "ProjectDesertionNotice.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ProjectDFSDUITheme.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

FString UProjectDesertionNoticeWidget::Describe(const FString& Role)
{
    return Role==TEXT("Mannequin")
        ?TEXT("지난 경기에서 마네킹으로 플레이하던 중 경기를 떠나 기권 처리되었습니다.\n생존자 승리로 경기가 끝났습니다.\n\n경기 결과: 기권 (패배)")
        :TEXT("지난 경기에서 살아 있는 상태로 경기를 떠나 기권 처리되었습니다.\n동료가 남아 있었다면 남은 목숨은 봇이 이어받았습니다.\n\n경기 결과: 기권 (패배)");
}

TSharedRef<SWidget> UProjectDesertionNoticeWidget::RebuildWidget()
{
    auto Label=[](const TAttribute<FText>& Text,int32 Size,const FLinearColor& Color)
    {return SNew(STextBlock).Text(Text).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(Color).AutoWrapText(true).Justification(ETextJustify::Center);};
    return SNew(SOverlay)
        +SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.85f))]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SBox).WidthOverride(640)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(32)
                [
                    SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)[Label(FText::FromString(TEXT("기권 처리")),30,ProjectDFSDUI::Danger())]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,24)[Label(TAttribute<FText>::CreateWeakLambda(this,[this](){return FText::FromString(Describe(Role));}),16,ProjectDFSDUI::Text())]
                    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [
                        SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(48,10))
                        .OnClicked_Lambda([this](){Acknowledge();return FReply::Handled();})
                        [Label(FText::FromString(TEXT("확인")),18,ProjectDFSDUI::Text())]
                    ]
                ]
            ]
        ];
}

void UProjectDesertionNoticeWidget::Check()
{
    auto* A=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;
    SetVisibility(ESlateVisibility::Collapsed);if(Request||!A||!A->IsSignedIn())return;
    const FString Token=A->GetAccessTokenForAuthenticatedRequest(),Base=A->GetApiBaseUrlForAuthenticatedRequest();
    auto Pending=FHttpModule::Get().CreateRequest();Request=Pending;
    Pending->SetURL(Base+TEXT("/api/matches/desertions/pending"));Pending->SetVerb(TEXT("GET"));Pending->SetTimeout(5);
    Pending->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);FProjectProject01VersionContract::ApplyToRequest(Pending);
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this,Token](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Connected)
    {
        if(Request!=Completed)return;Request.Reset();
        auto* Auth=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;
        // No notice (204), a failure or a different session: nothing to show. It stays unseen for the next lobby visit.
        if(!Auth||Auth->GetAccessTokenForAuthenticatedRequest()!=Token||!Connected||!Response||Response->GetResponseCode()!=200||Response->GetContentLength()>4096)return;
        TSharedPtr<FJsonObject> J;FGuid Id;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)||!J||!J->TryGetStringField(TEXT("matchId"),MatchId)||!FGuid::Parse(MatchId,Id))return;
        J->TryGetStringField(TEXT("role"),Role);
        SetVisibility(ESlateVisibility::Visible);
    });
    if(!Pending->ProcessRequest())Request.Reset();
}

void UProjectDesertionNoticeWidget::Acknowledge()
{
    SetVisibility(ESlateVisibility::Collapsed);
    auto* A=GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01AuthSubsystem>():nullptr;if(!A||!A->IsSignedIn()||MatchId.IsEmpty())return;
    auto Pending=FHttpModule::Get().CreateRequest();
    Pending->SetURL(A->GetApiBaseUrlForAuthenticatedRequest()+TEXT("/api/matches/desertions/")+MatchId+TEXT("/seen"));Pending->SetVerb(TEXT("POST"));Pending->SetTimeout(5);
    Pending->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+A->GetAccessTokenForAuthenticatedRequest());FProjectProject01VersionContract::ApplyToRequest(Pending);
    // One notice at a time: once this one is marked seen, show the next if there is one.
    Pending->OnProcessRequestComplete().BindWeakLambda(this,[this](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected)
    {if(Connected&&Response&&EHttpResponseCodes::IsOk(Response->GetResponseCode())){MatchId.Reset();Check();}});
    Pending->ProcessRequest();
}

void UProjectDesertionNoticeWidget::NativeDestruct()
{
    if(Request){Request->OnProcessRequestComplete().Unbind();Request->CancelRequest();Request.Reset();}
    Super::NativeDestruct();
}
