// File: Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp
// Build target: ProjectProject01 / ProjectProject01Server, Unreal Engine 5.8.2

#include "ProjectProject01GameInstance.h"

#include "Dom/JsonObject.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "AudioDevice.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/NetworkVersion.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "Rendering/RenderingCommon.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01NetworkSecurity, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogProjectProject01UserSettings, Log, All);

namespace ProjectProject01UserSettings
{
	bool IsAllowedBinding(const FKey Key)
	{
		return Key.IsValid() && !Key.IsGamepadKey() && Key != EKeys::Escape;
	}
}

UProjectProject01GameUserSettings::UProjectProject01GameUserSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UProjectProject01GameUserSettings* UProjectProject01GameUserSettings::Get()
{
	return IsValid(GEngine) ? Cast<UProjectProject01GameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UProjectProject01GameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	MasterVolume = 1.0f;
	SFXVolume = 1.0f;
	MusicVolume = 1.0f;
	UIVolume = 1.0f;
	bMuteAll = false;
	bMuteWhenUnfocused = true;
	bMotionBlurEnabled = true;
	DisplayGammaSetting = 2.2f;
	MouseSensitivity = 1.0f;
	VFXIntensity = EProjectProject01VFXIntensity::Standard;
	bSubtitlesEnabled = true;
	bEnhancedVisualCues = true;
	ColorVisionMode = EProjectProject01ColorVisionMode::Normal;
	ColorVisionSeverity = 5.0f;
	ScreenFlashScale = 1.0f;
	ScreenDistortionScale = 1.0f;
	ScreenShakeScale = 1.0f;
	UIReadableScale = 1.0f;
	bAutomaticServerConnection = true;
	DirectServerAddress.Reset();
	MoveForwardKeyName = EKeys::W.GetFName();
	MoveBackwardKeyName = EKeys::S.GetFName();
	MoveLeftKeyName = EKeys::A.GetFName();
	MoveRightKeyName = EKeys::D.GetFName();
	SprintKeyName = EKeys::LeftShift.GetFName();
	VoicePushToTalkKeyName = EKeys::F2.GetFName();
	VoiceToggleKeyName = EKeys::U.GetFName();
	HelpPingKeyName = EKeys::Z.GetFName();
	DangerPingKeyName = EKeys::X.GetFName();
	LocationPingKeyName = EKeys::C.GetFName();
	SetFrameRateLimit(120.0f);
	SetResolutionScaleValueEx(100.0f);
	SetOverallScalabilityLevel(3);
}

void UProjectProject01GameUserSettings::LoadSettings(const bool bForceReload)
{
	Super::LoadSettings(bForceReload);
	ValidateProjectSettings();
}

void UProjectProject01GameUserSettings::ApplySettings(const bool bCheckForCommandLineOverrides)
{
	ValidateProjectSettings();
	Super::ApplySettings(bCheckForCommandLineOverrides);
	ApplyProjectSettings(true);
}

void UProjectProject01GameUserSettings::ApplyNonResolutionSettings()
{
	ValidateProjectSettings();
	Super::ApplyNonResolutionSettings();
	ApplyProjectSettings(true);
}

void UProjectProject01GameUserSettings::ValidateProjectSettings()
{
	MasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
	SFXVolume = FMath::Clamp(SFXVolume, 0.0f, 1.0f);
	MusicVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
	UIVolume = FMath::Clamp(UIVolume, 0.0f, 1.0f);
	DisplayGammaSetting = FMath::Clamp(DisplayGammaSetting, 1.8f, 2.6f);
	MouseSensitivity = FMath::Clamp(MouseSensitivity, 0.1f, 3.0f);
	ColorVisionSeverity = FMath::Clamp(ColorVisionSeverity, 0.0f, 10.0f);
	ScreenFlashScale = FMath::Clamp(ScreenFlashScale, 0.0f, 1.0f);
	ScreenDistortionScale = FMath::Clamp(ScreenDistortionScale, 0.0f, 1.0f);
	ScreenShakeScale = FMath::Clamp(ScreenShakeScale, 0.0f, 1.0f);
	UIReadableScale = FMath::Clamp(UIReadableScale, 0.8f, 1.3f);
	DirectServerAddress.TrimStartAndEndInline();
	if (VFXIntensity != EProjectProject01VFXIntensity::Standard &&
		VFXIntensity != EProjectProject01VFXIntensity::Reduced)
	{
		VFXIntensity = EProjectProject01VFXIntensity::Standard;
	}
	if (ColorVisionMode != EProjectProject01ColorVisionMode::Normal &&
		ColorVisionMode != EProjectProject01ColorVisionMode::Deuteranopia &&
		ColorVisionMode != EProjectProject01ColorVisionMode::Protanopia &&
		ColorVisionMode != EProjectProject01ColorVisionMode::Tritanopia)
	{
		ColorVisionMode = EProjectProject01ColorVisionMode::Normal;
	}

	TArray<FKey> Keys = {
		GetValidatedKey(MoveForwardKeyName, EKeys::W),
		GetValidatedKey(MoveBackwardKeyName, EKeys::S),
		GetValidatedKey(MoveLeftKeyName, EKeys::A),
		GetValidatedKey(MoveRightKeyName, EKeys::D),
		GetValidatedKey(SprintKeyName, EKeys::LeftShift),
		GetValidatedKey(VoicePushToTalkKeyName, EKeys::F2),
		GetValidatedKey(VoiceToggleKeyName, EKeys::U),
		GetValidatedKey(HelpPingKeyName, EKeys::Z),
		GetValidatedKey(DangerPingKeyName, EKeys::X),
		GetValidatedKey(LocationPingKeyName, EKeys::C)
	};
	TSet<FKey> UniqueKeys;
	bool bHasInvalidOrDuplicate = false;
	for (const FKey Key : Keys)
	{
		if (!ProjectProject01UserSettings::IsAllowedBinding(Key) || UniqueKeys.Contains(Key))
		{
			bHasInvalidOrDuplicate = true;
			break;
		}
		UniqueKeys.Add(Key);
	}
	if (bHasInvalidOrDuplicate)
	{
		UE_LOG(LogProjectProject01UserSettings, Warning,
			TEXT("Invalid, reserved, or duplicate input bindings were reset to safe defaults."));
		MoveForwardKeyName = EKeys::W.GetFName();
		MoveBackwardKeyName = EKeys::S.GetFName();
		MoveLeftKeyName = EKeys::A.GetFName();
		MoveRightKeyName = EKeys::D.GetFName();
		SprintKeyName = EKeys::LeftShift.GetFName();
		VoicePushToTalkKeyName = EKeys::F2.GetFName();
		VoiceToggleKeyName = EKeys::U.GetFName();
		HelpPingKeyName = EKeys::Z.GetFName();
		DangerPingKeyName = EKeys::X.GetFName();
		LocationPingKeyName = EKeys::C.GetFName();
	}
}

