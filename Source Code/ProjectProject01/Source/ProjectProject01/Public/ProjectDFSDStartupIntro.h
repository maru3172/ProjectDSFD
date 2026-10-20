#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProjectDFSDStartupIntro.generated.h"

/** Only advances from ticks/clicks. Hitch-limited so the logos cannot disappear during startup loading. */
struct PROJECTPROJECT01_API FProjectDFSDIntroTimeline
{
	int32 Stage = 0; // university, team, title reveal, finished
	float Elapsed = 0.0f;
	bool bSkipping = false;
	float SkipOpacity = 0.0f;
	static constexpr float FadeIn = 1.2f;
	static constexpr float Hold = 1.6f;
	static constexpr float FadeOut = 0.9f;
	static constexpr float Reveal = 1.5f;
	static constexpr float SkipFade = 0.25f;
	float Opacity() const;
	void Advance(float DeltaSeconds);
	void Skip();
};

UCLASS()
class PROJECTPROJECT01_API UProjectDFSDStartupIntro final : public UUserWidget
{
	GENERATED_BODY()
	friend class FProjectDFSDIntroRenderTest;

public:
	FSimpleDelegate OnFinished;

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
	void ApplyFrame();
	void CompleteIfFinished();
	FProjectDFSDIntroTimeline Timeline;
	UPROPERTY(Transient) TObjectPtr<class UScaleBox> LogoBox;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> TeamText;
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> LogoTexture;
};
