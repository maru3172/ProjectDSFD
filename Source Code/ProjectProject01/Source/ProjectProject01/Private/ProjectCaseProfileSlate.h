#pragma once
#include "ProjectCasePresentation.h"
#include "ProjectDFSDUITheme.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace ProjectCaseProfile
{
    inline FString Name(const FString& Id)
    {
        static const TMap<FString,FString> Names{{TEXT("banner_lastlight"),TEXT("마지막 점등")},{TEXT("banner_silentfloor"),TEXT("침묵의 층")},{TEXT("frame_burntcase"),TEXT("그을린 사건철")},{TEXT("banner_emergency"),TEXT("비상등 아래")},{TEXT("banner_shutter"),TEXT("닫힌 셔터")},{TEXT("plate_nightwatch"),TEXT("야간 경비 명찰")},{TEXT("seal_unresolved"),TEXT("미결 사건 인장")},{TEXT("icon_masterkey"),TEXT("분실된 마스터키")},{TEXT("frame_carbon"),TEXT("먹지 테두리")},{TEXT("frame_evidence"),TEXT("증거 봉투 테두리")},{TEXT("banner_escalator"),TEXT("멈춘 에스컬레이터")},{TEXT("banner_service"),TEXT("직원 전용 통로")},{TEXT("plate_lostfound"),TEXT("분실물 보관 명찰")},{TEXT("stamp_closed"),TEXT("폐점 확인 소인")},{TEXT("badge_rooftop"),TEXT("옥상 출입 배지")},{TEXT("icon_floormap"),TEXT("접힌 층별 안내도")},{TEXT("icon_ticket"),TEXT("빛바랜 대기표")},{TEXT("icon_locker"),TEXT("보관함 열쇠")},{TEXT("stamp_received"),TEXT("접수 완료 도장")},{TEXT("stamp_checked"),TEXT("열람 확인 도장")},{TEXT("badge_basement"),TEXT("지하층 기념 배지")},{TEXT("badge_service"),TEXT("안내 데스크 배지")},{TEXT("frame_staple"),TEXT("스테이플 문서 테두리")},{TEXT("plate_archive"),TEXT("기록실 직원 명찰")},{TEXT("stamp_checkpoint"),TEXT("첫 기록 인장")},{TEXT("badge_exit"),TEXT("탈출구 기념 배지")},{TEXT("seal_case"),TEXT("사건 종결 인장")},{TEXT("plate_investigator"),TEXT("조사 직원 명찰")},{TEXT("plate_training"),TEXT("교육 직원 명찰")},{TEXT("badge_survivor"),TEXT("생존 기록 배지")},{TEXT("stamp_mannequin"),TEXT("추적 기록 도장")},{TEXT("banner_afterhours"),TEXT("폐점 이후")},{TEXT("banner_embers"),TEXT("잔불")},{TEXT("frame_archive"),TEXT("보관 문서 테두리")},{TEXT("icon_receipt"),TEXT("오래된 영수증")},{TEXT("stamp_store"),TEXT("백화점 소인")},{TEXT("badge_directory"),TEXT("층별 안내 배지")}};
        if(const FString* N=Names.Find(Id))return *N;return ProjectCasePresentation::ItemName(Id);
    }
    inline FString Achievement(const FString& Code)
    {
        static const TMap<FString,FString> Names{{TEXT("campaign_checkpoint"),TEXT("첫 기록")},{TEXT("campaign_escape"),TEXT("출구를 찾다")},{TEXT("campaign_complete"),TEXT("사건의 끝")},{TEXT("pvp_first"),TEXT("대전 참가")},{TEXT("bot_first"),TEXT("모의 조사")},{TEXT("survivor_escape"),TEXT("생존 기록")},{TEXT("mannequin_victory"),TEXT("추적 기록")}};
        if(const FString* N=Names.Find(Code))return *N;return Code;
    }
    class SInlineBanner final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SInlineBanner):_ItemId(FString()),_Background(false){} SLATE_ATTRIBUTE(FString,ItemId) SLATE_ARGUMENT(bool,Background) SLATE_END_ARGS()
        void Construct(const FArguments& A){ItemId=A._ItemId;bBackground=A._Background;ChildSlot[SNew(SBox).WidthOverride(60).HeightOverride(20)];SetToolTipText(TAttribute<FText>::CreateLambda([this]{return FText::FromString(Name(ItemId.Get()));}));}
        virtual void Tick(const FGeometry&,double,float)override{const auto Id=ItemId.Get();if(Id!=PaintedId){PaintedId=Id;Invalidate(EInvalidateWidgetReason::Paint);}}
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
        {
            const auto Id=ItemId.Get();if(Id.IsEmpty())return Layer;const auto Size=G.GetLocalSize();const auto Tint=Style.GetColorAndOpacityTint();const auto* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
            const uint32 Seed=FCrc::StrCrc32(*Id);const float Variant=(Seed%7)/6.f;
            const FLinearColor Color=Id==TEXT("banner_embers")?FLinearColor(.13f,.07f,.065f):FLinearColor(.045f+Variant*.025f,.06f+Variant*.025f,.08f+Variant*.02f);
            for(int32 I=0;I<12;I++){const float Alpha=bBackground?.55f*(1-I/12.f):1.f;FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(FVector2D(Size.X/12+1,Size.Y),FSlateLayoutTransform(FVector2D(Size.X*I/12,0))),Brush,ESlateDrawEffect::None,Color.CopyWithNewOpacity(Alpha)*Tint);}
            const float Begin=bBackground?Size.X*.78f:6.f,End=Size.X-6.f;const FLinearColor LineColor=(Id==TEXT("banner_embers")?FLinearColor(.34f,.22f,.18f):FLinearColor(.22f,.30f,.35f)).CopyWithNewOpacity(bBackground?.32f:.75f);
            TArray<FVector2D> Edge{{0,Size.Y-1},{Size.X,Size.Y-1}};FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Edge,ESlateDrawEffect::None,LineColor*Tint,true,1);
            const int32 Marks=3+Seed%4;for(int32 I=0;I<Marks;I++){float X=FMath::Lerp(Begin,End,float(I)/(Marks-1));TArray<FVector2D> Mark{{X,Size.Y*(.15f+.05f*((Seed>>I)%4))},{X-(Seed%3)*2,Size.Y*(.6f+.04f*((Seed>>(I+2))%5))}};FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Mark,ESlateDrawEffect::None,LineColor*Tint,true,1);}
            return SCompoundWidget::OnPaint(A,G,Clip,Out,Layer+2,Style,Enabled);
        }
    private:TAttribute<FString> ItemId;FString PaintedId;bool bBackground=false;
    };
    class SEmblem final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SEmblem):_Height(64.f){} SLATE_ARGUMENT(FString,ItemId) SLATE_ARGUMENT(float,Height) SLATE_END_ARGS()
        void Construct(const FArguments& A){Id=A._ItemId;ChildSlot[SNew(SBox).HeightOverride(A._Height>0?A._Height:64.f)];}
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
        {
            const FVector2D S=G.GetLocalSize(),C=S*.5;const float R=FMath::Min(float(S.X),float(S.Y))*.34f;const auto Tint=Style.GetColorAndOpacityTint();
            const uint32 Seed=FCrc::StrCrc32(*Id);const float Variant=(Seed%9)/8.f;
            const FLinearColor Ink=Id.Contains(TEXT("embers"))?FLinearColor(.4f,.13f,.1f):FLinearColor(.33f+Variant*.1f,.37f+Variant*.06f,.4f+Variant*.04f);
            auto Line=[&](TArray<FVector2D> Points,float Width=1.4f){FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Ink*Tint,true,Width);};
            if(Id.StartsWith(TEXT("stamp"))||Id.StartsWith(TEXT("seal"))||Id.StartsWith(TEXT("badge")))
            {
                TArray<FVector2D> Circle;const int32 Sides=Id.StartsWith(TEXT("badge"))?6:32;for(int32 I=0;I<=Sides;I++){const float T=2*PI*I/Sides;Circle.Add(C+FVector2D(FMath::Cos(T),FMath::Sin(T))*R);}Line(Circle,2);
                if(Seed%2)Line({C+FVector2D(-R*.5,-R*.15),C+FVector2D(-R*.12,R*.3),C+FVector2D(R*.5,-R*.4)},2);
                else {Line({C+FVector2D(-R*.45,-R*.4),C+FVector2D(R*.45,-R*.4),C+FVector2D(R*.45,R*.4),C+FVector2D(-R*.45,R*.4),C+FVector2D(-R*.45,-R*.4)});for(int32 I=0;I<1+int32(Seed%3);I++)Line({C+FVector2D(-R*.3,R*(-.2+I*.2)),C+FVector2D(R*.3,R*(-.2+I*.2))});}
            }
            else if(Id.StartsWith(TEXT("banner")))
            {
                const float W=FMath::Min(float(S.X)-12,R*4),H=W/6;Line({C+FVector2D(-W*.5,-H),C+FVector2D(W*.5,-H),C+FVector2D(W*.5,H),C+FVector2D(-W*.5,H),C+FVector2D(-W*.5,-H)});
                const int32 Marks=3+Seed%5;for(int32 I=0;I<Marks;I++){const float X=-W*.36+I*W*.72/(Marks-1);Line({C+FVector2D(X,H*.65),C+FVector2D(X-W*.025f*(Seed%3),-H*(.25f+.1f*((Seed>>I)%6)))});}
            }
            else if(Id.Contains(TEXT("key"))||Id.Contains(TEXT("locker")))
            {
                TArray<FVector2D> Ring;for(int32 I=0;I<=24;I++){float T=2*PI*I/24;Ring.Add(C+FVector2D(-R*.4,0)+FVector2D(FMath::Cos(T),FMath::Sin(T))*R*.35);}Line(Ring,2);Line({C+FVector2D(-R*.05,0),C+FVector2D(R*.8,0),C+FVector2D(R*.8,R*.25),C+FVector2D(R*.55,R*.25),C+FVector2D(R*.55,0)},2);
            }
            else
            {
                Line({C+FVector2D(-R,-R*.7),C+FVector2D(R,-R*.7),C+FVector2D(R,R*.7),C+FVector2D(-R,R*.7),C+FVector2D(-R,-R*.7)});
                for(int32 I=0;I<2+int32(Seed%3);I++)Line({C+FVector2D(-R*.6,R*(-.4+I*.25)),C+FVector2D(R*(.15+.15*((Seed>>I)%4)),R*(-.4+I*.25))});
            }
            return SCompoundWidget::OnPaint(A,G,Clip,Out,Layer+1,Style,Enabled);
        }
    private:FString Id;
    };
    class SDossier final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SDossier){} SLATE_ARGUMENT(TSharedPtr<FJsonObject>,State) SLATE_ARGUMENT(FString,DisplayName) SLATE_ARGUMENT(FString,Bio) SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            State=A._State;auto Text=[](const FString& S,int Size){return SNew(STextBlock).Text(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(true);};
            auto Body=SNew(SVerticalBox);auto Identity=SNew(SHorizontalBox);Identity->AddSlot().AutoWidth().Padding(0,0,12,0)[SNew(SBox).WidthOverride(48)[SNew(SEmblem).ItemId(ProjectCasePresentation::Equipped(State,TEXT("Icon"))).Height(48)]];Identity->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(A._DisplayName,24)];Body->AddSlot().AutoHeight()[Identity];
            double Level=1;if(State)State->TryGetNumberField(TEXT("level"),Level);
            const FString Plate=ProjectCasePresentation::Equipped(State,TEXT("Nameplate"));
            Body->AddSlot().AutoHeight().Padding(0,5)[Text(FString::Printf(TEXT("Lv. %d"),int32(Level))+(Plate.IsEmpty()?FString():TEXT(" · ")+Name(Plate)),15)];
            if(!A._Bio.IsEmpty())Body->AddSlot().AutoHeight().Padding(0,8)[Text(A._Bio,16)];
            auto Decorations=SNew(SHorizontalBox);
            for(const TCHAR* Slot:{TEXT("Stamp"),TEXT("Badge")}){const FString Id=ProjectCasePresentation::Equipped(State,Slot);if(!Id.IsEmpty())Decorations->AddSlot().AutoWidth().Padding(0,0,16,0)[SNew(SBox).WidthOverride(38).ToolTipText(FText::FromString(Name(Id)))[SNew(SEmblem).ItemId(Id).Height(38)]];}
            Body->AddSlot().AutoHeight().Padding(0,8)[Decorations];
            const TSharedPtr<FJsonObject>* Collection=nullptr;const TArray<TSharedPtr<FJsonValue>>* Featured=nullptr;
            if(State){State->TryGetArrayField(TEXT("featured"),Featured);if(!Featured&&State->TryGetObjectField(TEXT("collection"),Collection))(*Collection)->TryGetArrayField(TEXT("featured"),Featured);}
            const TArray<TSharedPtr<FJsonValue>>* Banners=nullptr;
            if(State)State->TryGetArrayField(TEXT("displayedBanners"),Banners);if(!Banners&&Collection)(*Collection)->TryGetArrayField(TEXT("displayedBanners"),Banners);
            if(Banners&&!Banners->IsEmpty())
            {
                auto Strip=SNew(SWrapBox).UseAllottedSize(true);const FString Representative=ProjectCasePresentation::Equipped(State,TEXT("Banner"));
                for(auto V:*Banners)
                {
                    FString Id;V->AsObject()->TryGetStringField(TEXT("itemId"),Id);const bool Primary=Id==Representative;auto Chip=SNew(SHorizontalBox);
                    Chip->AddSlot().AutoWidth()[SNew(SInlineBanner).ItemId(Id)];if(Primary)Chip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6,0)[Text(TEXT("대표"),12)];
                    Strip->AddSlot().Padding(0,4,8,4)[SNew(SBorder).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(3)[Chip]];
                }
                Body->AddSlot().AutoHeight()[Strip];
            }
            if(Featured){auto Cards=SNew(SHorizontalBox);for(auto V:*Featured){FString Code;V->AsObject()->TryGetStringField(TEXT("code"),Code);Cards->AddSlot().FillWidth(1).Padding(0,6,8,0)[SNew(SBorder).BorderBackgroundColor(ProjectDFSDUI::Panel())[Text(Achievement(Code),15)]];}Body->AddSlot().AutoHeight()[Cards];}
            ChildSlot.Padding(FMargin(22,28,22,16))[Body];
        }
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
        {
            const auto Size=G.GetLocalSize();const auto* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));auto Color=ProjectDFSDUI::Panel();
            FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),Brush,ESlateDrawEffect::None,Color*Style.GetColorAndOpacityTint());
            const FString Banner=ProjectCasePresentation::Equipped(State,TEXT("Banner"));
            const FLinearColor Band=Banner==TEXT("banner_embers")?FLinearColor(.15f,.045f,.035f):Banner==TEXT("banner_afterhours")?FLinearColor(.035f,.065f,.09f):FLinearColor(.065f,.065f,.065f);
            FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(Size.X,Banner.IsEmpty()?12.f:76.f),FSlateLayoutTransform()),Brush,ESlateDrawEffect::None,Band*Style.GetColorAndOpacityTint());
            TArray<FVector2D> Line{{0,Size.Y-1},{Size.X,Size.Y-1}};FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,ProjectCasePresentation::FrameColor(State)*Style.GetColorAndOpacityTint(),true,2);
            if(!Banner.IsEmpty())for(int32 I=0;I<9;I++){const float X=Size.X*(.55f+I*.05f);TArray<FVector2D> Strokes{{X,12},{X-12,26}};FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Strokes,ESlateDrawEffect::None,Band*Style.GetColorAndOpacityTint(),true,1);}
            return SCompoundWidget::OnPaint(A,G,Clip,Out,Layer+2,Style,Enabled);
        }
    private:TSharedPtr<FJsonObject> State;
    };
}