void UProjectProject01GameUserSettings::ApplyProjectSettings(const bool bApplicationActive)
{
	ValidateProjectSettings();
	if (IsValid(GEngine))
	{
		GEngine->DisplayGamma = DisplayGammaSetting;
	}
	if (IConsoleVariable* MotionBlurQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		MotionBlurQuality->Set(bMotionBlurEnabled ? 4 : 0, ECVF_SetByGameSetting);
	}
	UGameplayStatics::SetSubtitlesEnabled(bSubtitlesEnabled);
	if (!IsRunningDedicatedServer())
	{
		EColorVisionDeficiency Deficiency = EColorVisionDeficiency::NormalVision;
		switch (ColorVisionMode)
		{
		case EProjectProject01ColorVisionMode::Deuteranopia: Deficiency = EColorVisionDeficiency::Deuteranope; break;
		case EProjectProject01ColorVisionMode::Protanopia: Deficiency = EColorVisionDeficiency::Protanope; break;
		case EProjectProject01ColorVisionMode::Tritanopia: Deficiency = EColorVisionDeficiency::Tritanope; break;
		default: break;
		}
		UWidgetBlueprintLibrary::SetColorVisionDeficiencyType(
			Deficiency, ColorVisionSeverity, Deficiency != EColorVisionDeficiency::NormalVision, false);
		// FSlateApplication is shared with the editor during PIE. Scaling it there would
		// resize the editor itself, so apply the game-wide UI scale only to standalone/package runs.
		if (FSlateApplication::IsInitialized() && !GIsEditor)
		{
			FSlateApplication::Get().SetApplicationScale(UIReadableScale);
		}
		if (ScreenShakeScale <= KINDA_SMALL_NUMBER && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (UWorld* World = Context.World(); IsValid(World))
				{
					for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
					{
						if (APlayerController* Controller = It->Get(); IsValid(Controller) && IsValid(Controller->PlayerCameraManager))
						{
							Controller->PlayerCameraManager->StopAllCameraShakes(true);
						}
					}
				}
			}
		}
	}
	ApplyAudioSettings(bApplicationActive);
}

void UProjectProject01GameUserSettings::ApplyAudioSettings(const bool bApplicationActive)
{
	EnsureRuntimeAudioObjects();
	if (!IsValid(GEngine))
	{
		return;
	}
	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	if (!AudioDevice.IsValid())
	{
		return;
	}
	const bool bMuted = bMuteAll || (bMuteWhenUnfocused && !bApplicationActive);
	AudioDevice->SetTransientPrimaryVolume(bMuted ? 0.0f : MasterVolume);
	if (IsValid(RuntimeSoundMix))
	{
		if (!bRuntimeSoundMixPushed)
		{
			AudioDevice->PushSoundMixModifier(RuntimeSoundMix);
			bRuntimeSoundMixPushed = true;
		}
		AudioDevice->SetSoundMixClassOverride(RuntimeSoundMix, RuntimeSFXSoundClass, SFXVolume, 1.0f, 0.05f, true);
		AudioDevice->SetSoundMixClassOverride(RuntimeSoundMix, RuntimeMusicSoundClass, MusicVolume, 1.0f, 0.05f, true);
		AudioDevice->SetSoundMixClassOverride(RuntimeSoundMix, RuntimeUISoundClass, UIVolume, 1.0f, 0.05f, true);
	}
}

FKey UProjectProject01GameUserSettings::GetValidatedKey(const FName StoredName, const FKey Fallback) const
{
	const FKey Key(StoredName);
	return ProjectProject01UserSettings::IsAllowedBinding(Key) ? Key : Fallback;
}

FKey UProjectProject01GameUserSettings::GetMoveForwardKey() const { return GetValidatedKey(MoveForwardKeyName, EKeys::W); }
FKey UProjectProject01GameUserSettings::GetMoveBackwardKey() const { return GetValidatedKey(MoveBackwardKeyName, EKeys::S); }
FKey UProjectProject01GameUserSettings::GetMoveLeftKey() const { return GetValidatedKey(MoveLeftKeyName, EKeys::A); }
FKey UProjectProject01GameUserSettings::GetMoveRightKey() const { return GetValidatedKey(MoveRightKeyName, EKeys::D); }
FKey UProjectProject01GameUserSettings::GetSprintKey() const { return GetValidatedKey(SprintKeyName, EKeys::LeftShift); }
FKey UProjectProject01GameUserSettings::GetVoicePushToTalkKey() const { return GetValidatedKey(VoicePushToTalkKeyName, EKeys::F2); }
FKey UProjectProject01GameUserSettings::GetVoiceToggleKey() const { return GetValidatedKey(VoiceToggleKeyName, EKeys::U); }
FKey UProjectProject01GameUserSettings::GetHelpPingKey() const { return GetValidatedKey(HelpPingKeyName, EKeys::Z); }
FKey UProjectProject01GameUserSettings::GetDangerPingKey() const { return GetValidatedKey(DangerPingKeyName, EKeys::X); }
FKey UProjectProject01GameUserSettings::GetLocationPingKey() const { return GetValidatedKey(LocationPingKeyName, EKeys::C); }

