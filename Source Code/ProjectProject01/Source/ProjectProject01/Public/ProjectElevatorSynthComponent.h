#pragma once
#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "ProjectElevatorSynthComponent.generated.h"

/** 별도 음원 파일 없이 패키징되는 기본 운행음/도착 벨. 오디오 스레드는 UObject를 호출하지 않습니다. */
UCLASS()
class PROJECTPROJECT01_API UProjectElevatorSynthComponent final : public USynthComponent
{
	GENERATED_BODY()
	friend class FProjectElevatorAudioTest;
public:
	UProjectElevatorSynthComponent(const FObjectInitializer& Initializer);
	void SetMotor(bool bEnabled, float Gain = 1.0f);
	void Ding();
protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
private:
	int32 Rate = 48000;
	float MotorGain = 0, MotorTarget = 0;
	double MotorPhase = 0;
	int32 DingFrame = 0;
	bool bDingActive = false;
};
