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
	for(const FString& Id:GetVoiceParticipants()) HandleParticipantAdded(ChannelName,Id);
}
void UProjectProject01VoiceChatSubsystem::UpdateReceiveVolume()
{
	if(!VoiceUser)return;
	float Gain=HasFreshAudibility() && bAuthoritativeVoiceAllowed ? ReceiveVolume : 0.0f;
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
	Participants.Remove(LocalUserId);Participants.Remove(FString());Participants.Sort();return Participants;
}
TArray<FProjectVoiceParticipantState> UProjectProject01VoiceChatSubsystem::GetVoiceParticipantStates() const
{
	TArray<FProjectVoiceParticipantState> Result;
	// Both settings and HUD use the same survivor-channel membership gate. No game RPC is involved.
	for (const FString& Id : GetVoiceParticipants())
	{
		FProjectVoiceParticipantState State;
		State.Id=Id;
		State.DisplayName=GetParticipantDisplayName(Id);
		State.Volume=GetParticipantVolume(Id);
		State.bMuted=IsParticipantMuted(Id);
		State.bTalking=VoiceUser->IsPlayerTalking(Id);
		Result.Add(MoveTemp(State));
	}
	return Result;
}
void UProjectProject01VoiceChatSubsystem::SetParticipantMuted(const FString& Id,bool bMuted)
{
	if (!VoiceUser || !ParticipantControls.SetMuted(Id,LocalUserId,GetVoiceParticipants(),bMuted)) return;
	VoiceUser->SetPlayerMuted(Id,bMuted);
}
void UProjectProject01VoiceChatSubsystem::SetParticipantVolume(const FString& Id,float Volume)
{
	if (!VoiceUser || !ParticipantControls.SetVolume(Id,LocalUserId,GetVoiceParticipants(),Volume)) return;
	ApplyParticipantReceiveSettings(Id);
}
void UProjectProject01VoiceChatSubsystem::HandleParticipantAdded(const FString& JoinedChannel,const FString& Id)
{
	if (!bConfiguredForSurvivor || !VoiceUser || JoinedChannel!=ChannelName || Id.IsEmpty() || Id==LocalUserId) return;
	AppliedPeerGains.Remove(Id);
	SmoothedPeerDistanceGains.Remove(Id);
	VoiceUser->SetPlayerMuted(Id,IsParticipantMuted(Id));
	ApplyParticipantReceiveSettings(Id);
}

bool UProjectProject01VoiceChatSubsystem::HasFreshAudibility() const
{
	return LastAudibilityUpdateSeconds>0 && FPlatformTime::Seconds()-LastAudibilityUpdateSeconds<=1.0;
}

float UProjectProject01VoiceChatSubsystem::GetParticipantDistanceGain(const FString& Id) const
{
	if(!HasFreshAudibility() || !bAuthoritativeVoiceAllowed)return 0;
	const FString* Account=ParticipantAccountIds.Find(Id.ToLower());
	const float* Gain=Account?AuthoritativePeerGains.Find(*Account):nullptr;
	return Gain?*Gain:0;
}

float UProjectProject01VoiceChatSubsystem::AdvanceParticipantDistanceGain(const FString& Id,float DeltaSeconds)
{
	const float Target=GetParticipantDistanceGain(Id);
	auto& Ramp=SmoothedPeerDistanceGains.FindOrAdd(Id);
	// Never retain voice after death, eligibility loss, unknown identity, stale
	// server authority or the maximum audible range. All those paths yield zero.
	if (Target<=0) { Ramp.Reset();return 0; }
	const float Duration=FMath::IsFinite(DistanceGainInterpolationSeconds)?FMath::Clamp(DistanceGainInterpolationSeconds,.02f,1.f):.15f;
	return Ramp.Advance(Target,DeltaSeconds,Duration);
}

void UProjectProject01VoiceChatSubsystem::ApplyParticipantReceiveSettings(const FString& Id,float DeltaSeconds)
{
	if(!VoiceUser)return;
	const float Gain=GetParticipantVolume(Id)*AdvanceParticipantDistanceGain(Id,DeltaSeconds);
	const float* Applied=AppliedPeerGains.Find(Id);
	if(!Applied || !FMath::IsNearlyEqual(*Applied,Gain,0.001f))
	{
		VoiceUser->SetPlayerVolume(Id,Gain);AppliedPeerGains.Add(Id,Gain);
	}
}

void UProjectProject01VoiceChatSubsystem::UpdateAuthoritativeAudibility(const FString& InMatchId,bool bAllowed,const TArray<FProjectVoiceAudibility>& Peers)
{
	if(!bConfiguredForSurvivor || InMatchId!=MatchId || Peers.Num()>3)return;
	AuthoritativePeerGains.Reset();
	for(const auto& Peer:Peers)
		if(!Peer.UserId.IsEmpty() && FMath::IsFinite(Peer.Gain))AuthoritativePeerGains.Add(Peer.UserId,FMath::Clamp(Peer.Gain,0.0f,1.0f));
	bAuthoritativeVoiceAllowed=bAllowed;LastAudibilityUpdateSeconds=FPlatformTime::Seconds();
	UpdateReceiveVolume();ApplyTransmitMode();
}
void UProjectProject01VoiceChatSubsystem::ResetParticipantPreferences()
{
	const auto Adjusted=ParticipantControls.GetAdjustedIds();
	ParticipantControls.Reset();AppliedPeerGains.Reset();
	// Reset personal preferences, not the server's distance/eligibility decision.
	if (VoiceUser && VoiceUser->IsLoggedIn())
		for (const FString& Id:Adjusted)
		{
			VoiceUser->SetPlayerMuted(Id,false);
			if(bConfiguredForSurvivor)ApplyParticipantReceiveSettings(Id);
			else VoiceUser->SetPlayerVolume(Id,1.0f);
		}
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