void UProjectProject01GameUserSettings::EnsureRuntimeAudioObjects()
{
	if (!IsValid(RuntimeSFXSoundClass))
	{
		RuntimeSFXSoundClass = NewObject<USoundClass>(this, TEXT("ProjectProject01SFXSoundClass"));
	}
	if (!IsValid(RuntimeMusicSoundClass))
	{
		RuntimeMusicSoundClass = NewObject<USoundClass>(this, TEXT("ProjectProject01MusicSoundClass"));
	}
	if (!IsValid(RuntimeUISoundClass))
	{
		RuntimeUISoundClass = NewObject<USoundClass>(this, TEXT("ProjectProject01UISoundClass"));
	}
	if (!IsValid(RuntimeSoundMix))
	{
		RuntimeSoundMix = NewObject<USoundMix>(this, TEXT("ProjectProject01UserSoundMix"));
	}
}

USoundClass* UProjectProject01GameUserSettings::GetSFXSoundClass()
{
	EnsureRuntimeAudioObjects();
	return RuntimeSFXSoundClass;
}

USoundClass* UProjectProject01GameUserSettings::GetMusicSoundClass()
{
	EnsureRuntimeAudioObjects();
	return RuntimeMusicSoundClass;
}

USoundClass* UProjectProject01GameUserSettings::GetUISoundClass()
{
	EnsureRuntimeAudioObjects();
	return RuntimeUISoundClass;
}

namespace ProjectProject01NetworkSecurity
{
	constexpr int32 Aes256KeyBytes = 32;
	TArray<uint8> PendingClientEncryptionKey;
	FString PendingClientTicket;
	TMap<FString, FProjectProject01ValidatedJoinClaim> ValidatedClaims;

	FString NormalizeBaseUrl(FString Url)
	{
		Url.TrimStartAndEndInline();
		while (Url.EndsWith(TEXT("/")))
		{
			Url.LeftChopInline(1);
		}
		return Url;
	}

	bool ParseIpv4Address(const FString& Input, FString& OutAddress)
	{
		FString Address = Input;
		Address.TrimStartAndEndInline();
		TArray<FString> Parts;
		Address.ParseIntoArray(Parts, TEXT("."), false);
		if (Parts.Num() != 4)
		{
			return false;
		}
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty() || !Part.IsNumeric())
			{
				return false;
			}
			const int32 Octet = FCString::Atoi(*Part);
			if (Octet < 0 || Octet > 255)
			{
				return false;
			}
		}
		OutAddress = FString::Printf(TEXT("%d.%d.%d.%d"),
			FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]),
			FCString::Atoi(*Parts[2]), FCString::Atoi(*Parts[3]));
		return true;
	}

	bool IsSupportedTestAddress(const FString& Address)
	{
		TArray<FString> Parts;
		Address.ParseIntoArray(Parts, TEXT("."), false);
		if (Parts.Num() != 4) return false;
		const int32 A = FCString::Atoi(*Parts[0]);
		const int32 B = FCString::Atoi(*Parts[1]);
		return A == 25 || A == 10 || (A == 172 && B >= 16 && B <= 31) ||
			(A == 192 && B == 168) || A == 127;
	}

	FString ExtractHostFromBaseUrl(FString Url)
	{
		Url = NormalizeBaseUrl(Url);
		Url.RemoveFromStart(TEXT("http://"));
		Url.RemoveFromStart(TEXT("https://"));
		FString Host;
		if (Url.Split(TEXT(":"), &Host, nullptr))
		{
			return Host;
		}
		return Url;
	}

	void CompleteEncryptionFailure(
		const FOnEncryptionKeyResponse& Delegate,
		const EEncryptionResponse ResponseCode,
		const FString& Error)
	{
		FEncryptionKeyResponse Response(ResponseCode, Error);
		Delegate.ExecuteIfBound(Response);
	}

	bool ParseEncryptionKey(const FString& Base64, TArray<uint8>& OutKey)
	{
		OutKey.Reset();
		return FBase64::Decode(Base64, OutKey) && OutKey.Num() == Aes256KeyBytes;
	}
}

namespace ProjectProject01CrashReports
{
	constexpr int32 MaxShortFieldLength = 128;
	constexpr int32 MaxPathFieldLength = 256;

	FString LimitField(FString Value, const int32 MaxLength = MaxShortFieldLength)
	{
		Value.TrimStartAndEndInline();
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Value.Left(MaxLength);
	}

	FString GetProcessRole(const UWorld* World)
	{
		if (IsRunningDedicatedServer() || (IsValid(World) && World->GetNetMode() == NM_DedicatedServer))
		{
			return TEXT("DedicatedServer");
		}
		if (!IsValid(World)) return TEXT("Client");
		switch (World->GetNetMode())
		{
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Standalone: return TEXT("Standalone");
		default: return TEXT("Client");
		}
	}

	FString GetBuildVersion()
	{
		FString Version = FNetworkVersion::GetProjectVersion();
		if (Version.IsEmpty()) Version = FApp::GetBuildVersion();
		return Version.IsEmpty() ? TEXT("Unknown") : LimitField(Version);
	}

