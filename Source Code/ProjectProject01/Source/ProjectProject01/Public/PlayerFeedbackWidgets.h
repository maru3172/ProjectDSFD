#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraModifier.h"
#include "PlayerFeedbackWidgets.generated.h"

/** 마네킹 플레이어의 포획 성공 순간에만 최종 카메라에 짧은 좌우 진동을 더합니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01MannequinCaptureCameraModifier final : public UCameraModifier
{
	GENERATED_BODY()

public:
	void PlayCaptureShake(float InDurationSeconds, float InYawAmplitudeDegrees);
	virtual bool ModifyCamera(float DeltaTime, struct FMinimalViewInfo& InOutPOV) override;

private:
	float ElapsedSeconds = 0.0f;
	float DurationSeconds = 0.2f;
	float YawAmplitudeDegrees = 4.0f;
	bool bPlaying = false;
};

/** 데스카운트가 실제로 차감됐을 때만 표시하는 로컬 화면 테두리입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01DamageFeedbackWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectDamageFeedbackRenderTest;

public:
	/** The server's post-hit count snapshot determines severity; first of several hits keeps the base look. */
	static float CalculateDamageSeverity(int32 RemainingDeathCount, int32 StartingDeathCount);
	void Configure(float InIntroSeconds, float InFadeSeconds, float InMaxOpacity, float InMaxThicknessPixels,
		float DamageSeverity = 0.0f);

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

/** 마네킹 플레이어에게만 보이는 짧은 적색 비산 포획 피드백입니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectProject01MannequinCaptureFeedbackWidget final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectMannequinCaptureFeedbackRenderTest;

public:
	void Configure(bool bInElimination, float InIntensityScale = 1.0f);

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

	float DurationSeconds = 0.72f;
	float ElapsedSeconds = 0.0f;
	float IntensityScale = 1.0f;
	// New pattern per confirmed hit, stable during painting (no frame-to-frame random flicker).
	int32 PatternSeed = 0;
	bool bElimination = false;
};
