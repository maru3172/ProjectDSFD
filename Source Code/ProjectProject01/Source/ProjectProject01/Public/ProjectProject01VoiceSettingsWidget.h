#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "ProjectProject01VoiceSettingsWidget.generated.h"

UCLASS()
class PROJECTPROJECT01_API UProjectProject01VoiceSettingsWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	void ConfigureReturn(UUserWidget* Widget) { ReturnWidget=Widget; }
protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry&,float DeltaSeconds) override;
	virtual FReply NativeOnKeyDown(const FGeometry&,const FKeyEvent&) override;
private:
	UFUNCTION() void RefreshDevices();
	UFUNCTION() void Apply();
	UFUNCTION() void Reset();
	UFUNCTION() void ToggleMicrophoneTest();
	UFUNCTION() void ToggleMute();
	UFUNCTION() void Back();
	UFUNCTION() void VolumeChanged(float Value);
	UFUNCTION() void InputChanged(FString Value,ESelectInfo::Type Type);
	class UProjectProject01VoiceChatSubsystem* Voice() const;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ReturnWidget;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> Inputs;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> Outputs;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> Participants;
	UPROPERTY(Transient) TObjectPtr<class USlider> Volume;
	UPROPERTY(Transient) TObjectPtr<class UProgressBar> Meter;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> VolumeText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> MuteText;
	TArray<FString> InputIds,OutputIds,ParticipantIds;
	float PollSeconds=0;
};