	FString SanitizeStoredReport(const FString& Input)
	{
		TSharedPtr<FJsonObject> Source;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Source) || !Source.IsValid())
		{
			return FString();
		}
		const TArray<FString> Required = {
			TEXT("reportId"), TEXT("startedAtUtc"), TEXT("lastUpdatedAtUtc"), TEXT("buildVersion"),
			TEXT("mapName"), TEXT("gameMode"), TEXT("processRole"), TEXT("lastLogPath"), TEXT("diagnosticRunId") };
		TSharedRef<FJsonObject> Safe = MakeShared<FJsonObject>();
		for (const FString& Key : Required)
		{
			FString Value;
			if (!Source->TryGetStringField(Key, Value)) return FString();
			Safe->SetStringField(Key, LimitField(Value, Key == TEXT("lastLogPath") ? MaxPathFieldLength : MaxShortFieldLength));
		}
		for (const FString& Key : { FString(TEXT("matchId")), FString(TEXT("playerRole")) })
		{
			FString Value;
			if (Source->TryGetStringField(Key, Value) && !Value.IsEmpty())
			{
				Safe->SetStringField(Key, LimitField(Value));
			}
		}
		Safe->SetStringField(TEXT("detectedAtUtc"), FDateTime::UtcNow().ToIso8601());
		FString Output;
		FJsonSerializer::Serialize(Safe, TJsonWriterFactory<>::Create(&Output));
		return Output;
	}
}

void UProjectProject01GameInstance::Init()
{
	SecurityApiBaseUrl = ProjectProject01NetworkSecurity::NormalizeBaseUrl(SecurityApiBaseUrl);
	AllowedInsecureVpnSecurityApiBaseUrl =
		ProjectProject01NetworkSecurity::NormalizeBaseUrl(AllowedInsecureVpnSecurityApiBaseUrl);
	SecurityRequestTimeoutSeconds = FMath::Clamp(SecurityRequestTimeoutSeconds, 2.0f, 30.0f);
	AutomaticMultiplayerServerAddress = ProjectProject01NetworkSecurity::ExtractHostFromBaseUrl(SecurityApiBaseUrl);
	RuntimeMultiplayerServerAddress = AutomaticMultiplayerServerAddress;
	Super::Init();
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		Settings->LoadSettings(false);
		Settings->ApplyProjectSettings(true);
		FString EndpointError;
		if (!ConfigureMultiplayerServerAddress(
			Settings->GetDirectServerAddress(), Settings->IsAutomaticServerConnectionEnabled(), EndpointError))
		{
			UE_LOG(LogProjectProject01NetworkSecurity, Warning,
				TEXT("Saved multiplayer server selection was not applied: %s"), *EndpointError);
		}
	}
	if (!IsRunningDedicatedServer() && FSlateApplication::IsInitialized())
	{
		ApplicationActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
			this, &UProjectProject01GameInstance::HandleApplicationActivationChanged);
	}
	if (!IsRunningDedicatedServer() && GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(
			this, &UProjectProject01GameInstance::HandleNetworkFailure);
	}

	if (!IsBackendUrlAllowed())
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Security API must use HTTPS except for loopback development: %s"), *SecurityApiBaseUrl);
	}
	InitializeCrashReportCollection();
}

void UProjectProject01GameInstance::Shutdown()
{
	if (PostWorldInitializationHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitializationHandle);
		PostWorldInitializationHandle.Reset();
	}
	if (!ActiveCrashMarkerPath.IsEmpty())
	{
		IFileManager::Get().Delete(*ActiveCrashMarkerPath, false, true, true);
	}
	if (NetworkFailureHandle.IsValid() && GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		NetworkFailureHandle.Reset();
	}
	if (ApplicationActivationHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationHandle);
		ApplicationActivationHandle.Reset();
	}
	for (const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& Request : PendingSecurityRequests)
	{
		if (Request.IsValid())
		{
			Request->OnProcessRequestComplete().Unbind();
			Request->CancelRequest();
		}
	}
	PendingSecurityRequests.Reset();
	Super::Shutdown();
}

