#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerFeedbackWidgets.generated.h"

/** 데스카운트가 실제로 차감됐을 때만 표시하는 로컬 화면 테두리입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01DamageFeedbackWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(float InIntroSeconds, float InFadeSeconds, float InMaxOpacity, float InMaxThicknessPixels);

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	void BuildWidgetTree();
	void ApplyFrame(float ThicknessPixels, float Opacity);

	float IntroSeconds = 0.1f;
	float FadeSeconds = 2.0f;
	float MaxOpacity = 0.7f;
	float MaxThicknessPixels = 150.0f;
	float ElapsedSeconds = 0.0f;
	float CurrentThicknessPixels = 0.0f;
	float CurrentOpacity = 0.0f;
};

/** 사망 카메라가 바닥에 쓰러진 뒤 암전과 사망 문구를 표시합니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01DeathFeedbackWidget final : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(float InCameraSeconds, float InBlackFadeSeconds);

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildWidgetTree();

	UPROPERTY(Transient)
	TObjectPtr<class UBorder> BlackBackground;

	float CameraSeconds = 1.06f;
	float BlackFadeSeconds = 0.65f;
	float ElapsedSeconds = 0.0f;
};
