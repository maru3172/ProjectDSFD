#include "ProjectMailboxWidget.h"
#include "ProjectUIRenderCapture.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
// Docs/MailAndCoupons.md: the mailbox window fits both test screens with long mail, rewards and claimed mail.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectMailboxWidgetTest, "ProjectProject01.Mailbox.Render", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectMailboxWidgetTest::RunTest(const FString&)
{
    const FString Json = LR"J({"unclaimed":2,"mail":[
        {"id":3,"title":"점검 보상 · 11월 12일 서버 점검으로 불편을 드려 죄송합니다 (아주 긴 제목이 잘리는지 확인)","body":"점검 시간 동안 접속하지 못한 모든 분께 드립니다.","credits":300,"tickets":2,"items":[{"id":"frame_crimson","name":"붉은 봉인 테두리"}],"expiresUtc":"2099-01-01T00:00:00Z","claimed":false},
        {"id":2,"title":"쿠폰 보상","body":"","credits":50,"tickets":0,"items":[],"expiresUtc":null,"claimed":false},
        {"id":1,"title":"이벤트 참여 감사","body":"이벤트에 참여해 주셔서 감사합니다.","credits":0,"tickets":1,"items":[],"expiresUtc":"2099-01-01T00:00:00Z","claimed":true}]})J";
    TSharedPtr<FJsonObject> Box; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Box);
    auto* W = NewObject<UProjectMailboxWidget>();
    static_cast<UUserWidget*>(W)->Initialize();
    W->ShowForTest(Box);
    const TSharedRef<SWidget> Slate = W->TakeWidget();
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("CaseRecordUIQA"); IFileManager::Get().MakeDirectory(*Directory, true);
    FWidgetRenderer Renderer(false);
    for (const FIntPoint Size : {FIntPoint(1280, 720), FIntPoint(800, 600)})
    {
        const FString Label = FString::Printf(TEXT("[%dx%d] "), Size.X, Size.Y);
        const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
        auto* Target = ProjectUIRenderCapture::Create(Size);
        for (int32 Pass = 0; Pass < 4; ++Pass) Renderer.DrawWidget(Target, Slate, Dpi, FVector2D(Size), .016f);
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        if (TestTrue(Label + TEXT("Mailbox renders"), ProjectUIRenderCapture::Read(Target, Pixels)))
        {
            TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
            FFileHelper::SaveArrayToFile(Png, *(Directory / FString::Printf(TEXT("Mailbox_%dx%d.png"), Size.X, Size.Y)));
        }
        Target->ReleaseResource();
        bool bInside = true; int32 Texts = 0;
        TFunction<void(TSharedRef<SWidget>)> Visit = [&](TSharedRef<SWidget> Node)
        {
            if (Node->GetType() == FName(TEXT("STextBlock")) && !StaticCastSharedRef<STextBlock>(Node)->GetText().IsEmpty())
            {
                const FGeometry& G = Node->GetCachedGeometry(); const FVector2D P = G.GetAbsolutePosition(), S = G.GetAbsoluteSize(); ++Texts;
                bInside &= P.X >= 0 && P.Y >= 0 && P.X + S.X <= Size.X + 1 && P.Y + S.Y <= Size.Y + 1;
            }
            if (FChildren* C = Node->GetChildren()) for (int32 I = 0; I < C->Num(); ++I) Visit(C->GetChildAt(I));
        };
        Visit(Slate);
        TestTrue(Label + TEXT("Every text stays on screen"), bInside && Texts > 10);
    }
    return true;
}
#endif