void UProjectProject01GameInstance::InitializeCrashReportCollection()
{
	CrashRunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	CrashStartedAtUtc = FDateTime::UtcNow().ToIso8601();
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrashReports"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	RecoverPendingCrashReports();
	ActiveCrashMarkerPath = FPaths::Combine(Directory, FString::Printf(TEXT("Active-%u-%s.json"),
		FPlatformProcess::GetCurrentProcessId(), *CrashRunId));
	WriteActiveCrashMarker(GetWorld());

	const TWeakObjectPtr<UProjectProject01GameInstance> WeakThis(this);
	PostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda(
		[WeakThis](UWorld* World, const UWorld::InitializationValues)
		{
			UProjectProject01GameInstance* Instance = WeakThis.Get();
			if (IsValid(Instance) && IsValid(World) && World->GetGameInstance() == Instance &&
				(World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
			{
				Instance->RefreshCrashReportContext(World);
				const TWeakObjectPtr<UWorld> WeakWorld(World);
				World->OnWorldBeginPlay.AddWeakLambda(Instance, [WeakThis, WeakWorld]()
				{
					if (UProjectProject01GameInstance* LiveInstance = WeakThis.Get(); IsValid(LiveInstance))
					{
						LiveInstance->RefreshCrashReportContext(WeakWorld.Get());
					}
				});
			}
		});
	RefreshCrashReportContext(GetWorld());
}

void UProjectProject01GameInstance::RefreshCrashReportContext(UWorld* World)
{
	WriteActiveCrashMarker(World);
	FGenericCrashContext::SetGameData(TEXT("ProjectReportId"), CrashRunId);
	FGenericCrashContext::SetGameData(TEXT("ProjectBuildVersion"), ProjectProject01CrashReports::GetBuildVersion());
	FGenericCrashContext::SetGameData(TEXT("ProjectMap"),
		IsValid(World) ? ProjectProject01CrashReports::LimitField(World->GetMapName()) : TEXT("Unavailable"));
	FGenericCrashContext::SetGameData(TEXT("ProjectProcessRole"), ProjectProject01CrashReports::GetProcessRole(World));
	FGenericCrashContext::SetGameData(TEXT("ProjectPlayerRole"), PendingCrashPlayerRole);
	FGenericCrashContext::SetGameData(TEXT("ProjectMatchId"), PendingCrashMatchId);
	if (IsValid(World))
	{
		if (const UProjectProject01DiagnosticsSubsystem* Diagnostics = World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>();
			IsValid(Diagnostics))
		{
			FGenericCrashContext::SetGameData(TEXT("ProjectTestRunId"), Diagnostics->GetTestRunId());
		}
	}
}

FString UProjectProject01GameInstance::BuildSafeCrashReportJson(UWorld* World) const
{
	FString GameModeName = TEXT("Unavailable");
	if (IsValid(World))
	{
		if (const AGameModeBase* GameMode = World->GetAuthGameMode(); IsValid(GameMode))
		{
			GameModeName = GameMode->GetClass()->GetName();
		}
		else if (const AGameStateBase* GameState = World->GetGameState(); IsValid(GameState) && GameState->GameModeClass)
		{
			GameModeName = GameState->GameModeClass->GetName();
		}
	}
	FString DiagnosticRunId = CrashRunId;
	if (IsValid(World))
	{
		if (const UProjectProject01DiagnosticsSubsystem* Diagnostics = World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>();
			IsValid(Diagnostics) && !Diagnostics->GetTestRunId().IsEmpty())
		{
			DiagnosticRunId = Diagnostics->GetTestRunId();
		}
	}
	const FString Now = FDateTime::UtcNow().ToIso8601();
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("reportId"), CrashRunId);
	Json->SetStringField(TEXT("startedAtUtc"), CrashStartedAtUtc.IsEmpty() ? Now : CrashStartedAtUtc);
	Json->SetStringField(TEXT("lastUpdatedAtUtc"), Now);
	Json->SetStringField(TEXT("buildVersion"), ProjectProject01CrashReports::GetBuildVersion());
	Json->SetStringField(TEXT("mapName"), IsValid(World)
		? ProjectProject01CrashReports::LimitField(World->GetMapName()) : TEXT("Unavailable"));
	Json->SetStringField(TEXT("gameMode"), ProjectProject01CrashReports::LimitField(GameModeName));
	Json->SetStringField(TEXT("processRole"), ProjectProject01CrashReports::GetProcessRole(World));
	Json->SetStringField(TEXT("playerRole"), ProjectProject01CrashReports::LimitField(PendingCrashPlayerRole));
	Json->SetStringField(TEXT("matchId"), ProjectProject01CrashReports::LimitField(PendingCrashMatchId));
	Json->SetStringField(TEXT("lastLogPath"), TEXT("Saved/Logs/ProjectProject01.log"));
	Json->SetStringField(TEXT("diagnosticRunId"), ProjectProject01CrashReports::LimitField(DiagnosticRunId));
	FString Output;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Output));
	return Output;
}

void UProjectProject01GameInstance::WriteActiveCrashMarker(UWorld* World)
{
	if (!ActiveCrashMarkerPath.IsEmpty())
	{
		FFileHelper::SaveStringToFile(BuildSafeCrashReportJson(World), *ActiveCrashMarkerPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

void UProjectProject01GameInstance::RecoverPendingCrashReports()
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrashReports"));
	IFileManager& FileManager = IFileManager::Get();
	TArray<FString> ActiveFiles;
	FileManager.FindFiles(ActiveFiles, *FPaths::Combine(Directory, TEXT("Active-*.json")), true, false);
	const FString CurrentProcessPrefix = FString::Printf(TEXT("Active-%u-"), FPlatformProcess::GetCurrentProcessId());
	for (const FString& FileName : ActiveFiles)
	{
		if (FileName.StartsWith(CurrentProcessPrefix)) continue;
		const FString Source = FPaths::Combine(Directory, FileName);
		const FString Destination = FPaths::Combine(Directory, TEXT("Pending-") + FileName.RightChop(7));
		FileManager.Move(*Destination, *Source, true, true, false, true);
	}

	TArray<FString> PendingFiles;
	FileManager.FindFiles(PendingFiles, *FPaths::Combine(Directory, TEXT("Pending-*.json")), true, false);
	for (const FString& FileName : PendingFiles)
	{
		UploadPendingCrashReport(FPaths::Combine(Directory, FileName));
	}
}

void UProjectProject01GameInstance::UploadPendingCrashReport(const FString& ReportPath)
{
	if (!IsBackendUrlAllowed()) return;
	FString Stored;
	if (!FFileHelper::LoadFileToString(Stored, *ReportPath)) return;
	const FString Body = ProjectProject01CrashReports::SanitizeStoredReport(Stored);
	if (Body.IsEmpty()) return;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/crash-reports"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->SetContentAsString(Body);
	const TWeakObjectPtr<UProjectProject01GameInstance> WeakThis(this);
	const TWeakPtr<IHttpRequest, ESPMode::ThreadSafe> WeakRequest(Request);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, WeakRequest, ReportPath](FHttpRequestPtr, FHttpResponsePtr Response, const bool bSucceeded)
		{
			if (UProjectProject01GameInstance* Instance = WeakThis.Get(); IsValid(Instance))
			{
				Instance->RemovePendingRequest(WeakRequest.Pin());
			}
			if (bSucceeded && Response.IsValid() && Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300)
			{
				IFileManager::Get().Delete(*ReportPath, false, true, true);
			}
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		PendingSecurityRequests.Remove(Request);
	}
}

void UProjectProject01GameInstance::HandleNetworkFailure(
	UWorld* World,
	UNetDriver* NetDriver,
	const ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	if (IsRunningDedicatedServer() || !IsValid(World) ||
		!World->GetMapName().Contains(TEXT("MultiplayTest")))
	{
		return;
	}
	UE_LOG(LogProjectProject01NetworkSecurity, Warning,
		TEXT("Game server connection ended (%d): %s. Returning to the lobby for reconnect handling."),
		static_cast<int32>(FailureType), *ErrorString);
	if (APlayerController* PlayerController = World->GetFirstPlayerController(); IsValid(PlayerController))
	{
		if (IsValid(PlayerController->PlayerCameraManager))
		{
			PlayerController->PlayerCameraManager->StartCameraFade(
				0.0f, 1.0f, 0.5f, FLinearColor::Black, false, true);
		}
		const TWeakObjectPtr<APlayerController> WeakController(PlayerController);
		FTimerHandle ReturnTimer;
		World->GetTimerManager().SetTimer(ReturnTimer, [WeakController]()
		{
			if (APlayerController* Controller = WeakController.Get(); IsValid(Controller))
			{
				Controller->ClientTravel(TEXT("/Game/MyProject/Level/LobbyLevel"), TRAVEL_Absolute);
			}
		}, 0.5f, false);
	}
}

void UProjectProject01GameInstance::HandleApplicationActivationChanged(const bool bApplicationActive)
{
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		Settings->ApplyAudioSettings(bApplicationActive);
	}
}

