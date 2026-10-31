#include "ProjectCaseRecords.h"
#include "ProjectCaseProfileSlate.h"
#include "Dom/JsonObject.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/GameInstance.h"
#include "ProjectProject01AuthSubsystem.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProjectUIRenderCapture.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#endif

namespace
{
    TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& J,const TCHAR* K){const TSharedPtr<FJsonObject>* V=nullptr;return J&&J->TryGetObjectField(K,V)?*V:nullptr;}
    FString Str(const TSharedPtr<FJsonObject>& J,const TCHAR* K){FString V;if(J)J->TryGetStringField(K,V);return V;}
    double Num(const TSharedPtr<FJsonObject>& J,const TCHAR* K){double V=0;if(J)J->TryGetNumberField(K,V);return V;}
    bool Flag(const TSharedPtr<FJsonObject>& J,const TCHAR* K){bool V=false;if(J)J->TryGetBoolField(K,V);return V;}
    TArray<TSharedPtr<FJsonValue>> Rows(const TSharedPtr<FJsonObject>& J,const TCHAR* K){const TArray<TSharedPtr<FJsonValue>>* V=nullptr;return J&&J->TryGetArrayField(K,V)?*V:TArray<TSharedPtr<FJsonValue>>{};}
    TSharedRef<STextBlock> Text(const FString& S,int Size=16){return SNew(STextBlock).Text(FText::FromString(S)).ToolTipText(FText::FromString(S)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(ProjectDFSDUI::Text()).AutoWrapText(false).OverflowPolicy(ETextOverflowPolicy::Ellipsis);}
    TSharedRef<SButton> Button(const FString& S,TFunction<void()> Action,bool Enabled=true){return SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(FMargin(12,6)).IsEnabled(Enabled).OnClicked_Lambda([Action=MoveTemp(Action)]{Action();return FReply::Handled();})[Text(S)];}
    const FSpinBoxStyle& ExchangeStyle(){static const FSpinBoxStyle Style=[] {auto S=FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("SpinBox"));const auto& Input=ProjectDFSDUI::InputStyle();S.SetBackgroundBrush(Input.BackgroundImageNormal).SetHoveredBackgroundBrush(Input.BackgroundImageHovered).SetActiveBackgroundBrush(Input.BackgroundImageFocused).SetForegroundColor(ProjectDFSDUI::Text());S.SetInactiveFillBrush(FSlateNoResource()).SetActiveFillBrush(FSlateNoResource()).SetHoveredFillBrush(FSlateNoResource());return S;}();return Style;}
    FLinearColor RarityColor(int32 Index)
    {
        switch(Index){case 0:return FLinearColor(.48f,.34f,.18f);case 1:return FLinearColor(.36f,.28f,.43f);case 2:return FLinearColor(.22f,.34f,.43f);case 3:return FLinearColor(.25f,.37f,.29f);default:return FLinearColor(.34f,.34f,.34f);}
    }
    float RevealDelay(int32 Index){return Index==0?2.6f:Index==1?1.48f:.65f;}
    float GoldLightStrength(float Elapsed,float Opening)
    {
        // Smooth, bounded rises: anticipation, shaking, then the product handoff. No flash or pulse.
        const float Rise=.08f*FMath::SmoothStep(0.f,1.8f,Elapsed)
            +.14f*FMath::SmoothStep(1.8f,2.45f,Elapsed)
            +.22f*FMath::SmoothStep(2.45f,2.95f,Elapsed);
        return FMath::SmoothStep(0.f,.14f,Opening)*Rise;
    }
    TArray<TSharedPtr<FJsonValue>> AwardsFor(UProjectCaseRecords* R)
    {auto A=Rows(R?R->DrawResult:nullptr,TEXT("awards"));A.Sort([](const auto& L,const auto& Right){return Num(L->AsObject(),TEXT("grade"))<Num(Right->AsObject(),TEXT("grade"));});return A;}
    class SDrawFeedback final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SDrawFeedback):_Message(FString()),_StartedAt(-100){} SLATE_ATTRIBUTE(FString,Message) SLATE_ATTRIBUTE(double,StartedAt) SLATE_END_ARGS()
        void Construct(const FArguments& A){Message=A._Message;StartedAt=A._StartedAt;Caption=Text(Message.Get(),14);Caption->SetColorAndOpacity(ProjectDFSDUI::Danger());ChildSlot[SNew(SBox).HeightOverride(18)[Caption.ToSharedRef()]];Update();}
        virtual void Tick(const FGeometry&,double,float)override{Update();}
    private:
        void Update(){Caption->SetText(FText::FromString(Message.Get()));const float Age=float(FPlatformTime::Seconds()-StartedAt.Get());Caption->SetRenderOpacity(Message.Get().IsEmpty()?0:FMath::Clamp(1-(Age-1.1f)/1.8f,0.f,1.f));}
        TAttribute<FString> Message;TAttribute<double> StartedAt;TSharedPtr<STextBlock> Caption;
    };
    class SParcel final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SParcel):_Start(0),_StyleIndex(0),_Height(88),_LightOnly(false),_DrawLight(true),_SlowLight(false){} SLATE_ARGUMENT(double,Start) SLATE_ARGUMENT(int32,StyleIndex) SLATE_ARGUMENT(float,Height) SLATE_ARGUMENT(FString,Caption) SLATE_ARGUMENT(bool,LightOnly) SLATE_ARGUMENT(bool,DrawLight) SLATE_ARGUMENT(bool,SlowLight) SLATE_END_ARGS()
        void Construct(const FArguments& A){Start=A._Start;Index=A._StyleIndex;LightOnly=A._LightOnly;DrawLight=A._DrawLight;SlowLight=A._SlowLight;Caption=Text(A._Caption,15);UpdateCaption();ChildSlot[SNew(SBox).HeightOverride(A._Height).VAlign(VAlign_Bottom).HAlign(HAlign_Center)[Caption.ToSharedRef()]];}
        void UpdateCaption(){Caption->SetRenderOpacity(Start<=0?1.f:FMath::Clamp(float(FPlatformTime::Seconds()-Start-RevealDelay(Index))/.18f,0.f,1.f));}
        virtual void Tick(const FGeometry&,double,float)override{if(Start>0&&FPlatformTime::Seconds()-Start<RevealDelay(Index)+3.f){UpdateCaption();Invalidate(EInvalidateWidgetReason::Paint);}}
        virtual int32 OnPaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
        {
            const float Scale=FMath::Max(.1f,float(G.GetLocalSize().Y)/88.f);const FVector2D S=G.GetLocalSize()/Scale;
            const float Elapsed=Start>0?FMath::Max(0.f,float(FPlatformTime::Seconds()-Start)):0;
            const float Anticipation=Index==0?2.45f:1.3f,Duration=Index<2?Anticipation+.25f:.95f;
            const float Progress=Start>0?FMath::Clamp(Elapsed/Duration,0.f,1.f):0;
            float T=Progress*Progress*(3-2*Progress);
            if(Start>0&&Index<2)T=Elapsed<Anticipation?.14f*FMath::Clamp(Elapsed/FMath::Min(Anticipation,1.8f),0.f,1.f):.14f+.86f*FMath::InterpEaseOut(0.f,1.f,FMath::Clamp((Elapsed-Anticipation)/.25f,0.f,1.f),2.f);
            const float Shake=Start>0&&Index==0&&Elapsed>1.8f&&Elapsed<2.45f?.65f*(FMath::Sin(Elapsed*53)+.35f*FMath::Sin(Elapsed*79)):0;
            const auto* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));const auto Tint=Style.GetColorAndOpacityTint();const float W=FMath::Min(float(S.X-30),94.f),X=(S.X-W)*.5f+Shake,Y=30;
            const FLinearColor Base(.095f,.09f,.085f),Edge(.22f,.215f,.205f);
            auto Box=[&](FVector2D P,FVector2D Ext,FLinearColor C){FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Ext*Scale,FSlateLayoutTransform(P*Scale)),Brush,ESlateDrawEffect::None,C*Tint);};
            auto Line=[&](TArray<FVector2D> Points,FLinearColor C,float Width=1.f){for(auto& P:Points)P*=Scale;FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C*Tint,true,Width*Scale);};
            const int32 Rarity=FMath::Clamp(Index,0,4);
            const float HaloEmphasis[]{1.65f,1.30f,1.12f,1.f,.85f};
            const FLinearColor LightColors[]{FLinearColor(1.f,.62f,.12f),FLinearColor(.68f,.44f,.94f),FLinearColor(.36f,.67f,.96f),FLinearColor(.42f,.78f,.53f),FLinearColor(.67f,.70f,.74f)};
            const FLinearColor Halo=LightColors[Rarity];
            const float LightRise=SlowLight?FMath::SmoothStep(0.f,RevealDelay(Index)+1.5f,Elapsed):FMath::SmoothStep(0.f,.9f,Elapsed);
            const float Strength=Rarity==0?GoldLightStrength(Elapsed,T):HaloEmphasis[Rarity]*T*(.10f+.22f*LightRise);
            // Light escapes through the opening; the wrapping and closed parcels never glow.
            // Paint light first: the opaque lid and body below occlude it, including in the solo stage.
            if(T>0 && DrawLight)
            {
                constexpr int32 Steps=24,Rings=4;
                TArray<FSlateVertex> Vertices;Vertices.Reserve(1+Steps*Rings);
                TArray<SlateIndex> Indices;Indices.Reserve(Steps*3+(Rings-1)*Steps*6);
                const FVector2D Center(X+W*.5f,Y-2-T*14),Radius(W*.72f,5+T*(SlowLight?38.f:24.f));
                auto Vertex=[&](FVector2D P,float Alpha){return Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                    G.GetAccumulatedRenderTransform(),FVector2f(P*Scale),FVector2f(.5f,.5f),(Halo.CopyWithNewOpacity(Alpha)*Tint).ToFColor(true)));};
                Vertex(Center,Strength);
                for(int32 Ring=1;Ring<=Rings;++Ring)
                {
                    const float Distance=float(Ring)/Rings;
                    for(int32 I=0;I<Steps;++I)
                    {
                        const float Angle=I*2.f*PI/Steps;
                        Vertex(Center+FVector2D(FMath::Cos(Angle)*Radius.X,FMath::Sin(Angle)*Radius.Y)*Distance,
                            Strength*FMath::Square(1-Distance));
                    }
                }
                for(int32 I=0;I<Steps;++I)Indices.Append({0,SlateIndex(1+I),SlateIndex(1+(I+1)%Steps)});
                for(int32 Ring=1;Ring<Rings;++Ring)for(int32 I=0;I<Steps;++I)
                {
                    const SlateIndex A0=1+(Ring-1)*Steps+I,A1=1+(Ring-1)*Steps+(I+1)%Steps;
                    const SlateIndex B0=A0+Steps,B1=A1+Steps;
                    Indices.Append({A0,B0,B1,A0,B1,A1});
                }
                const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);
                FSlateDrawElement::MakeCustomVerts(Out,Layer,Resource,Vertices,Indices,nullptr,0,0);
                // Soft tapered shafts originate in the aperture, rather than outlining the box.
                Vertices.Reset();Indices.Reset();
                for(int32 Beam=0;Beam<3;++Beam)
                {
                    const float Side=float(Beam-1),BaseX=X+W*(.5f+Side*.21f);
                    const FVector2D Source(BaseX,Y-2),Tip(BaseX+Side*W*.28f,Y-8-T*(SlowLight?(Beam==1?66.f:51.f):(Beam==1?32.f:25.f)));
                    const FVector2D HalfBase(W*.075f,0),HalfTip(W*(Beam==1?.29f:.20f),0);
                    const int32 First=Vertices.Num();
                    constexpr float Across[]{-1.f,-.7f,0.f,.7f,1.f},CrossFade[]{0.f,.35f,1.f,.35f,0.f};
                    constexpr float Along[]{0.f,.32f,.72f,1.f},LengthFade[]{.65f,.78f,.25f,0.f};
                    for(int32 Row=0;Row<4;++Row)for(int32 Column=0;Column<5;++Column)
                    {
                        const FVector2D Position=FMath::Lerp(Source,Tip,Along[Row]);
                        const FVector2D Width=FMath::Lerp(HalfBase,HalfTip,Along[Row]);
                        Vertex(Position+Width*Across[Column],Strength*CrossFade[Column]*LengthFade[Row]);
                    }
                    for(int32 Row=0;Row<3;++Row)for(int32 Column=0;Column<4;++Column)
                    {
                        const SlateIndex A0=First+Row*5+Column,A1=A0+1,B0=A0+5,B1=B0+1;
                        Indices.Append({A0,B0,B1,A0,B1,A1});
                    }
                }
                FSlateDrawElement::MakeCustomVerts(Out,Layer,Resource,Vertices,Indices,nullptr,0,0);
            }
            if(LightOnly)return SCompoundWidget::OnPaint(A,G,Clip,Out,Layer+1,Style,Enabled);
            ++Layer; // Keep opaque parcel surfaces above every light primitive.
            // Matte wrapping paper, a shallow side face and contact shadow.
            Box({X-3,Y+33},{W+15,4},FLinearColor(0,0,0,.25f));Box({X,Y},{W,32},Base);
            const float GrainY[]{2,6,11,17,22,26,30};for(int32 I=0;I<7;I++)Box({X+2+I%3,Y+GrainY[I]},{W-5-(I%4)*3.f,1},FLinearColor(.13f,.125f,.12f,.18f+(I%3)*.025f));
            Box({X+W,Y-4},{7,35},FLinearColor(.055f,.052f,.05f));
            Line({{X,Y},{X,Y+32},{X+W,Y+32},{X+W+7,Y+27},{X+W+7,Y-4}},Edge);
            Box({X+W*.46f,Y},{W*.08f,32},FLinearColor(.23f,.22f,.20f));Box({X+W*.69f,Y+9},{W*.20f,10},FLinearColor(.30f,.29f,.27f));
            Line({{X+W*.72f,Y+13},{X+W*.85f,Y+13}},FLinearColor(.11f,.105f,.10f));
            if(T>0 && DrawLight)
            {
                Box({X+3,Y-2},{W-6,2},Halo.CopyWithNewOpacity(Strength*1.3f));
            }
            Box({X-2,Y-8-T*22},{W+8,7},FLinearColor(.18f,.175f,.165f));Line({{X-2,Y-8-T*22},{X+W+6,Y-8-T*22}},Edge);
            return SCompoundWidget::OnPaint(A,G,Clip,Out,Layer+1,Style,Enabled);
        }
    private:double Start=0;int32 Index=0;bool LightOnly=false,DrawLight=true,SlowLight=false;TSharedPtr<STextBlock> Caption;
    };
    class SRareShowcase final:public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SRareShowcase):_StartedAt(0){} SLATE_ARGUMENT(TSharedPtr<FJsonObject>,Prize) SLATE_ARGUMENT(double,StartedAt) SLATE_ARGUMENT(TFunction<void()>,Finished) SLATE_ARGUMENT(TFunction<void(double)>,Skipped) SLATE_END_ARGS()
        void Construct(const FArguments& A)
        {
            StartedAt=A._StartedAt;Finished=A._Finished;Skipped=A._Skipped;Index=int32(Num(A._Prize,TEXT("grade")))-1;
            auto GradeCaption=Text(FString::Printf(TEXT("%d등급"),Index+1),18);GradeCaption->SetColorAndOpacity(RarityColor(Index));
            auto ProductBox=SNew(SVerticalBox);ProductBox->AddSlot().AutoHeight().HAlign(HAlign_Center)[GradeCaption];
            ProductBox->AddSlot().AutoHeight().Padding(0,16)[SNew(ProjectCaseProfile::SEmblem).ItemId(Str(A._Prize,TEXT("itemId"))).Height(240)];
            ProductBox->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0,12)[Text(Str(A._Prize,TEXT("name")),28)];Product=ProductBox;Product->SetRenderTransformPivot(FVector2D(.5,.5));
            Light=SNew(SParcel).Start(StartedAt).StyleIndex(Index).Height(240).LightOnly(true).SlowLight(true).Visibility(EVisibility::HitTestInvisible);
            Parcel=SNew(SParcel).Start(StartedAt).StyleIndex(Index).Height(240).DrawLight(false).Caption(FString());
            auto Stage=SNew(SOverlay)+SOverlay::Slot().VAlign(VAlign_Center)[Light.ToSharedRef()]+SOverlay::Slot().VAlign(VAlign_Center)[Parcel.ToSharedRef()]+SOverlay::Slot().VAlign(VAlign_Center)[Product.ToSharedRef()];
            ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.014f,.014f,.015f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(520).HeightOverride(360)[Stage]]];
            Update();
        }
        virtual void Tick(const FGeometry&,double,float)override{Update();}
        virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& Event)override
        {
            if(Event.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Handled();
            const double Now=FPlatformTime::Seconds(),RevealAt=SkippedAt>0?SkippedAt:StartedAt+RevealDelay(Index);
            if(Now<RevealAt){SkippedAt=Now;if(Skipped)Skipped(Now-RevealDelay(Index));Update();return FReply::Handled();}
            if(Now<RevealAt+.22)return FReply::Handled();
            auto Done=Finished;if(Done)Done();return FReply::Handled();
        }
        virtual FReply OnMouseButtonDoubleClick(const FGeometry& G,const FPointerEvent& Event)override{return OnMouseButtonDown(G,Event);}
    private:
        void Update()
        {
            const double RevealAt=SkippedAt>0?SkippedAt:StartedAt+RevealDelay(Index);float T=FMath::Clamp(float(FPlatformTime::Seconds()-RevealAt)/.18f,0.f,1.f);T=1-FMath::Square(1-T);
            Product->SetRenderOpacity(T);Parcel->SetRenderOpacity(T>0?0.f:1.f);Product->SetRenderTransform(FSlateRenderTransform(FScale2D(.94f+.06f*T)));
            // The solo product view is clean. The separate opened-parcel results retain their light.
            Light->SetVisibility(T>0?EVisibility::Hidden:EVisibility::HitTestInvisible);
        }
        int32 Index=0;double StartedAt=0,SkippedAt=0;TFunction<void()> Finished;TFunction<void(double)> Skipped;TSharedPtr<SWidget> Parcel,Product,Light;
    };
}
TSharedRef<SWidget> UProjectCaseBanner::RebuildWidget(){return SNew(ProjectCaseProfile::SInlineBanner).ItemId_Lambda([this]{return ItemId;});}
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectParcelLightTest,"ProjectProject01.CaseRecords.ParcelLight",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectParcelLightTest::RunTest(const FString&)
{
    float Previous=0;
    for(int32 Frame=0;Frame<=240;++Frame)
    {
        const float Brightness=GoldLightStrength(Frame/60.f,1.f);
        TestTrue(TEXT("Gold brightness rises smoothly without a flash or overbright spike"),Brightness>=Previous && Brightness-Previous<.02f && Brightness<=.441f);
        Previous=Brightness;
    }
    const FIntPoint Size(800,600);const FString Directory=FPaths::ProjectSavedDir()/TEXT("CaseRecordUIQA");
    IFileManager::Get().MakeDirectory(*Directory,true);FWidgetRenderer Renderer(false);
    auto Capture=[&](TSharedRef<SWidget> Widget,const FString& Name)
    {
        auto* Target=ProjectUIRenderCapture::Create(Size);
        for(int32 I=0;I<3;++I)Renderer.DrawWidget(Target,Widget,FVector2D(Size),0);
        FlushRenderingCommands();TArray<FColor> Pixels;
        TestTrue(TEXT("Parcel lighting renders pixels"),ProjectUIRenderCapture::Read(Target,Pixels)&&Pixels.Num()==Size.X*Size.Y);
        if(Pixels.Num()==Size.X*Size.Y&&!Name.IsEmpty())
        {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Directory/(Name+TEXT(".png"))));}
        Target->ReleaseResource();return Pixels;
    };
    auto ParcelFrame=[&](double Age,bool Lit)
    {return SNew(SOverlay)+SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(220).HeightOverride(88)[SNew(SParcel).Start(Age<0?0:FPlatformTime::Seconds()-Age).StyleIndex(0).DrawLight(Lit)]];};
    const auto Bare=Capture(ParcelFrame(30,false),FString());
    const auto Lit=Capture(ParcelFrame(30,true),TEXT("ParcelGoldPersistent"));
    const auto Earlier=Capture(ParcelFrame(10,true),FString());
    const auto Closed=Capture(ParcelFrame(-1,true),FString());
    const auto ClosedBare=Capture(ParcelFrame(-1,false),FString());
    if(Bare.Num()==Size.X*Size.Y&&Lit.Num()==Bare.Num()&&Earlier.Num()==Bare.Num()&&Closed.Num()==Bare.Num()&&ClosedBare.Num()==Bare.Num())
    {
        int32 LitPixels=0,SurfaceLeaks=0,FadeDifferences=0,ClosedLeaks=0;
        // Centered 220x88 parcel: lid y=256..263, body y=286..318, x=353..447.
        for(int32 Y=200;Y<320;++Y)for(int32 X=320;X<480;++X)
        {
            const int32 P=Y*Size.X+X;
            if(Lit[P]!=Bare[P]){++LitPixels;if(X>=356&&X<=444&&((Y>=257&&Y<=262)||(Y>=287&&Y<=316)))++SurfaceLeaks;}
            FadeDifferences+=Lit[P]!=Earlier[P];ClosedLeaks+=Closed[P]!=ClosedBare[P];
        }
        TestTrue(TEXT("Light escapes into surrounding space"),LitPixels>100);
        TestEqual(TEXT("Opaque lid and body fully block light"),SurfaceLeaks,0);
        TestEqual(TEXT("Light remains at full strength after ten and thirty seconds"),FadeDifferences,0);
        TestEqual(TEXT("Closed boxes never emit light"),ClosedLeaks,0);
    }
    auto Prize=MakeShared<FJsonObject>();Prize->SetNumberField(TEXT("grade"),1);Prize->SetStringField(TEXT("name"),TEXT("폐점 이후"));Prize->SetStringField(TEXT("itemId"),TEXT("banner_afterhours"));
    const auto ProductPixels=Capture(SNew(SRareShowcase).Prize(Prize).StartedAt(FPlatformTime::Seconds()-30),TEXT("RareGoldProduct"));
    if(ProductPixels.Num()==Size.X*Size.Y)
    {
        int32 HaloPixels=0;
        for(int32 Y=40;Y<110;++Y)for(int32 X=220;X<580;++X)
        {const auto P=ProductPixels[Y*Size.X+X];HaloPixels+=int32(P.R)>int32(P.B)+12;}
        TestEqual(TEXT("Solo product display has no opening-light halo"),HaloPixels,0);
    }
    Capture(SNew(SRareShowcase).Prize(Prize).StartedAt(FPlatformTime::Seconds()-1.4),TEXT("RareGoldOpening"));
    Prize->SetNumberField(TEXT("grade"),2);Prize->SetStringField(TEXT("name"),TEXT("잔불"));Prize->SetStringField(TEXT("itemId"),TEXT("banner_embers"));
    Capture(SNew(SRareShowcase).Prize(Prize).StartedAt(FPlatformTime::Seconds()-30),TEXT("RareSecondGradeProduct"));
    return true;
}
#endif
void UProjectCaseRecordWidget::BuildCollection(const TSharedPtr<FJsonObject>& State)
{
    auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();const auto C=Obj(State,TEXT("collection"));const bool Ready=R->IsAuthenticated();
    auto Header=SNew(SHorizontalBox);Header->AddSlot().FillWidth(1)[Text(TEXT("꾸미기 컬렉션"),24)];
    Header->AddSlot().AutoWidth()[Button(TEXT("시즌 뽑기"),[this]{Page=TEXT("Draw");RefreshPanel();},Ready)];Body->AddSlot().AutoHeight().Padding(4,8)[Header];
    auto Filters=SNew(SWrapBox).UseAllottedSize(true);
    for(const auto& Filter:TArray<TPair<FString,FString>>{{TEXT("전체"),TEXT("All")},{TEXT("배너"),TEXT("Banner")},{TEXT("도장·인장"),TEXT("Stamp")},{TEXT("명찰"),TEXT("Nameplate")},{TEXT("배지"),TEXT("Badge")},{TEXT("테두리"),TEXT("Frame")},{TEXT("아이콘"),TEXT("Icon")}})
    {
        auto Control=Button(Filter.Key,[this,Category=Filter.Value]{CollectionCategory=Category;CollectionPage=0;RenderedFingerprint.Reset();RefreshPanel();});Control->SetButtonStyle(&ProjectDFSDUI::ButtonStyle(CollectionCategory==Filter.Value));Control->SetContentPadding(FMargin(8,3));StaticCastSharedRef<STextBlock>(Control->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));Filters->AddSlot().Padding(2)[Control];
    }
    Body->AddSlot().AutoHeight().Padding(2,0,2,8)[Filters];
    auto Items=Rows(C,TEXT("catalogue"));TSet<FString> Known,Owned;for(auto V:Items)Known.Add(Str(V->AsObject(),TEXT("id")));for(auto V:Rows(State,TEXT("inventory")))Owned.Add(Str(V->AsObject(),TEXT("itemId")));
    for(auto V:Rows(State,TEXT("catalogue"))){auto Original=V->AsObject();const FString Id=Str(Original,TEXT("id"));if(Known.Contains(Id))continue;Known.Add(Id);auto Copy=MakeShared<FJsonObject>();Copy->Values=Original->Values;Copy->SetStringField(TEXT("origin"),TEXT("Shop"));Items.Add(MakeShared<FJsonValueObject>(Copy));}
    TArray<FString> OwnedIds=Owned.Array();OwnedIds.Sort();for(const auto& Id:OwnedIds)if(!Known.Contains(Id)&&(Id.StartsWith(TEXT("level_frame_"))||Id.StartsWith(TEXT("investigation_seal_")))){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),Id);J->SetStringField(TEXT("name"),ProjectCaseProfile::Name(Id));J->SetStringField(TEXT("slot"),TEXT("Frame"));Items.Add(MakeShared<FJsonValueObject>(J));}
    if(CollectionCategory!=TEXT("All"))Items.RemoveAll([this](const auto& V){return Str(V->AsObject(),TEXT("slot"))!=CollectionCategory;});
    TSet<FString> Displayed;for(auto V:Rows(C,TEXT("displayedBanners")))Displayed.Add(Str(V->AsObject(),TEXT("itemId")));
    constexpr int32 PerPage=3;const int32 Pages=FMath::Max(1,FMath::DivideAndRoundUp(Items.Num(),PerPage));CollectionPage=FMath::Clamp(CollectionPage,0,Pages-1);
    auto Grid=SNew(SUniformGridPanel).SlotPadding(6);
    for(int32 I=CollectionPage*PerPage;I<FMath::Min(Items.Num(),(CollectionPage+1)*PerPage);I++)
    {
        const auto J=Items[I]->AsObject();const FString Id=Str(J,TEXT("id")),CosmeticSlot=Str(J,TEXT("slot"));const bool Has=Owned.Contains(Id),Equipped=ProjectCasePresentation::Equipped(State,*CosmeticSlot)==Id;
        auto Card=SNew(SVerticalBox);Card->AddSlot().AutoHeight().Padding(0,5)[SNew(ProjectCaseProfile::SEmblem).ItemId(Id).Height(56)];Card->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(Str(J,TEXT("name")),15)];
        const FString Source=Str(J,TEXT("achievement"));Card->AddSlot().AutoHeight().Padding(6,8)[Text(Has?TEXT("보유 중"):!Source.IsEmpty()?ProjectCaseProfile::Achievement(Source):Str(J,TEXT("origin"))==TEXT("Shop")?TEXT("보관소 상점"):TEXT("시즌 뽑기"))];
        if(CosmeticSlot==TEXT("Banner"))
        {
            const bool Shown=Displayed.Contains(Id);auto Actions=SNew(SHorizontalBox);
            Actions->AddSlot().FillWidth(1).Padding(0,0,4,0)[Button(Shown?TEXT("전시 해제"):TEXT("전시"),[this,Id,Shown]{RequestCosmeticAction(Shown?TEXT("HideBanner"):TEXT("ShowBanner"),Id);},Ready&&Has)];
            Actions->AddSlot().FillWidth(1)[Button(Equipped?TEXT("대표"):TEXT("대표 지정"),[this,Id]{RequestCosmeticAction(TEXT("Equip"),Id);},Ready&&Has&&!Equipped)];Card->AddSlot().AutoHeight()[Actions];
        }
        else Card->AddSlot().AutoHeight()[Button(Equipped?TEXT("장착 중"):TEXT("장착"),[this,Id]{RequestCosmeticAction(TEXT("Equip"),Id);},Ready&&Has&&!Equipped)];
        Grid->AddSlot(I%PerPage,0)[SNew(SBorder).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(8)[Card]];
    }
    Body->AddSlot().AutoHeight()[Grid];auto Pager=SNew(SHorizontalBox);
    Pager->AddSlot().AutoWidth()[Button(TEXT("이전"),[this]{CollectionPage--;RenderedFingerprint.Reset();RefreshPanel();},CollectionPage>0)];
    Pager->AddSlot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("%d / %d"),CollectionPage+1,Pages))];
    Pager->AddSlot().AutoWidth()[Button(TEXT("다음"),[this]{CollectionPage++;RenderedFingerprint.Reset();RefreshPanel();},CollectionPage+1<Pages)];Body->AddSlot().AutoHeight().Padding(4,8)[Pager];
}
void UProjectCaseRecordWidget::RequestDrawAction(const FString& Operation,int32 Count)
{
    auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();if(!R||!R->IsAuthenticated()||R->IsWriting()||Count<1||Count>2000000)return;
    const auto State=R->Snapshot,D=Obj(Obj(State,TEXT("collection")),TEXT("draw"));
    if(Operation==TEXT("Draw")&&!Flag(D,TEXT("active")))
    {DrawFeedback=TEXT("시즌 뽑기 상품·확률이 아직 공개되지 않았습니다.");DrawFeedbackAt=FPlatformTime::Seconds();return;}
    if(Operation==TEXT("Draw")||Operation==TEXT("Exchange"))
    {ConfirmAction(Operation==TEXT("Draw")?TEXT("Draw"):TEXT("Purchase"),Operation==TEXT("Draw")?TEXT("계속 진행하시겠습니까?"):TEXT("구매하시겠습니까?"),[this,Operation,Count]{ExecuteDrawAction(Operation,Count);});return;}
    ExecuteDrawAction(Operation,Count);
}
void UProjectCaseRecordWidget::ExecuteDrawAction(const FString& Operation,int32 Count)
{
    auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();if(!R||!R->IsAuthenticated()||R->IsWriting())return;
    const auto State=R->Snapshot,D=Obj(Obj(State,TEXT("collection")),TEXT("draw"));
    if(Operation==TEXT("Draw")&&!Flag(D,TEXT("active")))
    {DrawFeedback=TEXT("시즌 뽑기 상품·확률이 아직 공개되지 않았습니다.");DrawFeedbackAt=FPlatformTime::Seconds();return;}
    if((Operation==TEXT("Draw")&&Num(D,TEXT("tickets"))<Count)||(Operation==TEXT("Exchange")&&Num(State,TEXT("credits"))<500.0*Count))
    {DrawFeedback=TEXT("재화가 부족합니다");DrawFeedbackAt=FPlatformTime::Seconds();return;}
    DrawFeedback.Reset();if(Operation==TEXT("Draw")){R->DrawResult.Reset();bDrawRequestedHere=true;}
    R->DrawAction(Operation,Count);
}
void UProjectCaseRecordWidget::OpenDrawParcel(int32 Index)
{
    auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();if(!bDrawResultOpen||!Rows(R->DrawResult,TEXT("awards")).IsValidIndex(Index)||ParcelOpenTimes.Contains(Index))return;
    const double Now=FPlatformTime::Seconds();ParcelOpenTimes.Add(Index,Now);
    auto Awards=AwardsFor(R);if(Num(Awards[Index]->AsObject(),TEXT("grade"))<=2){if(RareRevealQueue.IsEmpty())RareRevealBeganAt=Now;RareRevealQueue.Add(Index);}
    RenderedFingerprint.Reset();RefreshPanel();
}
void UProjectCaseRecordWidget::OpenAllDrawParcels()
{
    if(!bDrawResultOpen)return;auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();auto Awards=AwardsFor(R);const int32 Count=Awards.Num();const double Now=FPlatformTime::Seconds();
    for(int32 I=0;I<Count;I++)if(!ParcelOpenTimes.Contains(I))
    {ParcelOpenTimes.Add(I,Now);if(Num(Awards[I]->AsObject(),TEXT("grade"))<=2){if(RareRevealQueue.IsEmpty())RareRevealBeganAt=Now;RareRevealQueue.Add(I);}}
    RenderedFingerprint.Reset();RefreshPanel();
}
void UProjectCaseRecordWidget::FinishRarePresentation()
{
    if(RareRevealQueue.IsEmpty())return;RareRevealQueue.RemoveAt(0);RareRevealBeganAt=FPlatformTime::Seconds();RenderedFingerprint.Reset();RefreshPanel();
}
void UProjectCaseRecordWidget::BuildDraw(const TSharedPtr<FJsonObject>& State)
{
    auto* R=GetGameInstance()->GetSubsystem<UProjectCaseRecords>();const auto D=Obj(Obj(State,TEXT("collection")),TEXT("draw")),Pool=Obj(D,TEXT("pool"));
    if(Str(R->DrawResult,TEXT("operation"))==TEXT("EditorReset")&&LastDrawReceipt!=Str(R->DrawResult,TEXT("requestId")))
    {LastDrawReceipt=Str(R->DrawResult,TEXT("requestId"));bDrawRequestedHere=bDrawResultOpen=false;ParcelOpenTimes.Reset();RareRevealQueue.Reset();}
    const bool Ready=R->IsAuthenticated();
    const auto Update=[this]{RenderedFingerprint.Reset();RefreshPanel();};
    auto Shelf=SNew(SVerticalBox);auto Header=SNew(SHorizontalBox);Header->AddSlot().FillWidth(1)[Text(Pool?Str(Pool,TEXT("title")):TEXT("시즌 뽑기"),24)];
#if WITH_EDITOR
    if(GIsEditor){Header->AddSlot().AutoWidth().Padding(0,0,6,0)[Button(TEXT("+5,000 P"),[R]{R->Action(TEXT("DebugAddPoints"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));},Ready)];Header->AddSlot().AutoWidth().Padding(0,0,6,0)[Button(TEXT("획득 초기화"),[this]{RequestDrawAction(TEXT("EditorReset"),1);},Ready)];}
#endif
    Header->AddSlot().AutoWidth()[Button(TEXT("확률 확인"),[this,Update]{bShowDrawRates=true;Update();},Pool.IsValid())];Shelf->AddSlot().AutoHeight()[Header];
    Shelf->AddSlot().AutoHeight().Padding(0,5)[Text(FString::Printf(TEXT("뽑기 재화 %.0f  ·  기록 포인트 %.0f  ·  누적 뽑기 %.0f회"),Num(D,TEXT("tickets")),Num(State,TEXT("credits")),Num(D,TEXT("total"))))];
    if(!R->IsBusy()&&(R->Status.Contains(TEXT("재화가 부족"))||R->Status.Contains(TEXT("포인트가 부족")))){DrawFeedback=TEXT("재화가 부족합니다");DrawFeedbackAt=FPlatformTime::Seconds();}
    Shelf->AddSlot().AutoHeight()[SNew(SDrawFeedback).Message_Lambda([this]{return DrawFeedback;}).StartedAt_Lambda([this]{return DrawFeedbackAt;})];
    auto Prizes=Rows(Pool,TEXT("prizes"));Prizes.Sort([](const auto& A,const auto& B){return Num(A->AsObject(),TEXT("grade"))<Num(B->AsObject(),TEXT("grade"));});
    auto Exchange=SNew(SHorizontalBox);
    Exchange->AddSlot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("재화 1개 교환 · 500 P"),[this]{RequestDrawAction(TEXT("Exchange"),1);},Ready)];
    Exchange->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)[SNew(SBox).WidthOverride(68)[SNew(SSpinBox<int32>).Style(&ExchangeStyle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ContentPadding(FMargin(6,4)).MinValue(1).MaxValue(2000000).MinSliderValue(1).MaxSliderValue(100).Value_Lambda([this]{return DrawExchangeCount;}).OnValueChanged_Lambda([this](int32 N){DrawExchangeCount=N;})]];
    Exchange->AddSlot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("수량 교환"),[this]{RequestDrawAction(TEXT("Exchange"),DrawExchangeCount);},Ready)];
    Exchange->AddSlot().AutoWidth()[Button(TEXT("최대 교환"),[this,R]{RequestDrawAction(TEXT("Exchange"),FMath::Max(1,FMath::Min(2000000,int32(Num(R->Snapshot,TEXT("credits"))/500))));},Ready)];
    Shelf->AddSlot().AutoHeight().Padding(0,3)[Exchange];
    auto Display=SNew(SHorizontalBox);const int32 Order[]{3,1,0,2,4};const int32 Grades[]{2,1,3};
    for(int32 Position=0;Position<3;Position++)
    {
        const int32 Grade=Grades[Position];auto Group=Prizes.FilterByPredicate([Grade](const auto& P){return Num(P->AsObject(),TEXT("grade"))==Grade;});if(Group.IsEmpty())continue;
        int32& Selected=DrawShowcase.FindOrAdd(Grade);Selected=FMath::Clamp(Selected,0,Group.Num()-1);const auto J=Group[Selected]->AsObject();
        auto GradeCaption=Text(FString::Printf(TEXT("%d등급%s"),Grade,Flag(J,TEXT("owned"))&&Grade<=2?TEXT(" · 획득 완료"):TEXT("")),14);GradeCaption->SetColorAndOpacity(RarityColor(Grade-1));
        auto Browse=SNew(SHorizontalBox);Browse->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("%d / %d"),Selected+1,Group.Num()),13)];Browse->AddSlot().AutoWidth()[Button(TEXT("›"),[this,Grade,N=Group.Num(),Update]{DrawShowcase.FindOrAdd(Grade)=(DrawShowcase.FindRef(Grade)+1)%N;Update();},Group.Num()>1)];
        Display->AddSlot().FillWidth(Position==1?1.2f:1.f).Padding(5)[SNew(SBorder).BorderBackgroundColor(ProjectDFSDUI::Panel()).Padding(8)
            [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[GradeCaption]
             +SVerticalBox::Slot().AutoHeight()[SNew(ProjectCaseProfile::SEmblem).ItemId(Str(J,TEXT("itemId"))).Height(60)]
             +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(Str(J,TEXT("name")),15)]
             +SVerticalBox::Slot().AutoHeight()[Browse]]];
    }
    Shelf->AddSlot().AutoHeight().Padding(0,3)[Display];
    auto Controls=SNew(SHorizontalBox);
    Controls->AddSlot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("1개 뽑기"),[this]{RequestDrawAction(TEXT("Draw"),1);},Ready)];
    Controls->AddSlot().AutoWidth()[Button(TEXT("5개 뽑기"),[this]{RequestDrawAction(TEXT("Draw"),5);},Ready)];
    Controls->AddSlot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("50회 반천장 · 100회 천장  |  스택 %.0f / 100"),Num(D,TEXT("pity"))),13)];Shelf->AddSlot().AutoHeight().Padding(0,8)[Controls];
    auto Results=SNew(SVerticalBox);const auto Result=R->DrawResult;
    if(Str(Result,TEXT("operation"))==TEXT("Draw")&&(bDrawRequestedHere||bDrawResultOpen))
    {
        const FString Receipt=Str(Result,TEXT("requestId"));if(LastDrawReceipt!=Receipt){LastDrawReceipt=Receipt;ParcelOpenTimes.Reset();RareRevealQueue.Reset();bDrawResultOpen=true;}
        auto Awards=AwardsFor(R);auto Opened=SNew(SHorizontalBox);
        for(int32 Position=0;Position<5;Position++)
        {
            if(Awards.Num()==1&&Position!=2){Opened->AddSlot().FillWidth(1)[SNew(SBox)];continue;}
            const int32 I=Awards.Num()==5?Order[Position]:0;if(!Awards.IsValidIndex(I))continue;auto J=Awards[I]->AsObject();FString Caption=Str(J,TEXT("name"));if(Num(J,TEXT("credits"))>0)Caption=FString::Printf(TEXT("%s · %.0f P"),*Caption,Num(J,TEXT("credits")));
            const bool IsOpen=ParcelOpenTimes.Contains(I);
            Opened->AddSlot().FillWidth(1).Padding(4)[SNew(SButton).ButtonStyle(&ProjectDFSDUI::ButtonStyle()).ContentPadding(3).OnClicked_Lambda([this,I]{OpenDrawParcel(I);return FReply::Handled();})
                [SNew(SParcel).Start(IsOpen?ParcelOpenTimes[I]:0).StyleIndex(int32(Num(J,TEXT("grade")))-1).Caption(IsOpen?Caption:FString::Printf(TEXT("박스 %d"),Position+1))]];
        }Results->AddSlot().AutoHeight().Padding(0,8)[Text(TEXT("상품 개봉"),22)];Results->AddSlot().AutoHeight().Padding(0,8)[Opened];
        auto ResultActions=SNew(SHorizontalBox);
        if(Awards.Num()>1)ResultActions->AddSlot().FillWidth(1).Padding(0,0,8,0)[Button(TEXT("모두 개봉"),[this]{OpenAllDrawParcels();},ParcelOpenTimes.Num()<Awards.Num())];
        ResultActions->AddSlot().FillWidth(1)[Button(TEXT("확인"),[this,R,Update]{bDrawResultOpen=false;bDrawRequestedHere=false;RareRevealQueue.Reset();R->DrawResult.Reset();Update();})];Results->AddSlot().AutoHeight().Padding(0,12)[ResultActions];
    }
    auto Rates=SNew(SVerticalBox);Rates->AddSlot().AutoHeight().Padding(0,0,0,12)[Text(TEXT("추첨 확률"),22)];
    for(auto V:Prizes){auto J=V->AsObject();Rates->AddSlot().AutoHeight().Padding(4,5)[Text(FString::Printf(TEXT("%s  ·  %.4f%%"),*Str(J,TEXT("name")),Num(J,TEXT("probability"))))];}
    for(const auto& Guarantee:TArray<TPair<FString,FString>>{{TEXT("50뽑 · 1등급 50% / 2등급 50%"),TEXT("softGuaranteePrizes")},{TEXT("100뽑 · 1등급 확정"),TEXT("guaranteePrizes")}})
    {Rates->AddSlot().AutoHeight().Padding(0,8)[Text(Guarantee.Key,18)];for(auto V:Rows(Pool,*Guarantee.Value)){auto J=V->AsObject();Rates->AddSlot().AutoHeight()[Text(FString::Printf(TEXT("%s · %.4f%%"),*Str(J,TEXT("name")),Num(J,TEXT("probability"))),14)];}}
    Body->AddSlot().AutoHeight()[SNew(SOverlay)+SOverlay::Slot()[Shelf]+SOverlay::Slot()
        [SNew(SBorder).Visibility(bShowDrawRates?EVisibility::Visible:EVisibility::Collapsed).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(0,0,0,.8f)).HAlign(HAlign_Center).VAlign(VAlign_Center).OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})
            [SNew(SBox).WidthOverride(580).HeightOverride(220)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Canvas()).Padding(12)[SNew(SVerticalBox)+SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Rates]]+SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[Button(TEXT("닫기"),[this,Update]{bShowDrawRates=false;Update();})]]]]]
        +SOverlay::Slot()[SNew(SBorder).Visibility(bDrawResultOpen?EVisibility::Visible:EVisibility::Collapsed).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(ProjectDFSDUI::Canvas()).Padding(16).VAlign(VAlign_Center).OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})[Results]]];
    if(RarePresentationLayer)
    {
        RarePresentationLayer->ClearChildren();auto Awards=AwardsFor(R);
        if(bDrawResultOpen&&!RareRevealQueue.IsEmpty()&&Awards.IsValidIndex(RareRevealQueue[0]))RarePresentationLayer->AddSlot()
            [SNew(SRareShowcase).Prize(Awards[RareRevealQueue[0]]->AsObject()).StartedAt(RareRevealBeganAt).Finished([this]{FinishRarePresentation();}).Skipped([this](double Start){RareRevealBeganAt=Start;})];
    }
}
