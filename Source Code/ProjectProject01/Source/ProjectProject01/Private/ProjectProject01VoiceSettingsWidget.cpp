#include "ProjectProject01VoiceSettingsWidget.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "InputCoreTypes.h"

UProjectProject01VoiceChatSubsystem* UProjectProject01VoiceSettingsWidget::Voice() const
{
	return GetGameInstance()?GetGameInstance()->GetSubsystem<UProjectProject01VoiceChatSubsystem>():nullptr;
}
bool UProjectProject01VoiceSettingsWidget::Initialize()
{
	if(!Super::Initialize())return false;SetIsFocusable(true);
	UBorder* Background=WidgetTree->ConstructWidget<UBorder>();WidgetTree->RootWidget=Background;
	Background->SetBrushColor(FLinearColor(0.12f,0.12f,0.14f,1));Background->SetHorizontalAlignment(HAlign_Center);Background->SetVerticalAlignment(VAlign_Center);
	USizeBox* Frame=WidgetTree->ConstructWidget<USizeBox>();Frame->SetWidthOverride(700);Background->AddChild(Frame);
	UVerticalBox* Panel=WidgetTree->ConstructWidget<UVerticalBox>();Frame->AddChild(Panel);
	auto Label=[&](const FString& Text){UTextBlock* Item=WidgetTree->ConstructWidget<UTextBlock>();Item->SetText(FText::FromString(Text));Item->SetColorAndOpacity(FLinearColor::White);Panel->AddChild(Item);return Item;};
	auto Button=[&](const FString& Text,FName Handler){UButton* Item=WidgetTree->ConstructWidget<UButton>();UTextBlock* Caption=WidgetTree->ConstructWidget<UTextBlock>();Caption->SetText(FText::FromString(Text));Caption->SetColorAndOpacity(FLinearColor::Black);Item->AddChild(Caption);Panel->AddChild(Item);FScriptDelegate Delegate;Delegate.BindUFunction(this,Handler);Item->OnClicked.Add(Delegate);};
	Label(TEXT("음성 채팅 설정"));Label(TEXT("입력 장치"));Inputs=WidgetTree->ConstructWidget<UComboBoxString>();Panel->AddChild(Inputs);
	Label(TEXT("출력 장치"));Outputs=WidgetTree->ConstructWidget<UComboBoxString>();Panel->AddChild(Outputs);
	Button(TEXT("장치 새로고침"),TEXT("RefreshDevices"));
	VolumeText=Label(TEXT("보이스 수신 음량 100%"));Volume=WidgetTree->ConstructWidget<USlider>();Volume->SetMinValue(0);Volume->SetMaxValue(1);Volume->SetStepSize(0.01f);Panel->AddChild(Volume);
	Volume->OnValueChanged.AddUniqueDynamic(this,&UProjectProject01VoiceSettingsWidget::VolumeChanged);
	Inputs->OnSelectionChanged.AddUniqueDynamic(this,&UProjectProject01VoiceSettingsWidget::InputChanged);
	Button(TEXT("마이크 테스트 시작 / 중지"),TEXT("ToggleMicrophoneTest"));Meter=WidgetTree->ConstructWidget<UProgressBar>();Panel->AddChild(Meter);
	Label(TEXT("테스트는 입력 게이지만 표시합니다. 녹음 파일을 만들거나 서버에 보내지 않습니다."));
	Label(TEXT("경기 중 동료 생존자 음소거"));Participants=WidgetTree->ConstructWidget<UComboBoxString>();Panel->AddChild(Participants);
	MuteText=Label(TEXT("참가한 생존자 음성 채널에서만 사용 가능합니다."));Button(TEXT("선택한 동료 음소거 / 해제"),TEXT("ToggleMute"));
	Status=Label(TEXT("장치 선택과 음량은 적용 버튼으로 저장합니다. 동료 음소거는 즉시 적용됩니다."));
	Button(TEXT("적용"),TEXT("Apply"));Button(TEXT("음성 설정 초기화"),TEXT("Reset"));Button(TEXT("돌아가기"),TEXT("Back"));
	return true;
}
void UProjectProject01VoiceSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();if(auto* Service=Voice()){Service->PrepareAudioDevices();Volume->SetValue(Service->GetReceiveVolume());}RefreshDevices();SetKeyboardFocus();
}
void UProjectProject01VoiceSettingsWidget::RefreshDevices()
{
	Inputs->ClearOptions();Outputs->ClearOptions();InputIds={FString()};OutputIds={FString()};
	Inputs->AddOption(TEXT("시스템 기본 마이크"));Outputs->AddOption(TEXT("시스템 기본 출력"));
	if(auto* Service=Voice())
	{
		Service->StopMicrophoneTest();
		if(!Service->PrepareAudioDevices())Status->SetText(FText::FromString(TEXT("음성 장치 초기화 중이거나 EOS를 사용할 수 없습니다. 기본 마이크 테스트는 독립적으로 사용할 수 있습니다.")));
		for(const auto& Device:Service->GetInputDevices()){InputIds.Add(Device.Id);Inputs->AddOption(FString::Printf(TEXT("%d. %s"),InputIds.Num()-1,*Device.Name));}
		for(const auto& Device:Service->GetOutputDevices()){OutputIds.Add(Device.Id);Outputs->AddOption(FString::Printf(TEXT("%d. %s"),OutputIds.Num()-1,*Device.Name));}
		Inputs->SetSelectedIndex(FMath::Max(0,InputIds.IndexOfByKey(Service->GetPreferredInput())));
		Outputs->SetSelectedIndex(FMath::Max(0,OutputIds.IndexOfByKey(Service->GetPreferredOutput())));
	}
}
void UProjectProject01VoiceSettingsWidget::NativeTick(const FGeometry& Geometry,float Delta)
{
	Super::NativeTick(Geometry,Delta);auto* Service=Voice();if(!Service)return;
	Meter->SetPercent(Service->GetMicrophoneLevel());PollSeconds+=Delta;
	if(PollSeconds<1)return;PollSeconds=0;
	TArray<FString> Current=Service->GetVoiceParticipants();
	if(Current!=ParticipantIds)
	{
		ParticipantIds=Current;Participants->ClearOptions();
		for(int32 Index=0;Index<ParticipantIds.Num();++Index)Participants->AddOption(FString::Printf(TEXT("동료 생존자 %d (%s)"),Index+1,*ParticipantIds[Index].Right(6)));
		if(ParticipantIds.Num())Participants->SetSelectedIndex(0);
	}
	int32 Index=Participants->GetSelectedIndex();
	MuteText->SetText(FText::FromString(ParticipantIds.IsValidIndex(Index)?(Service->IsParticipantMuted(ParticipantIds[Index])?TEXT("선택한 동료: 음소거됨"):TEXT("선택한 동료: 음소거 해제됨")):TEXT("현재 음성 채널에 동료가 없습니다.")));
}
void UProjectProject01VoiceSettingsWidget::Apply()
{
	if(auto* Service=Voice())
	{
		int32 I=Inputs->GetSelectedIndex(),O=Outputs->GetSelectedIndex();if(!InputIds.IsValidIndex(I)||!OutputIds.IsValidIndex(O))return;
		Service->SetAudioPreferences(InputIds[I],OutputIds[O],Volume->GetValue());Status->SetText(FText::FromString(TEXT("음성 설정을 저장하고 적용했습니다.")));
	}
}
void UProjectProject01VoiceSettingsWidget::Reset(){if(auto* Service=Voice())Service->SetAudioPreferences(TEXT(""),TEXT(""),1);Volume->SetValue(1);RefreshDevices();}
void UProjectProject01VoiceSettingsWidget::VolumeChanged(float Value){VolumeText->SetText(FText::FromString(FString::Printf(TEXT("보이스 수신 음량 %d%%"),FMath::RoundToInt(Value*100))));}
void UProjectProject01VoiceSettingsWidget::InputChanged(FString,ESelectInfo::Type){if(auto* Service=Voice())Service->StopMicrophoneTest();}
void UProjectProject01VoiceSettingsWidget::ToggleMicrophoneTest()
{
	if(auto* Service=Voice())
	{
		if(Service->IsMicrophoneTestActive()){Service->StopMicrophoneTest();Status->SetText(FText::FromString(TEXT("마이크 테스트를 중지했습니다.")));return;}
		int32 Index=Inputs->GetSelectedIndex();FString Error;
		bool Started=InputIds.IsValidIndex(Index)&&Service->StartMicrophoneTest(InputIds[Index],Error);
		Status->SetText(FText::FromString(Started?TEXT("마이크 입력 확인 중 · 상대에게 송신하지 않습니다."):Error));
	}
}
void UProjectProject01VoiceSettingsWidget::ToggleMute(){if(auto* Service=Voice()){int32 Index=Participants->GetSelectedIndex();if(ParticipantIds.IsValidIndex(Index))Service->SetParticipantMuted(ParticipantIds[Index],!Service->IsParticipantMuted(ParticipantIds[Index]));}}
void UProjectProject01VoiceSettingsWidget::NativeDestruct(){if(auto* Service=Voice())Service->StopMicrophoneTest();Super::NativeDestruct();}
void UProjectProject01VoiceSettingsWidget::Back(){if(auto* Service=Voice())Service->StopMicrophoneTest();RemoveFromParent();if(IsValid(ReturnWidget)){ReturnWidget->SetVisibility(ESlateVisibility::Visible);ReturnWidget->SetKeyboardFocus();}}
FReply UProjectProject01VoiceSettingsWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Key){if(Key.GetKey()==EKeys::Escape){Back();return FReply::Handled();}return Super::NativeOnKeyDown(Geometry,Key);}