bool UProjectProject01GameInstance::ConfigurePendingGameConnection(
	const FString& Ticket,
	const FString& EncryptionKeyBase64,
	const FString& RoomId,
	const FString& MatchId,
	const FString& Role)
{
	TArray<uint8> Key;
	if (Ticket.Len() < 48 || Ticket.Len() > 256 || RoomId.IsEmpty() || MatchId.IsEmpty() ||
		(Role != TEXT("Mannequin") && Role != TEXT("Survivor")) ||
		!ProjectProject01NetworkSecurity::ParseEncryptionKey(EncryptionKeyBase64, Key))
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Rejected malformed game ticket response before ClientTravel."));
		return false;
	}

	ProjectProject01NetworkSecurity::PendingClientTicket = Ticket;
	ProjectProject01NetworkSecurity::PendingClientEncryptionKey = MoveTemp(Key);
	PendingCrashMatchId = MatchId.Left(ProjectProject01CrashReports::MaxShortFieldLength);
	PendingCrashPlayerRole = Role.Left(ProjectProject01CrashReports::MaxShortFieldLength);
	RefreshCrashReportContext(GetWorld());
	return true;
}

void UProjectProject01GameInstance::ClearPendingGameConnection()
{
	ProjectProject01NetworkSecurity::PendingClientTicket.Reset();
	ProjectProject01NetworkSecurity::PendingClientEncryptionKey.Reset();
	PendingCrashMatchId.Reset();
	PendingCrashPlayerRole.Reset();
	RefreshCrashReportContext(GetWorld());
}

void UProjectProject01GameInstance::ReceivedNetworkEncryptionToken(
	const FString& EncryptionToken,
	const FOnEncryptionKeyResponse& Delegate)
{
	if (EncryptionToken.Len() < 48 || EncryptionToken.Len() > 256 || !IsBackendUrlAllowed())
	{
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::InvalidToken, TEXT("Invalid game connection ticket."));
		return;
	}

	const FString ServerSecret = LoadGameServerSharedSecret();
	if (ServerSecret.Len() < 32)
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Dedicated server security secret is not configured."));
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::Failure, TEXT("Game server security is not configured."));
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("ticket"), EncryptionToken);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/server/game-tickets/consume"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"), ServerSecret);
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetContentAsString(Body);
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, EncryptionToken, Delegate](
			const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& CompletedRequest,
			const TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& Response,
			const bool bConnectedSuccessfully)
		{
			RemovePendingRequest(CompletedRequest);
			if (!bConnectedSuccessfully || !Response.IsValid() ||
				Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
			{
				UE_LOG(LogProjectProject01NetworkSecurity, Warning,
					TEXT("Game ticket validation failed with HTTP status %d."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::InvalidToken, TEXT("Game connection ticket was rejected."));
				return;
			}

			TSharedPtr<FJsonObject> JsonResponse;
			if (!FJsonSerializer::Deserialize(
				TJsonReaderFactory<>::Create(Response->GetContentAsString()), JsonResponse) || !JsonResponse.IsValid())
			{
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::Failure, TEXT("Invalid game ticket validation response."));
				return;
			}

			FString EncryptionKeyBase64;
			FProjectProject01ValidatedJoinClaim Claim;
			if (!JsonResponse->TryGetStringField(TEXT("encryptionKey"), EncryptionKeyBase64) ||
				!JsonResponse->TryGetStringField(TEXT("userId"), Claim.UserId) ||
				!JsonResponse->TryGetStringField(TEXT("displayName"), Claim.DisplayName) ||
				!JsonResponse->TryGetStringField(TEXT("roomId"), Claim.RoomId) ||
				!JsonResponse->TryGetStringField(TEXT("matchId"), Claim.MatchId) ||
				!JsonResponse->TryGetStringField(TEXT("role"), Claim.Role) ||
				!JsonResponse->TryGetStringField(TEXT("clientBuildVersion"), Claim.ClientBuildVersion) ||
				!JsonResponse->TryGetStringField(TEXT("dedicatedServerBuildVersion"), Claim.DedicatedServerBuildVersion) ||
				!JsonResponse->TryGetStringField(TEXT("apiVersion"), Claim.ApiVersion) ||
				!JsonResponse->TryGetStringField(TEXT("gameDataVersion"), Claim.GameDataVersion) ||
				!JsonResponse->TryGetStringField(TEXT("networkProtocolVersion"), Claim.NetworkProtocolVersion))
			{
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::Failure, TEXT("Incomplete game ticket validation response."));
				return;
			}
			FString VersionError;
			if (!FProjectProject01VersionContract::Matches(
				Claim.ClientBuildVersion, Claim.DedicatedServerBuildVersion, Claim.ApiVersion, Claim.GameDataVersion,
				Claim.NetworkProtocolVersion, VersionError))
			{
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::InvalidToken, VersionError);
				return;
			}

			TArray<uint8> Key;
			if (!ProjectProject01NetworkSecurity::ParseEncryptionKey(EncryptionKeyBase64, Key))
			{
				ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
					Delegate, EEncryptionResponse::NoKey, TEXT("Invalid AES-GCM key."));
				return;
			}

			const FDateTime NowUtc = FDateTime::UtcNow();
			for (auto It = ProjectProject01NetworkSecurity::ValidatedClaims.CreateIterator(); It; ++It)
			{
				if (It.Value().ValidUntilUtc <= NowUtc)
				{
					It.RemoveCurrent();
				}
			}
			Claim.ValidUntilUtc = NowUtc + FTimespan::FromSeconds(30.0);
			ProjectProject01NetworkSecurity::ValidatedClaims.Add(EncryptionToken, MoveTemp(Claim));
			FEncryptionKeyResponse KeyResponse;
			KeyResponse.Response = EEncryptionResponse::Success;
			KeyResponse.EncryptionData.Key = MoveTemp(Key);
			KeyResponse.EncryptionData.Identifier = TEXT("ProjectProject01Match");
			Delegate.ExecuteIfBound(KeyResponse);
		});

	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		RemovePendingRequest(Request);
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::Failure, TEXT("Unable to start game ticket validation."));
	}
}

