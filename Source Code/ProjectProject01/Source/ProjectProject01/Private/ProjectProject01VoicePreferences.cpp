#include "ProjectProject01VoiceChatSubsystem.h"
#include "VoiceChat.h"
#include "AudioCaptureCore.h"
#include "Misc/ConfigCacheIni.h"
#include "ProjectProject01GameInstance.h"
#include "Framework/Application/SlateApplication.h"

bool UProjectProject01VoiceChatSubsystem::PrepareAudioDevices()
{
	if (IsRunningDedicatedServer()) return false;
	if (!VoiceChat) { ApplyRuntimeEosConfiguration(); VoiceChat = IVoiceChat::Get(); }
	if (!VoiceChat) return false;
	if (!VoiceChat->IsInitialized())
	{
		if (!bDeviceInitializationPending)
		{
			bDeviceInitializationPending=true;
			VoiceChat->Initialize(FOnVoiceChatInitializeCompleteDelegate::CreateUObject(this,&UProjectProject01VoiceChatSubsystem::HandleInitialized));
		}
		return false;
	}
	if (!VoiceUser) VoiceUser=VoiceChat->CreateUser();
	ApplyAudioPreferences();
	return VoiceUser != nullptr;
}

TArray<FProjectVoiceDevice> UProjectProject01VoiceChatSubsystem::GetInputDevices() const
{
	TArray<FProjectVoiceDevice> Result;
	if (VoiceUser) for(const auto& Device:VoiceUser->GetAvailableInputDeviceInfos()) Result.Add({Device.Id,Device.DisplayName});
	return Result;
}
TArray<FProjectVoiceDevice> UProjectProject01VoiceChatSubsystem::GetOutputDevices() const
{
	TArray<FProjectVoiceDevice> Result;
	if (VoiceUser) for(const auto& Device:VoiceUser->GetAvailableOutputDeviceInfos()) Result.Add({Device.Id,Device.DisplayName});
	return Result;
}
void UProjectProject01VoiceChatSubsystem::SetAudioPreferences(const FString& Input,const FString& Output,float Volume)
{
	StopMicrophoneTest();
	PreferredInput=Input;PreferredOutput=Output;ReceiveVolume=FMath::IsFinite(Volume)?FMath::Clamp(Volume,0.0f,1.0f):1.0f;
	ApplyAudioPreferences();
	GConfig->SetString(TEXT("ProjectProject01.VoicePreferences"),TEXT("Input"),*PreferredInput,GGameUserSettingsIni);
	GConfig->SetString(TEXT("ProjectProject01.VoicePreferences"),TEXT("Output"),*PreferredOutput,GGameUserSettingsIni);
	GConfig->SetFloat(TEXT("ProjectProject01.VoicePreferences"),TEXT("Volume"),ReceiveVolume,GGameUserSettingsIni);
	GConfig->Flush(false,GGameUserSettingsIni);
}
void UProjectProject01VoiceChatSubsystem::ApplyAudioPreferences()
{
	if (!VoiceUser) return;
	const bool bHasInput=PreferredInput.IsEmpty()||GetInputDevices().ContainsByPredicate([this](const auto& Device){return Device.Id==PreferredInput;});
	const bool bHasOutput=PreferredOutput.IsEmpty()||GetOutputDevices().ContainsByPredicate([this](const auto& Device){return Device.Id==PreferredOutput;});
	VoiceUser->SetInputDeviceId(bHasInput?PreferredInput:FString());
	VoiceUser->SetOutputDeviceId(bHasOutput?PreferredOutput:FString());
	AppliedReceiveVolume=-1.0f;
	UpdateReceiveVolume();
	for(const FString& Id:MutedParticipants)VoiceUser->SetPlayerMuted(Id,true);
}
void UProjectProject01VoiceChatSubsystem::UpdateReceiveVolume()
{
	if(!VoiceUser)return;
	float Gain=ReceiveVolume;
	if(const auto* Settings=UProjectProject01GameUserSettings::Get())
	{
		const bool bInactive=FSlateApplication::IsInitialized()&&!FSlateApplication::Get().IsActive();
		Gain*=Settings->IsMuteAllEnabled()||(Settings->IsMuteWhenUnfocusedEnabled()&&bInactive)?0.0f:Settings->GetMasterVolume();
	}
	if(!FMath::IsNearlyEqual(AppliedReceiveVolume,Gain)){VoiceUser->SetAudioOutputVolume(Gain);AppliedReceiveVolume=Gain;}
}
TArray<FString> UProjectProject01VoiceChatSubsystem::GetVoiceParticipants() const
{
	TArray<FString> Participants=bConfiguredForSurvivor && bJoinedChannel && VoiceUser ? VoiceUser->GetPlayersInChannel(ChannelName) : TArray<FString>();
	Participants.Remove(LocalUserId);return Participants;
}
void UProjectProject01VoiceChatSubsystem::SetParticipantMuted(const FString& Id,bool bMuted)
{
	if (!VoiceUser || Id==LocalUserId || !GetVoiceParticipants().Contains(Id)) return;
	if(bMuted) MutedParticipants.Add(Id);else MutedParticipants.Remove(Id);
	VoiceUser->SetPlayerMuted(Id,bMuted);
}
bool UProjectProject01VoiceChatSubsystem::StartMicrophoneTest(const FString& InputId,FString& Error)
{
	StopMicrophoneTest();
	auto Capture=MakeShared<Audio::FAudioCapture>();
	Audio::FAudioCaptureDeviceParams Params;
	if (!InputId.IsEmpty())
	{
		FString Name;
		for(const auto& Device:GetInputDevices())if(Device.Id==InputId)Name=Device.Name;
		TArray<Audio::FCaptureDeviceInfo> Devices;Capture->GetCaptureDevicesAvailable(Devices);
		Params.DeviceIndex=Devices.IndexOfByPredicate([&](const auto& Device){return Device.DeviceId==InputId||(!Name.IsEmpty()&&Device.DeviceName==Name);});
		if(Params.DeviceIndex==INDEX_NONE){Error=TEXT("선택한 마이크를 찾지 못했습니다. 장치를 새로고침하거나 시스템 기본값을 선택하세요.");return false;}
	}
	const auto Level=MicrophoneLevel;
	const bool bOpened=Capture->OpenAudioCaptureStream(Params,[Level](const void* Data,int32 Frames,int32 Channels,int32,double,bool)
	{
		float Peak=0.0f;const float* Samples=static_cast<const float*>(Data);
		if(Samples)for(int32 Index=0;Index<Frames*Channels;++Index)Peak=FMath::Max(Peak,FMath::Abs(Samples[Index]));
		Level->Store(FMath::Clamp(Peak,0.0f,1.0f));
	},1024);
	if(!bOpened||!Capture->StartStream()){Capture->AbortStream();Error=TEXT("마이크를 열지 못했습니다. Windows 마이크 권한과 장치 사용 상태를 확인하세요.");return false;}
	MicrophoneCapture=Capture;ApplyTransmitMode();return true;
}
void UProjectProject01VoiceChatSubsystem::StopMicrophoneTest()
{
	if(MicrophoneCapture){MicrophoneCapture->StopStream();MicrophoneCapture->CloseStream();MicrophoneCapture.Reset();}
	MicrophoneLevel->Store(0.0f);ApplyTransmitMode();
}
