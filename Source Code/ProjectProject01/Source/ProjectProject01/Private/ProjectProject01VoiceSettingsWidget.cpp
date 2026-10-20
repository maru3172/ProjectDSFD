#include "ProjectProject01VoiceSettingsWidget.h"
#include "ProjectDFSDUITheme.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
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
	Background->SetPadding(FMargin(24));
	ContentFrame=WidgetTree->ConstructWidget<USizeBox>();ContentFrame->SetWidthOverride(700);ContentFrame->SetMaxDesiredHeight(760);Background->AddChild(ContentFrame);
	UScrollBox* Scroll=WidgetTree->ConstructWidget<UScrollBox>();ContentFrame->AddChild(Scroll);
	UVerticalBox* Panel=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Panel);
	auto Label=[&](const FString& Text){UTextBlock* Item=WidgetTree->ConstructWidget<UTextBlock>();Item->SetText(FText::FromString(Text));Item->SetColorAndOpacity(FLinearColor::White);Item->SetAutoWrapText(true);FSlateFontInfo Font=Item->GetFont();Font.Size=18;Item->SetFont(Font);Panel->AddChild(Item);return Item;};
	auto Button=[&](const FString& Text,FName Handler){UButton* Item=WidgetTree->ConstructWidget<UButton>();UTextBlock* Caption=WidgetTree->ConstructWidget<UTextBlock>();Caption->SetText(FText::FromString(Text));Caption->SetColorAndOpacity(ProjectDFSDUI::Text());FSlateFontInfo Font=Caption->GetFont();Font.Size=18;Caption->SetFont(Font);Item->AddChild(Caption);Panel->AddChild(Item);FScriptDelegate Delegate;Delegate.BindUFunction(this,Handler);Item->OnClicked.Add(Delegate);return Item;};
	Label(TEXT("음성 채팅 설정"));Label(TEXT("입력 장치"));Inputs=WidgetTree->ConstructWidget<UProjectDFSDComboBox>();Panel->AddChild(Inputs);
	Label(TEXT("출력 장치"));Outputs=WidgetTree->ConstructWidget<UProjectDFSDComboBox>();Panel->AddChild(Outputs);
	Button(TEXT("장치 새로고침"),TEXT("RefreshDevices"));
	VolumeText=Label(TEXT("보이스 수신 음량 100%"));Volume=WidgetTree->ConstructWidget<USlider>();Volume->SetMinValue(0);Volume->SetMaxValue(1);Volume->SetStepSize(0.01f);Volume->SetValue(1);Panel->AddChild(Volume);
	Volume->OnValueChanged.AddUniqueDynamic(this,&UProjectProject01VoiceSettingsWidget::VolumeChanged);
	Inputs->OnSelectionChanged.AddUniqueDynamic(this,&UProjectProject01VoiceSettingsWidget::InputChanged);
	Button(TEXT("마이크 테스트 시작 / 중지"),TEXT("ToggleMicrophoneTest"));Meter=WidgetTree->ConstructWidget<UProgressBar>();Panel->AddChild(Meter);
	Label(TEXT("테스트는 입력 게이지만 표시합니다. 녹음 파일을 만들거나 서버에 보내지 않습니다."));
	Label(TEXT("경기 중 동료 생존자 음성"));Participants=WidgetTree->ConstructWidget<UProjectDFSDComboBox>();Panel->AddChild(Participants);
	Participants->OnSelectionChanged.AddUniqueDynamic(this,&UProjectProject01VoiceSettingsWidget::ParticipantChanged);
	MuteText=Label(TEXT("참가한 생존자 음성 채널에서만 사용 가능합니다."));ParticipantMuteButton=Button(TEXT("선택한 동료 음소거 / 해제"),TEXT("ToggleMute"));
	ParticipantVolumeText=Label(TEXT("선택한 동료 음량 100%"));ParticipantVolume=WidgetTree->ConstructWidget<USlider>();ParticipantVolume->SetMinValue(0);ParticipantVolume->SetMaxValue(1);ParticipantVolume->SetStepSize(0.01f);ParticipantVolume->SetValue(1);Panel->AddChild(ParticipantVolume);
	ParticipantVolume->OnValueChanged.AddUniqueDynamic(this,&UProjectProject01VoiceSettingsWidget::ParticipantVolumeChanged);
	Label(TEXT("동료 음소거·음량은 내 수신에만 즉시 적용되며 상대에게 알리지 않습니다. 같은 경기의 맵 이동에는 유지되고, 경기 종료·로그아웃 때 초기화됩니다."));
	ConnectionText=Label(TEXT("경기 중 생존자 전용"));RetryButton=Button(TEXT("음성 연결 다시 시도"),TEXT("RetryConnection"));RetryButton->SetIsEnabled(false);
	Status=Label(TEXT("장치 선택과 전체 수신 음량은 적용 버튼으로 저장합니다."));
	Button(TEXT("적용"),TEXT("Apply"));Button(TEXT("음성 설정 초기화"),TEXT("Reset"));Button(TEXT("돌아가기"),TEXT("Back"));
	RefreshSelectedParticipant();
	ProjectDFSDUI::ApplyTree(WidgetTree);
	return true;
}
void UProjectProject01VoiceSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();if(auto* Service=Voice()){Service->PrepareAudioDevices();Volume->SetValue(Service->GetReceiveVolume());VolumeChanged(Service->GetReceiveVolume());}RefreshDevices();RefreshParticipants();SetKeyboardFocus();
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
	Super::NativeTick(Geometry,Delta);
	if(ContentFrame && Geometry.GetLocalSize().Y>0)
	{
		ContentFrame->SetMaxDesiredHeight(FMath::Clamp(Geometry.GetLocalSize().Y-48.0f,220.0f,760.0f));
		ContentFrame->SetWidthOverride(FMath::Clamp(Geometry.GetLocalSize().X-48.0f,220.0f,700.0f));
	}
	auto* Service=Voice();if(!Service)return;
	Meter->SetPercent(Service->GetMicrophoneLevel());PollSeconds+=Delta;
	if(PollSeconds<0.2f)return;PollSeconds=0;
	RefreshParticipants();
}
void UProjectProject01VoiceSettingsWidget::RefreshParticipants()
{
	auto* Service=Voice();
	TArray<FString> Current=Service?Service->GetVoiceParticipants():TArray<FString>();
	TArray<FString> Labels;for(const FString& Id:Current)Labels.Add(Service->GetParticipantDisplayName(Id));
	if(Current!=ParticipantIds || Labels!=ParticipantLabels)
	{
		const int32 OldIndex=Participants->GetSelectedIndex();
		const FString Selected=ParticipantIds.IsValidIndex(OldIndex)?ParticipantIds[OldIndex]:FString();
		bRefreshingParticipants=true;
		ParticipantIds=Current;ParticipantLabels=Labels;Participants->ClearOptions();
		for(const FString& Label:Labels)Participants->AddOption(Label);
		if(ParticipantIds.Num())Participants->SetSelectedIndex(FMath::Max(0,ParticipantIds.IndexOfByKey(Selected)));
		bRefreshingParticipants=false;
		ParticipantChanged(FString(),ESelectInfo::Direct);
	}
	RefreshSelectedParticipant();
	ConnectionText->SetText(FText::FromString(Service?Service->GetConnectionStatusText():TEXT("경기 중 생존자 전용")));
	RetryButton->SetIsEnabled(Service && Service->CanRetryVoiceConnection());
}
void UProjectProject01VoiceSettingsWidget::RefreshSelectedParticipant()
{
	const int32 Index=Participants->GetSelectedIndex();auto* Service=Voice();
	const bool bSelected=Service && ParticipantIds.IsValidIndex(Index);
	Participants->SetIsEnabled(ParticipantIds.Num()>0);
	ParticipantMuteButton->SetIsEnabled(bSelected);ParticipantVolume->SetIsEnabled(bSelected);
	FString Text=TEXT("현재 음성 채널에 동료가 없습니다.");
	if(bSelected)
	{
		for(const auto& State:Service->GetVoiceParticipantStates())if(State.Id==ParticipantIds[Index])Text=TEXT("선택한 동료: ")+State.GetStatusLabel();
	}
	MuteText->SetText(FText::FromString(Text));
}
void UProjectProject01VoiceSettingsWidget::ParticipantChanged(FString,ESelectInfo::Type)
{
	if(bRefreshingParticipants)return;
	const int32 Index=Participants->GetSelectedIndex();auto* Service=Voice();
	const float Value=Service && ParticipantIds.IsValidIndex(Index)?Service->GetParticipantVolume(ParticipantIds[Index]):1.0f;
	ParticipantVolume->SetValue(Value);
	ParticipantVolumeText->SetText(FText::FromString(FString::Printf(TEXT("선택한 동료 음량 %d%%"),FMath::RoundToInt(Value*100))));
	RefreshSelectedParticipant();
}
void UProjectProject01VoiceSettingsWidget::ParticipantVolumeChanged(float Value)
{
	if(bRefreshingParticipants)return;
	auto* Service=Voice();const int32 Index=Participants->GetSelectedIndex();
	if(!Service || !ParticipantIds.IsValidIndex(Index))return;
	Service->SetParticipantVolume(ParticipantIds[Index],Value);
	ParticipantVolumeText->SetText(FText::FromString(FString::Printf(TEXT("선택한 동료 음량 %d%%"),FMath::RoundToInt(Service->GetParticipantVolume(ParticipantIds[Index])*100))));
}
void UProjectProject01VoiceSettingsWidget::Apply()
{
	if(auto* Service=Voice())
	{
		int32 I=Inputs->GetSelectedIndex(),O=Outputs->GetSelectedIndex();if(!InputIds.IsValidIndex(I)||!OutputIds.IsValidIndex(O))return;
		Service->SetAudioPreferences(InputIds[I],OutputIds[O],Volume->GetValue());Status->SetText(FText::FromString(TEXT("음성 설정을 저장하고 적용했습니다.")));
	}
}
void UProjectProject01VoiceSettingsWidget::Reset(){if(auto* Service=Voice()){Service->SetAudioPreferences(TEXT(""),TEXT(""),1);Service->ResetParticipantPreferences();}Volume->SetValue(1);VolumeChanged(1);RefreshDevices();ParticipantChanged(FString(),ESelectInfo::Direct);}
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
void UProjectProject01VoiceSettingsWidget::ToggleMute(){if(auto* Service=Voice()){int32 Index=Participants->GetSelectedIndex();if(ParticipantIds.IsValidIndex(Index))Service->SetParticipantMuted(ParticipantIds[Index],!Service->IsParticipantMuted(ParticipantIds[Index]));}RefreshSelectedParticipant();}
void UProjectProject01VoiceSettingsWidget::RetryConnection(){if(auto* Service=Voice())Service->RetryVoiceConnection();RefreshParticipants();}
void UProjectProject01VoiceSettingsWidget::NativeDestruct(){if(auto* Service=Voice())Service->StopMicrophoneTest();Super::NativeDestruct();}
void UProjectProject01VoiceSettingsWidget::Back(){if(auto* Service=Voice())Service->StopMicrophoneTest();RemoveFromParent();if(IsValid(ReturnWidget)){ReturnWidget->SetVisibility(ESlateVisibility::Visible);ReturnWidget->SetKeyboardFocus();}}
FReply UProjectProject01VoiceSettingsWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Key){if(Key.GetKey()==EKeys::Escape){Back();return FReply::Handled();}return Super::NativeOnKeyDown(Geometry,Key);}