void UProjectProject01GameInstance::ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate)
{
	if (ProjectProject01NetworkSecurity::PendingClientEncryptionKey.Num() !=
		ProjectProject01NetworkSecurity::Aes256KeyBytes)
	{
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::NoKey, TEXT("No pending AES-GCM key is available."));
		return;
	}

	FEncryptionKeyResponse Response;
	Response.Response = EEncryptionResponse::Success;
	Response.EncryptionData.Key = ProjectProject01NetworkSecurity::PendingClientEncryptionKey;
	Response.EncryptionData.Identifier = TEXT("ProjectProject01Match");
	Delegate.ExecuteIfBound(Response);
	ClearPendingGameConnection();
}

bool UProjectProject01GameInstance::ConsumeValidatedJoinClaim(
	const FString& Ticket,
	FProjectProject01ValidatedJoinClaim& OutClaim)
{
	if (FProjectProject01ValidatedJoinClaim* Claim = ProjectProject01NetworkSecurity::ValidatedClaims.Find(Ticket))
	{
		if (Claim->ValidUntilUtc <= FDateTime::UtcNow())
		{
			ProjectProject01NetworkSecurity::ValidatedClaims.Remove(Ticket);
			return false;
		}
		OutClaim = MoveTemp(*Claim);
		ProjectProject01NetworkSecurity::ValidatedClaims.Remove(Ticket);
		return true;
	}
	return false;
}

void UProjectProject01GameInstance::SubmitAuthoritativeMatchResult(
	const FString& MatchId,
	const FString& UserId,
	const FString& Role,
	const bool bSuccess,
	const int32 CaptureCount,
	const double FirstCaptureSeconds,
	const double AllCapturedSeconds,
	const int32 RescueCount,
	const double EscapeSeconds)
{
	SubmitAuthoritativeMatchResultWithCallback(
		MatchId, UserId, Role, bSuccess, CaptureCount, FirstCaptureSeconds,
		AllCapturedSeconds, RescueCount, EscapeSeconds, TFunction<void(bool)>());
}

void UProjectProject01GameInstance::SubmitAuthoritativeMatchResultWithCallback(
	const FString& MatchId,
	const FString& UserId,
	const FString& Role,
	const bool bSuccess,
	const int32 CaptureCount,
	const double FirstCaptureSeconds,
	const double AllCapturedSeconds,
	const int32 RescueCount,
	const double EscapeSeconds,
	TFunction<void(bool)> Completion)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client || !IsBackendUrlAllowed())
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Warning,
			TEXT("Rejected authoritative match result outside a server world."));
		if (Completion) Completion(false);
		return;
	}
	const FString ServerSecret = LoadGameServerSharedSecret();
	if (ServerSecret.Len() < 32)
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Error,
			TEXT("Cannot submit match result because the server secret is unavailable."));
		if (Completion) Completion(false);
		return;
	}

	TSharedRef<TFunction<void(bool)>> CompletionRef =
		MakeShared<TFunction<void(bool)>>(MoveTemp(Completion));
	TSharedRef<bool> bCompletionCalled = MakeShared<bool>(false);
	auto CompleteOnce = [CompletionRef, bCompletionCalled](const bool bSubmissionSucceeded)
	{
		if (*bCompletionCalled)
		{
			return;
		}
		*bCompletionCalled = true;
		if (*CompletionRef)
		{
			(*CompletionRef)(bSubmissionSucceeded);
		}
	};

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("matchId"), MatchId);
	Json->SetStringField(TEXT("userId"), UserId);
	Json->SetStringField(TEXT("role"), Role);
	Json->SetBoolField(TEXT("success"), bSuccess);
	Json->SetNumberField(TEXT("captureCount"), CaptureCount);
	Json->SetNumberField(TEXT("firstCaptureSeconds"), FirstCaptureSeconds);
	Json->SetNumberField(TEXT("allCapturedSeconds"), AllCapturedSeconds);
	Json->SetNumberField(TEXT("rescueCount"), RescueCount);
	Json->SetNumberField(TEXT("escapeSeconds"), EscapeSeconds);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/server/matches/results"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"), ServerSecret);
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetContentAsString(Body);
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, CompleteOnce](const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& CompletedRequest,
			const TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& Response,
			const bool bConnectedSuccessfully)
		{
			RemovePendingRequest(CompletedRequest);
			const bool bSucceeded = bConnectedSuccessfully && Response.IsValid() &&
				Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300;
			if (!bSucceeded)
			{
				UE_LOG(LogProjectProject01NetworkSecurity, Error,
					TEXT("Authoritative match result submission failed with status %d."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
			}
			CompleteOnce(bSucceeded);
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		RemovePendingRequest(Request);
		UE_LOG(LogProjectProject01NetworkSecurity, Error, TEXT("Unable to start authoritative result submission."));
		CompleteOnce(false);
	}
}

void UProjectProject01GameInstance::NotifyAuthoritativePlayerForfeit(
	const FString& MatchId,
	const FString& UserId,
	const FString& Role)
{
	const UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client || MatchId.IsEmpty() || UserId.IsEmpty() ||
		!IsBackendUrlAllowed())
	{
		return;
	}
	const FString ServerSecret = LoadGameServerSharedSecret();
	if (ServerSecret.Len() < 32)
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Warning,
			TEXT("Cannot notify the backend about forfeit because the server secret is unavailable."));
		return;
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("matchId"), MatchId);
	Json->SetStringField(TEXT("userId"), UserId);
	Json->SetStringField(TEXT("role"), Role);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/server/matches/forfeit"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"), ServerSecret);
	FProjectProject01VersionContract::ApplyToRequest(Request);
	Request->SetContentAsString(Body);
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this](const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& CompletedRequest,
			const TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& Response,
			const bool bConnectedSuccessfully)
		{
			RemovePendingRequest(CompletedRequest);
			if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() < 200 ||
				Response->GetResponseCode() >= 300)
			{
				UE_LOG(LogProjectProject01NetworkSecurity, Warning,
					TEXT("Backend forfeit notification failed with status %d; match authority remains on the game server."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
			}
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		RemovePendingRequest(Request);
	}
}

