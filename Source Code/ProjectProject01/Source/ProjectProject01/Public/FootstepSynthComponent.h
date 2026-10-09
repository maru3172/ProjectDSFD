// File: Source/ProjectProject01/Public/FootstepSynthComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "FootstepSynthComponent.generated.h"

UENUM(BlueprintType)
enum class EProjectProject01FootstepStyle : uint8
{
	MarblePlastic UMETA(DisplayName = "Marble / Plastic"),
	MarbleSneaker UMETA(DisplayName = "Marble / Sneaker")
};

// 사운드 관련 클래스 전방 선언
class USoundBase;
class USoundAttenuation;

/**
 * 외부 음원 없이 대리석 위 플라스틱·운동화 발소리를 만드는 경량 공간 음향 컴포넌트입니다.
 * 애니메이션 Notify가 있는 캐릭터는 TriggerFootstep을 직접 호출하고, 그렇지 않은 캐릭터는
 * 이동 거리를 기준으로 자동 발걸음을 생성할 수 있습니다.
 */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class PROJECTPROJECT01_API UFootstepSynthComponent final : public USynthComponent
{
	GENERATED_BODY()

public:
	UFootstepSynthComponent(const FObjectInitializer& ObjectInitializer);

	void Configure(EProjectProject01FootstepStyle NewStyle, float NewVolumeScale, bool bNewAutomaticCadence);
	void UpdateFootsteps(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category="Audio|Footstep")
	void TriggerFootstep(bool bRunning);

	/** 달리기 직후 급제동했을 때 운동화 밑창이 대리석을 짧게 끄는 소리를 재생합니다. */
	UFUNCTION(BlueprintCallable, Category="Audio|Footstep")
	void TriggerSkidStop();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	void UpdateAutomaticCadence(float DeltaTime);
	bool CanProduceLocalAudio() const;

	UPROPERTY(EditAnywhere, Category="Footstep")
	EProjectProject01FootstepStyle FootstepStyle = EProjectProject01FootstepStyle::MarbleSneaker;

	UPROPERTY(EditAnywhere, Category="Footstep", meta=(ClampMin="0.0"))
	float FootstepVolumeScale = 0.85f;

	UPROPERTY(EditAnywhere, Category="Footstep")
	bool bAutomaticCadence = true;

	UPROPERTY(EditAnywhere, Category="Footstep|Cadence", meta=(ClampMin="1.0", Units="cm"))
	float WalkStepDistance = 300.0f;

	UPROPERTY(EditAnywhere, Category="Footstep|Cadence", meta=(ClampMin="1.0", Units="cm"))
	float RunStepDistance = 350.0f;

	UPROPERTY(EditAnywhere, Category="Footstep|Cadence", meta=(ClampMin="0.0", Units="cm/s"))
	float RunningSpeedThreshold = 700.0f;

	UPROPERTY(EditAnywhere, Category="Footstep|Cadence", meta=(ClampMin="0.0", Units="cm/s"))
	float MinimumMovingSpeed = 20.0f;

	UPROPERTY(EditAnywhere, Category="Footstep|Cadence", meta=(ClampMin="0.0", Units="s"))
	float MinimumTriggerInterval = 0.15f;

	UPROPERTY(EditAnywhere, Category="Footstep|Skid", meta=(ClampMin="0.0", Units="cm/s"))
	float MinimumSkidSpeedLossPerSecond = 250.0f;

	FVector PreviousOwnerLocation = FVector::ZeroVector;
	float AccumulatedTravelDistance = 0.0f;
	double LastTriggerTimeSeconds = -1.0;
	float PreviousHorizontalSpeed = 0.0f;
	bool bSkidArmed = false;
	bool bLocationInitialized = false;

	// 아래 상태는 SynthCommand를 통해서만 오디오 스레드에서 변경합니다.
	int32 SynthSampleRate = 48000;
	int32 PulseFrameIndex = 0;
	uint32 NoiseState = 0x12345678u;
	float PulsePhaseLow = 0.0f;
	float PulsePhaseHigh = 0.0f;
	float FilteredNoise = 0.0f;
	float FilteredNoiseSlow = 0.0f;
	float PulsePitch = 1.0f;
	float PulseGain = 0.0f;
	bool bPulseRunning = false;
	bool bSkidPulse = false;
	bool bPulseActive = false;
	EProjectProject01FootstepStyle ActiveStyle = EProjectProject01FootstepStyle::MarbleSneaker;
	
	// 관련 변수
	UPROPERTY(EditAnywhere, Category = "Footstep|Sounds")
	TArray<TObjectPtr<USoundBase>> WalkSounds;
	
	UPROPERTY(EditAnywhere, Category = "Footstep|Sounds")
	TArray<TObjectPtr<USoundBase>> RunSounds;

	UPROPERTY(EditAnywhere, Category = "Footstep|Sounds")
	TArray<TObjectPtr<USoundBase>> SkidSounds;

	UPROPERTY(EditAnywhere, Category = "Footstep|Sounds")
	TObjectPtr<USoundAttenuation> SoundAttenuation;
	
	// 관련 함수
	bool PlaySoundAsset(bool bRunning);
	USoundBase* SelectRandomSound(const TArray<TObjectPtr<USoundBase>>& Sounds) const;
};