FString UProjectProject01GameInstance::LoadGameServerSharedSecret() const
{
	FString Secret = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECTPROJECT01_GAME_SERVER_SECRET"));
	Secret.TrimStartAndEndInline();
#if !UE_BUILD_SHIPPING
	if (Secret.IsEmpty())
	{
		const FString LocalConfigPath = FPaths::Combine(
			FPaths::ProjectDir(), TEXT("Tools/ProjectProject01Backend/LocalMySql/ProjectProject01Backend.local.json"));
		FString JsonText;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(JsonText, *LocalConfigPath) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) && Root.IsValid())
		{
			const TSharedPtr<FJsonObject>* GameServer = nullptr;
			if (Root->TryGetObjectField(TEXT("GameServer"), GameServer) && GameServer != nullptr && GameServer->IsValid())
			{
				(*GameServer)->TryGetStringField(TEXT("SharedSecret"), Secret);
				Secret.TrimStartAndEndInline();
			}
		}
	}
#endif
	return Secret;
}

bool UProjectProject01GameInstance::IsBackendUrlAllowed() const
{
	return SecurityApiBaseUrl.StartsWith(TEXT("https://")) ||
		SecurityApiBaseUrl.StartsWith(TEXT("http://127.0.0.1")) ||
		SecurityApiBaseUrl.StartsWith(TEXT("http://localhost")) ||
		(!AllowedInsecureVpnSecurityApiBaseUrl.IsEmpty() &&
			SecurityApiBaseUrl.Equals(AllowedInsecureVpnSecurityApiBaseUrl, ESearchCase::IgnoreCase));
}

bool UProjectProject01GameInstance::ConfigureMultiplayerServerAddress(
	const FString& ServerAddress,
	const bool bUseAutomaticConnection,
	FString& OutError)
{
	OutError.Reset();
	FString Address = bUseAutomaticConnection ? AutomaticMultiplayerServerAddress : ServerAddress;
	if (!ProjectProject01NetworkSecurity::ParseIpv4Address(Address, Address) ||
		!ProjectProject01NetworkSecurity::IsSupportedTestAddress(Address))
	{
		OutError = TEXT("서버 IP는 25.x.x.x 또는 사설 IPv4(10.x, 172.16~31.x, 192.168.x) 주소여야 합니다.");
		return false;
	}

	const FString ApiUrl = FString::Printf(TEXT("http://%s:5080"), *Address);
	UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>();
	UProjectProject01LobbySubsystem* Lobby = GetSubsystem<UProjectProject01LobbySubsystem>();
	if (!IsValid(Auth) || !IsValid(Lobby))
	{
		OutError = TEXT("멀티플레이 연결 시스템을 찾지 못했습니다.");
		return false;
	}
	if (Auth->GetAuthState() != EProjectProject01AuthState::SignedOut ||
		Lobby->IsRequestInFlight() || Lobby->HasCurrentRoom())
	{
		OutError = TEXT("로그인 또는 경기 진행 중에는 서버 연결 방식을 바꿀 수 없습니다.");
		return false;
	}
	if (!Auth->ConfigureLocalTestApiEndpoint(ApiUrl) || !Lobby->ConfigureLocalTestApiEndpoint(ApiUrl))
	{
		OutError = TEXT("로그인 또는 경기 진행 중에는 서버 연결 방식을 바꿀 수 없습니다.");
		return false;
	}

	SecurityApiBaseUrl = ApiUrl;
	AllowedInsecureVpnSecurityApiBaseUrl = ApiUrl;
	RuntimeMultiplayerServerAddress = Address;
	return true;
}

FString UProjectProject01GameInstance::GetConfiguredMultiplayerServerAddress() const
{
	return RuntimeMultiplayerServerAddress;
}

void UProjectProject01GameInstance::RemovePendingRequest(
	const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& Request)
{
	PendingSecurityRequests.Remove(Request);
}
