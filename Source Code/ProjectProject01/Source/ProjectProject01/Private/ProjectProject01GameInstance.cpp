// File: Source/ProjectProject01/Private/ProjectProject01GameInstance.cpp
// Build target: ProjectProject01 / ProjectProject01Server, Unreal Engine 5.8.2

#include "ProjectProject01GameInstance.h"
#include "MultiplayTestGameMode.h"
#include "ProjectProject01CrashFiles.h"
#include "ProjectProject01ResultOutbox.h"
#include "ProjectProject01TravelDeadline.h"

#include "Containers/Ticker.h"

#include "Dom/JsonObject.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
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
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/NetworkVersion.h"
#include "ProjectProject01DiagnosticsSubsystem.h"
#include "ProjectProject01AuthSubsystem.h"
#include "ProjectProject01LobbySubsystem.h"
#include "ProjectProject01VersionContract.h"
#include "ProjectProject01VoiceChatSubsystem.h"
#include "Rendering/RenderingCommon.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "Widgets/Layout/SBorder.h"

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

void UProjectProject01GameUserSettings::MarkSurvivorGuideSeen()
{
	if (!bHasSeenSurvivorGuide)
	{
		bHasSeenSurvivorGuide = true;
		SaveSettings();
	}
}

void UProjectProject01GameUserSettings::MarkMannequinGuideSeen()
{
	if (!bHasSeenMannequinGuide)
	{
		bHasSeenMannequinGuide = true;
		SaveSettings();
	}
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
	DirectServerAddress.Reset();
	MoveForwardKeyName = EKeys::W.GetFName();
	MoveBackwardKeyName = EKeys::S.GetFName();
	MoveLeftKeyName = EKeys::A.GetFName();
	MoveRightKeyName = EKeys::D.GetFName();
	SprintKeyName = EKeys::LeftShift.GetFName();
	VoicePushToTalkKeyName = EKeys::F2.GetFName();
	VoiceToggleKeyName = EKeys::U.GetFName();
	HelpPingKeyName = EKeys::E.GetFName();
	DangerPingKeyName = EKeys::R.GetFName();
	LocationPingKeyName = EKeys::T.GetFName();
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
	// 기존 빌드의 기본 생존자 핑 Z/X/C만 새 기본 E/R/T로 한 번 이동한다.
	// 사용자가 다른 키로 직접 지정한 조합은 보존한다.
	if (HelpPingKeyName == EKeys::Z.GetFName() &&
		DangerPingKeyName == EKeys::X.GetFName() &&
		LocationPingKeyName == EKeys::C.GetFName())
	{
		HelpPingKeyName = EKeys::E.GetFName();
		DangerPingKeyName = EKeys::R.GetFName();
		LocationPingKeyName = EKeys::T.GetFName();
	}

	TArray<FKey> Keys = {
		GetValidatedKey(MoveForwardKeyName, EKeys::W),
		GetValidatedKey(MoveBackwardKeyName, EKeys::S),
		GetValidatedKey(MoveLeftKeyName, EKeys::A),
		GetValidatedKey(MoveRightKeyName, EKeys::D),
		GetValidatedKey(SprintKeyName, EKeys::LeftShift),
		GetValidatedKey(VoicePushToTalkKeyName, EKeys::F2),
		GetValidatedKey(VoiceToggleKeyName, EKeys::U),
		GetValidatedKey(HelpPingKeyName, EKeys::E),
		GetValidatedKey(DangerPingKeyName, EKeys::R),
		GetValidatedKey(LocationPingKeyName, EKeys::T)
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
		HelpPingKeyName = EKeys::E.GetFName();
		DangerPingKeyName = EKeys::R.GetFName();
		LocationPingKeyName = EKeys::T.GetFName();
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
		AudioDevice->SetSoundMixClassOverride(
			RuntimeSoundMix, RuntimeSFXSoundClass,
			SFXVolume * FMath::Clamp(GameplaySFXMultiplier, 0.0f, 1.0f), 1.0f, 0.05f, true);
		AudioDevice->SetSoundMixClassOverride(RuntimeSoundMix, RuntimeMusicSoundClass, MusicVolume, 1.0f, 0.05f, true);
		AudioDevice->SetSoundMixClassOverride(RuntimeSoundMix, RuntimeUISoundClass, UIVolume, 1.0f, 0.05f, true);
	}
}

void UProjectProject01GameUserSettings::FadeGameplaySFX(
	const float TargetMultiplier, const float FadeSeconds)
{
	GameplaySFXMultiplier = FMath::Clamp(TargetMultiplier, 0.0f, 1.0f);
	EnsureRuntimeAudioObjects();
	if (!IsValid(GEngine))
	{
		return;
	}
	FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
	if (!AudioDevice.IsValid() || !IsValid(RuntimeSoundMix) || !IsValid(RuntimeSFXSoundClass))
	{
		return;
	}
	if (!bRuntimeSoundMixPushed)
	{
		AudioDevice->PushSoundMixModifier(RuntimeSoundMix);
		bRuntimeSoundMixPushed = true;
	}
	AudioDevice->SetSoundMixClassOverride(
		RuntimeSoundMix, RuntimeSFXSoundClass,
		SFXVolume * GameplaySFXMultiplier, 1.0f, FMath::Max(FadeSeconds, 0.0f), true);
}

void UProjectProject01GameUserSettings::RestoreGameplaySFX(const float FadeSeconds)
{
	FadeGameplaySFX(1.0f, FadeSeconds);
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
FKey UProjectProject01GameUserSettings::GetHelpPingKey() const { return GetValidatedKey(HelpPingKeyName, EKeys::E); }
FKey UProjectProject01GameUserSettings::GetDangerPingKey() const { return GetValidatedKey(DangerPingKeyName, EKeys::R); }
FKey UProjectProject01GameUserSettings::GetLocationPingKey() const { return GetValidatedKey(LocationPingKeyName, EKeys::T); }

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
	RuntimeMultiplayerServerAddress.Reset();
	Super::Init();
    BindEncryptionDispatchers();
	if (!IsRunningDedicatedServer())
	{
		if (UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>(); IsValid(Auth))
		{
			Auth->OnSessionInvalidated.AddUniqueDynamic(
				this, &UProjectProject01GameInstance::HandleAuthenticationSessionInvalidated);
		}
	}
	if (UProjectProject01GameUserSettings* Settings = UProjectProject01GameUserSettings::Get(); IsValid(Settings))
	{
		Settings->LoadSettings(false);
		FString LauncherServerAddress;
		if (FParse::Value(FCommandLine::Get(), TEXT("ProjectProject01Server="), LauncherServerAddress))
		{
			LauncherServerAddress.TrimStartAndEndInline();
			if (!LauncherServerAddress.IsEmpty())
			{
				Settings->SetDirectServerAddress(LauncherServerAddress);
				Settings->SaveSettings();
			}
		}
		Settings->ApplyProjectSettings(true);
		FString EndpointError;
		if (!Settings->GetDirectServerAddress().IsEmpty() &&
			!ConfigureMultiplayerServerAddress(Settings->GetDirectServerAddress(), EndpointError))
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
	if (!IsRunningDedicatedServer())
	{
		EntireScreenFadeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UProjectProject01GameInstance::TickEntireScreenFade));
	}
}

void UProjectProject01GameInstance::Shutdown()
{
    CancelLevelTransitionWatchdog();
	if (EntireScreenFadeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EntireScreenFadeTickerHandle);
		EntireScreenFadeTickerHandle.Reset();
	}
	if (bEntireScreenFadeAttached && EntireScreenFadeOverlay.IsValid())
	{
		if (UGameViewportClient* FadeViewport = EntireScreenFadeViewport.Get(); IsValid(FadeViewport))
		{
			FadeViewport->RemoveViewportWidgetContent(EntireScreenFadeOverlay.ToSharedRef());
		}
	}
	bEntireScreenFadeAttached = false;
	EntireScreenFadeViewport.Reset();
	EntireScreenFadeOverlay.Reset();
	if (CrashUploadRetryTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CrashUploadRetryTickerHandle);
		CrashUploadRetryTickerHandle.Reset();
	}
	if (UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>(); IsValid(Auth))
	{
		Auth->OnSessionInvalidated.RemoveDynamic(
			this, &UProjectProject01GameInstance::HandleAuthenticationSessionInvalidated);
		Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01GameInstance::HandleSafeLogoutCompleted);
	}
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
    // Base Shutdown unbinds global delegates even when another PIE world is alive.
    if(GEngine)for(const auto& Context:GEngine->GetWorldContexts())
        if(Context.OwningGameInstance!=this&&Cast<UProjectProject01GameInstance>(Context.OwningGameInstance.Get())){BindEncryptionDispatchers();break;}
}

UProjectProject01GameInstance* UProjectProject01GameInstance::ResolveEncryptionOwner(const UObject* Owner)
{
    if(const auto* Connection=Cast<UNetConnection>(Owner))
        return Connection->Driver&&Connection->Driver->GetWorld()?Cast<UProjectProject01GameInstance>(Connection->Driver->GetWorld()->GetGameInstance()):nullptr;
    if(GEngine&&Cast<UPendingNetGame>(Owner))for(const auto& Context:GEngine->GetWorldContexts())
        if(Context.PendingNetGame==Owner)return Cast<UProjectProject01GameInstance>(Context.OwningGameInstance.Get());
    return nullptr;
}
void UProjectProject01GameInstance::BindEncryptionDispatchers()
{
    FNetDelegates::OnReceivedNetworkEncryptionToken.BindLambda([](const FString& Token,const FOnEncryptionKeyResponse& Callback)
    {
        if(auto* Owner=ResolveEncryptionOwner(Callback.GetUObject()))Owner->ReceivedNetworkEncryptionToken(Token,Callback);
        else ProjectProject01NetworkSecurity::CompleteEncryptionFailure(Callback,EEncryptionResponse::Failure,TEXT("Encryption connection owner is unavailable."));
    });
    FNetDelegates::OnReceivedNetworkEncryptionAck.BindLambda([](const FOnEncryptionKeyResponse& Callback)
    {
        if(auto* Owner=ResolveEncryptionOwner(Callback.GetUObject()))Owner->ReceivedNetworkEncryptionAck(Callback);
        else ProjectProject01NetworkSecurity::CompleteEncryptionFailure(Callback,EEncryptionResponse::Failure,TEXT("Encryption connection owner is unavailable."));
    });
}

void UProjectProject01GameInstance::InitializeCrashReportCollection()
{
	// PIE game instances share one Saved directory and are not independent game processes.
	if (GIsEditor) { ProjectProject01CrashFiles::ExcludeEditorReports(); return; }
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
	CrashUploadRetryTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			RecoverPendingCrashReports();
			return true;
		}), 30.0f);
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
	Json->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
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
	for (const FString& FileName : ActiveFiles)
	{
		const FString Source = FPaths::Combine(Directory, FileName);
		if (!ActiveCrashMarkerPath.IsEmpty() && Source.Equals(ActiveCrashMarkerPath, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FString Stored;
		TSharedPtr<FJsonObject> Marker;
		if (!FFileHelper::LoadFileToString(Stored, *Source) ||
			!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Stored), Marker) || !Marker.IsValid()) continue;
		int32 ProcessId = 0;
		Marker->TryGetNumberField(TEXT("processId"), ProcessId);
		if (ProcessId == 0)
		{
			TArray<FString> Parts; FileName.ParseIntoArray(Parts, TEXT("-"));
			if (Parts.Num() > 1) ProcessId = FCString::Atoi(*Parts[1]);
		}
		if (ProcessId > 0 && FPlatformProcess::IsApplicationRunning(static_cast<uint32>(ProcessId))) continue;
		FString ReportId; Marker->TryGetStringField(TEXT("reportId"), ReportId);
		FString MapName; Marker->TryGetStringField(TEXT("mapName"), MapName);
		if (MapName.StartsWith(TEXT("UEDPIE_"))) { ProjectProject01CrashFiles::ExcludeReport(Source); continue; }
		if (ProjectProject01CrashFiles::FindNativeCrashDirectory(ReportId).IsEmpty())
		{
			if(ProjectProject01CrashFiles::AwaitNativeEvidence(Source,Marker))continue;
			// Preserve excluded records for inspection without presenting them as confirmed crashes.
			ProjectProject01CrashFiles::ExcludeReport(Source);
			continue;
		}
		const FString Destination = FPaths::Combine(Directory, TEXT("Pending-") + FileName.RightChop(7));
		FileManager.Move(*Destination, *Source, true, true, false, true);
	}

	TArray<FString> PendingFiles;
	FileManager.FindFiles(PendingFiles, *FPaths::Combine(Directory, TEXT("Pending-*.json")), true, false);
	for (const FString& FileName : PendingFiles)
	{
		const FString Path = FPaths::Combine(Directory, FileName);
		FString Stored; TSharedPtr<FJsonObject> Marker; FString ReportId;
		if (FFileHelper::LoadFileToString(Stored, *Path) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Stored), Marker) && Marker.IsValid())
			Marker->TryGetStringField(TEXT("reportId"), ReportId);
		FString MapName;
		if (Marker.IsValid()) Marker->TryGetStringField(TEXT("mapName"), MapName);
		if (MapName.StartsWith(TEXT("UEDPIE_")))
			ProjectProject01CrashFiles::ExcludeReport(Path);
		else if(ProjectProject01CrashFiles::FindNativeCrashDirectory(ReportId).IsEmpty())
        {if(!ProjectProject01CrashFiles::AwaitNativeEvidence(Path,Marker))ProjectProject01CrashFiles::ExcludeReport(Path);}
		else if (CrashReportsInFlight.Num() < 2) UploadPendingCrashReport(Path);
	}
}

void UProjectProject01GameInstance::UploadPendingCrashReport(const FString& ReportPath)
{
	if (!IsBackendUrlAllowed() || CrashReportsInFlight.Contains(ReportPath)) return;
	FString Stored;
	if (!FFileHelper::LoadFileToString(Stored, *ReportPath)) return;
	const FString Body = ProjectProject01CrashReports::SanitizeStoredReport(Stored);
	if (Body.IsEmpty()) return;
	TSharedPtr<FJsonObject> Marker; FString ReportId;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Stored), Marker) || !Marker.IsValid() ||
		!Marker->TryGetStringField(TEXT("reportId"), ReportId)) return;
	const FString CrashDirectory = ProjectProject01CrashFiles::FindNativeCrashDirectory(ReportId);
	// The crash reporter may still be copying the dump/log after the game process exits.
	if (CrashDirectory.IsEmpty() || (FDateTime::UtcNow() - IFileManager::Get().GetTimeStamp(
		*FPaths::Combine(CrashDirectory, TEXT("CrashContext.runtime-xml")))).GetTotalSeconds() < 15.0) return;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/crash-reports"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->SetContentAsString(Body);
	CrashReportsInFlight.Add(ReportPath);
	const TWeakObjectPtr<UProjectProject01GameInstance> WeakThis(this);
	const TWeakPtr<IHttpRequest, ESPMode::ThreadSafe> WeakRequest(Request);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, WeakRequest, ReportPath](FHttpRequestPtr, FHttpResponsePtr Response, const bool bSucceeded)
		{
			if (UProjectProject01GameInstance* Instance = WeakThis.Get(); IsValid(Instance))
			{
				Instance->RemovePendingRequest(WeakRequest.Pin());
				Instance->CrashReportsInFlight.Remove(ReportPath);
				if (bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
				{
					FString Stored; TSharedPtr<FJsonObject> Marker; FString ReportId;
					if (FFileHelper::LoadFileToString(Stored, *ReportPath) &&
						FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Stored), Marker) && Marker.IsValid() &&
						Marker->TryGetStringField(TEXT("reportId"), ReportId))
					{
						Instance->CrashReportsInFlight.Add(ReportPath);
						TSharedPtr<FJsonObject> Policy;bool bAllowDump=false;
						if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Policy) && Policy)
							Policy->TryGetBoolField(TEXT("allowMinidumpUploads"),bAllowDump);
						TArray<FString> Files=ProjectProject01CrashFiles::FindAttachments(ProjectProject01CrashFiles::FindNativeCrashDirectory(ReportId));
						if(!bAllowDump)Files.RemoveAll([](const FString& Path){return ProjectProject01CrashFiles::AttachmentName(Path)==TEXT("Crash.dmp");});
						Instance->UploadCrashAttachment(ReportPath, ReportId,Files,0);
					}
				}
			}
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		PendingSecurityRequests.Remove(Request);
		CrashReportsInFlight.Remove(ReportPath);
	}
}

void UProjectProject01GameInstance::UploadCrashAttachment(const FString& ReportPath, const FString& ReportId,
	const TArray<FString>& Files, const int32 Index)
{
	if (!Files.IsValidIndex(Index))
	{
		CrashReportsInFlight.Remove(ReportPath);
		if (!Files.IsEmpty())
		{
			IFileManager::Get().Delete(*ReportPath, false, true, true);
			UE_LOG(LogProjectProject01NetworkSecurity, Log, TEXT("Crash report %s and %d attachments uploaded."), *ReportId, Files.Num());
		}
		return;
	}
	const FString& Path = Files[Index];
	TArray<uint8> Content;
	if (ProjectProject01CrashFiles::AttachmentName(Path) == TEXT("Crash.dmp"))
		FFileHelper::LoadFileToArray(Content, *Path);
	else
	{
		FString Log; FFileHelper::LoadFileToString(Log, *Path);
		const FTCHARToUTF8 Utf8(*ProjectProject01CrashFiles::ScrubText(MoveTemp(Log)));
		Content.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	if (Content.IsEmpty()) { CrashReportsInFlight.Remove(ReportPath); return; }
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/crash-reports/") + ReportId + TEXT("/artifacts/") + ProjectProject01CrashFiles::AttachmentName(Path));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/octet-stream"));
	Request->SetTimeout(60.0f);
	Request->SetContent(MoveTemp(Content));
	const TWeakObjectPtr<UProjectProject01GameInstance> WeakThis(this);
	const TWeakPtr<IHttpRequest, ESPMode::ThreadSafe> WeakRequest(Request);
	Request->OnProcessRequestComplete().BindLambda([WeakThis, WeakRequest, ReportPath, ReportId, Files, Index]
		(FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		if (UProjectProject01GameInstance* Instance = WeakThis.Get(); IsValid(Instance))
		{
			Instance->RemovePendingRequest(WeakRequest.Pin());
			if (bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
				Instance->UploadCrashAttachment(ReportPath, ReportId, Files, Index + 1);
			else
			{
				Instance->CrashReportsInFlight.Remove(ReportPath);
				UE_LOG(LogProjectProject01NetworkSecurity, Warning, TEXT("Crash attachment upload failed (HTTP %d); kept for retry."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
			}
		}
	});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		PendingSecurityRequests.Remove(Request);
		CrashReportsInFlight.Remove(ReportPath);
	}
}

void UProjectProject01GameInstance::FetchActivePlayerSurvey(
	TFunction<void(bool, bool, const FProjectProject01SurveyDefinition&, const FString&)> Completion)
{
	const UProjectProject01AuthSubsystem* SurveyAuth = GetSubsystem<UProjectProject01AuthSubsystem>();
	const uint64 SessionGeneration=SurveyAuth?SurveyAuth->GetSessionGeneration():0;
	const TWeakObjectPtr<UWorld> SurveyWorld(GetWorld());
	if (!IsValid(SurveyAuth) || !SurveyAuth->IsSignedIn())
	{
		Completion(true, false, FProjectProject01SurveyDefinition(), TEXT("로그인 후 설문에 참여할 수 있습니다."));
		return;
	}
	if (!IsBackendUrlAllowed())
	{
		Completion(false, false, FProjectProject01SurveyDefinition(), TEXT("설문 서버 주소가 허용되지 않습니다."));
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/surveys/active"));
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + SurveyAuth->GetAccessTokenForAuthenticatedRequest());
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	const TWeakObjectPtr<UProjectProject01GameInstance> WeakThis(this);
	const TWeakPtr<IHttpRequest, ESPMode::ThreadSafe> WeakRequest(Request);
	TSharedRef<TFunction<void(bool, bool, const FProjectProject01SurveyDefinition&, const FString&)>> CompletionRef =
		MakeShared<TFunction<void(bool, bool, const FProjectProject01SurveyDefinition&, const FString&)>>(MoveTemp(Completion));
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, WeakRequest, CompletionRef,SessionGeneration,SurveyWorld](FHttpRequestPtr, FHttpResponsePtr Response, const bool bSucceeded)
		{
            auto* Instance=WeakThis.Get();if(!Instance)return;
            Instance->RemovePendingRequest(WeakRequest.Pin());
            if(!Instance->IsSurveySessionCurrent(SessionGeneration,SurveyWorld.Get()))return;
			if (bSucceeded && Response.IsValid() && Response->GetResponseCode() == 404)
			{
				(*CompletionRef)(true, false, FProjectProject01SurveyDefinition(), TEXT("현재 진행 중인 설문이 없습니다."));
				return;
			}
			if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
			{
				(*CompletionRef)(false, false, FProjectProject01SurveyDefinition(), TEXT("설문 상태를 확인하지 못했습니다."));
				return;
			}
			TSharedPtr<FJsonObject> Root;
			FProjectProject01SurveyDefinition Survey;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Root) ||
				!Root.IsValid() || !Root->TryGetStringField(TEXT("surveyId"), Survey.SurveyId) ||
				!Root->TryGetStringField(TEXT("title"), Survey.Title))
			{
				(*CompletionRef)(false, false, Survey, TEXT("설문 정보를 읽지 못했습니다."));
				return;
			}
			Root->TryGetStringField(TEXT("description"), Survey.Description);
			const TArray<TSharedPtr<FJsonValue>>* QuestionValues = nullptr;
			if (!Root->TryGetArrayField(TEXT("questions"), QuestionValues) || !QuestionValues)
			{
				(*CompletionRef)(false, false, Survey, TEXT("설문 문항을 읽지 못했습니다."));
				return;
			}
			for (const TSharedPtr<FJsonValue>& QuestionValue : *QuestionValues)
			{
				const TSharedPtr<FJsonObject> QuestionJson = QuestionValue.IsValid() ? QuestionValue->AsObject() : nullptr;
				if (!QuestionJson.IsValid()) continue;
				FProjectProject01SurveyQuestion Question;
				if (!QuestionJson->TryGetStringField(TEXT("id"), Question.Id) ||
					!QuestionJson->TryGetStringField(TEXT("type"), Question.Type) ||
					!QuestionJson->TryGetStringField(TEXT("prompt"), Question.Prompt)) continue;
				QuestionJson->TryGetBoolField(TEXT("required"), Question.bRequired);
				QuestionJson->TryGetBoolField(TEXT("allowMultiple"), Question.bAllowMultiple);
				QuestionJson->TryGetNumberField(TEXT("maxSelections"), Question.MaxSelections);
				const TArray<TSharedPtr<FJsonValue>>* OptionValues = nullptr;
				if (QuestionJson->TryGetArrayField(TEXT("options"), OptionValues) && OptionValues)
				{
					for (const TSharedPtr<FJsonValue>& Option : *OptionValues)
					{
						if (Option.IsValid()) Question.Options.Add(Option->AsString());
					}
				}
				Survey.Questions.Add(MoveTemp(Question));
			}
			const bool bValid = Survey.IsValid();
			(*CompletionRef)(bValid, bValid, Survey,
				bValid ? TEXT("활성 설문을 확인했습니다.") : TEXT("설문 문항이 비어 있습니다."));
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		PendingSecurityRequests.Remove(Request);
		(*CompletionRef)(false, false, FProjectProject01SurveyDefinition(), TEXT("설문 확인 요청을 시작하지 못했습니다."));
	}
}

void UProjectProject01GameInstance::SubmitPlayerSurvey(
	const FProjectProject01SurveyDefinition& Survey,
	const TArray<FProjectProject01SurveyAnswer>& Answers,
	TFunction<void(bool, const FString&)> Completion)
{
	const UProjectProject01AuthSubsystem* SurveyAuth = GetSubsystem<UProjectProject01AuthSubsystem>();
	const uint64 SessionGeneration=SurveyAuth?SurveyAuth->GetSessionGeneration():0;
	const TWeakObjectPtr<UWorld> SurveyWorld(GetWorld());
	if (!IsValid(SurveyAuth) || !SurveyAuth->IsSignedIn())
	{
		Completion(false, TEXT("로그인 후 설문에 참여해주세요."));
		return;
	}
	if (!IsBackendUrlAllowed() || !Survey.IsValid() || Answers.IsEmpty())
	{
		Completion(false, TEXT("설문 답변을 확인해주세요."));
		return;
	}
	UWorld* World = GetWorld();
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("responseId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	Json->SetStringField(TEXT("submittedAtUtc"), FDateTime::UtcNow().ToIso8601());
	TArray<TSharedPtr<FJsonValue>> AnswerValues;
	for (const FProjectProject01SurveyAnswer& Answer : Answers)
	{
		TSharedRef<FJsonObject> AnswerJson = MakeShared<FJsonObject>();
		AnswerJson->SetStringField(TEXT("questionId"), Answer.QuestionId);
		AnswerJson->SetStringField(TEXT("value"), Answer.Value.TrimStartAndEnd());
		TArray<TSharedPtr<FJsonValue>> Selected;
		for (const FString& Value : Answer.Values) Selected.Add(MakeShared<FJsonValueString>(Value));
		AnswerJson->SetArrayField(TEXT("values"), MoveTemp(Selected));
		AnswerValues.Add(MakeShared<FJsonValueObject>(AnswerJson));
	}
	Json->SetArrayField(TEXT("answers"), MoveTemp(AnswerValues));
	Json->SetStringField(TEXT("buildVersion"), ProjectProject01CrashReports::GetBuildVersion());
	Json->SetStringField(TEXT("mapName"), IsValid(World)
		? ProjectProject01CrashReports::LimitField(World->GetMapName()) : TEXT("Unavailable"));
	Json->SetStringField(TEXT("processRole"), ProjectProject01CrashReports::GetProcessRole(World));
	Json->SetStringField(TEXT("playerRole"), ProjectProject01CrashReports::LimitField(PendingCrashPlayerRole));
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/surveys/") + Survey.SurveyId + TEXT("/responses"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (const UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>();
		IsValid(Auth) && Auth->IsSignedIn())
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Auth->GetAccessTokenForAuthenticatedRequest());
	}
	Request->SetTimeout(SecurityRequestTimeoutSeconds);
	Request->SetContentAsString(Body);
	const TWeakObjectPtr<UProjectProject01GameInstance> WeakThis(this);
	const TWeakPtr<IHttpRequest, ESPMode::ThreadSafe> WeakRequest(Request);
	TSharedRef<TFunction<void(bool, const FString&)>> CompletionRef =
		MakeShared<TFunction<void(bool, const FString&)>>(MoveTemp(Completion));
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, WeakRequest, CompletionRef,SessionGeneration,SurveyWorld](FHttpRequestPtr, FHttpResponsePtr Response, const bool bSucceeded)
		{
            auto* Instance=WeakThis.Get();if(!Instance)return;
            Instance->RemovePendingRequest(WeakRequest.Pin());
            if(!Instance->IsSurveySessionCurrent(SessionGeneration,SurveyWorld.Get()))return;
			const bool bAccepted = bSucceeded && Response.IsValid() &&
				Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300;
			FString ResponseMessage = bAccepted
				? TEXT("설문 응답을 전송했습니다. 감사합니다.")
				: TEXT("서버에 설문 응답을 전송하지 못했습니다. 잠시 후 다시 시도해주세요.");
			if (Response.IsValid())
			{
				TSharedPtr<FJsonObject> ResponseJson;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), ResponseJson) &&
					ResponseJson.IsValid())
				{
					ResponseJson->TryGetStringField(TEXT("message"), ResponseMessage);
				}
			}
			(*CompletionRef)(bAccepted, ResponseMessage);
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		PendingSecurityRequests.Remove(Request);
		(*CompletionRef)(false, TEXT("설문 전송 요청을 시작하지 못했습니다."));
	}
}

void UProjectProject01GameInstance::MarkPendingSinglePlayerLevelTransitionFade()
{
	bPendingSinglePlayerLevelTransitionFade = true;
}

void UProjectProject01GameInstance::CancelLevelTransitionWatchdog()
{
    FTSTicker::GetCoreTicker().RemoveTicker(LevelTransitionWatchdog);LevelTransitionWatchdog.Reset();
}
void UProjectProject01GameInstance::ArmLevelTransitionWatchdog()
{
    CancelLevelTransitionWatchdog();const double Start=FPlatformTime::Seconds();const TWeakObjectPtr<UWorld> Original(GetWorld());
    // Core ticker belongs to the GameInstance, not a trigger in the disappearing old world.
    LevelTransitionWatchdog=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this,Start,Original](float)
    {
        auto* World=GetWorld();const auto* Context=GEngine?GEngine->GetWorldContextFromWorld(World):nullptr;
        const bool Async=Context&&Context->SeamlessTravelHandler.IsInTransition();
        const auto Action=FProjectTravelDeadline::Evaluate(FPlatformTime::Seconds()-Start,Async,World!=Original.Get());
        if(Action==EProjectTravelDeadline::Abort){AbortStalledLevelTravel();return false;}
        return Action!=EProjectTravelDeadline::Complete&&FPlatformTime::Seconds()-Start<121;
    }),1.0f);
}
void UProjectProject01GameInstance::AbortStalledLevelTravel()
{
    ServerMatchCarryover.Reset();ConsumePendingSinglePlayerLevelTransitionFade();
    UWorld* World=GetWorld();
    if(World)
    {
        World->NextURL.Reset();
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            if(auto* PC=It->Get())PC->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("맵 이동 시간이 초과되었습니다. 다시 접속하세요.")));
    }
    UE_LOG(LogProjectProject01NetworkSecurity,Warning,TEXT("Async level transition exceeded 120 seconds; closing this match safely."));
    if(IsRunningDedicatedServer())FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){FPlatformMisc::RequestExit(false);return false;}),1.0f);
    else if(GEngine&&World)GEngine->HandleDisconnect(World,World->GetNetDriver());
}

bool UProjectProject01GameInstance::IsSurveySessionCurrent(uint64 Generation,const UWorld* World) const
{
    const auto* Auth=GetSubsystem<UProjectProject01AuthSubsystem>();
    return Auth&&Auth->IsSignedIn()&&Auth->GetSessionGeneration()==Generation&&GetWorld()==World;
}
bool UProjectProject01GameInstance::IsMultiplayerFailureWorld(const UWorld* World) const
{
    if(!World||World->GetGameInstance()!=this)return false;
    const auto* Lobby=GetSubsystem<UProjectProject01LobbySubsystem>();
    return World->GetNetMode()==NM_Client||!PendingClientTicket.IsEmpty()||
        (Lobby&&Lobby->HasCurrentRoom()&&(Lobby->GetCurrentRoom().bStarting||Lobby->GetCurrentRoom().bStarted||Lobby->GetCurrentRoom().bServerPreparing));
}

bool UProjectProject01GameInstance::ConsumePendingSinglePlayerLevelTransitionFade()
{
	const bool bWasPending = bPendingSinglePlayerLevelTransitionFade;
	bPendingSinglePlayerLevelTransitionFade = false;
	return bWasPending;
}

void UProjectProject01GameInstance::EnsureEntireScreenFadeOverlay()
{
	if (IsRunningDedicatedServer())
	{
		return;
	}
	if (!EntireScreenFadeOverlay.IsValid())
	{
		SAssignNew(EntireScreenFadeOverlay, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor::Transparent);
	}
	UGameViewportClient* CurrentViewport = GetGameViewportClient();
	if (bEntireScreenFadeAttached && EntireScreenFadeViewport.Get() != CurrentViewport)
	{
		if (UGameViewportClient* PreviousViewport = EntireScreenFadeViewport.Get();
			IsValid(PreviousViewport))
		{
			PreviousViewport->RemoveViewportWidgetContent(EntireScreenFadeOverlay.ToSharedRef());
		}
		bEntireScreenFadeAttached = false;
		EntireScreenFadeViewport.Reset();
	}
	if (!bEntireScreenFadeAttached && IsValid(CurrentViewport))
	{
		// 일반 AddToViewport 위젯보다 높은 계층에 붙여 메뉴와 안내 UI까지 한 화면처럼 덮는다.
		CurrentViewport->AddViewportWidgetContent(EntireScreenFadeOverlay.ToSharedRef(), 1000000);
		EntireScreenFadeViewport = CurrentViewport;
		bEntireScreenFadeAttached = true;
	}
}

void UProjectProject01GameInstance::ApplyEntireScreenFadeOpacity()
{
	EnsureEntireScreenFadeOverlay();
	if (!EntireScreenFadeOverlay.IsValid())
	{
		return;
	}
	EntireScreenFadeOpacity = FMath::Clamp(EntireScreenFadeOpacity, 0.0f, 1.0f);
	EntireScreenFadeOverlay->SetBorderBackgroundColor(
		FLinearColor(0.0f, 0.0f, 0.0f, EntireScreenFadeOpacity));
	EntireScreenFadeOverlay->SetVisibility(EntireScreenFadeOpacity > UE_KINDA_SMALL_NUMBER
		? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

bool UProjectProject01GameInstance::TickEntireScreenFade(const float DeltaSeconds)
{
	if (bEntireScreenFadeAnimating || EntireScreenFadeOpacity > 0.0f)
	{
		// 트래블 중 GameViewportClient가 교체되면 기존 attached 플래그만 믿지 말고
		// 새 뷰포트에 즉시 다시 붙여 밝은 로딩 프레임이 노출되지 않게 한다.
		EnsureEntireScreenFadeOverlay();
		ApplyEntireScreenFadeOpacity();
	}
	if (!bEntireScreenFadeAnimating)
	{
		return true;
	}
	EntireScreenFadeElapsedSeconds = FMath::Min(
		EntireScreenFadeDurationSeconds,
		// A loading hitch must not consume the whole visible fade in a single frame.
		EntireScreenFadeElapsedSeconds + FMath::Clamp(DeltaSeconds, 0.0f, 0.05f));
	const float Progress = EntireScreenFadeDurationSeconds > UE_KINDA_SMALL_NUMBER
		? EntireScreenFadeElapsedSeconds / EntireScreenFadeDurationSeconds : 1.0f;
	const float SmoothedProgress = FMath::InterpEaseInOut(0.0f, 1.0f,
		FMath::Clamp(Progress, 0.0f, 1.0f), 2.0f);
	EntireScreenFadeOpacity = FMath::Lerp(
		EntireScreenFadeStartOpacity, EntireScreenFadeTargetOpacity,
		SmoothedProgress);
	if (Progress >= 1.0f)
	{
		bEntireScreenFadeAnimating = false;
		EntireScreenFadeOpacity = EntireScreenFadeTargetOpacity;
	}
	ApplyEntireScreenFadeOpacity();
	return true;
}

void UProjectProject01GameInstance::FadeEntireScreenToBlack(const float DurationSeconds)
{
	EnsureEntireScreenFadeOverlay();
	EntireScreenFadeStartOpacity = EntireScreenFadeOpacity;
	EntireScreenFadeTargetOpacity = 1.0f;
	EntireScreenFadeElapsedSeconds = 0.0f;
	EntireScreenFadeDurationSeconds = FMath::Max(0.0f, DurationSeconds);
	bEntireScreenFadeAnimating = EntireScreenFadeDurationSeconds > UE_KINDA_SMALL_NUMBER;
	if (!bEntireScreenFadeAnimating)
	{
		EntireScreenFadeOpacity = 1.0f;
	}
	ApplyEntireScreenFadeOpacity();
}

void UProjectProject01GameInstance::HoldEntireScreenBlack()
{
	EnsureEntireScreenFadeOverlay();
	bEntireScreenFadeAnimating = false;
	EntireScreenFadeOpacity = 1.0f;
	EntireScreenFadeStartOpacity = 1.0f;
	EntireScreenFadeTargetOpacity = 1.0f;
	ApplyEntireScreenFadeOpacity();
}

void UProjectProject01GameInstance::FadeEntireScreenFromBlack(const float DurationSeconds)
{
	EnsureEntireScreenFadeOverlay();
	// 새 월드 첫 프레임이 밝게 비치는 일을 막기 위해 해제는 언제나 완전한 검정에서 시작한다.
	EntireScreenFadeOpacity = 1.0f;
	EntireScreenFadeStartOpacity = 1.0f;
	EntireScreenFadeTargetOpacity = 0.0f;
	EntireScreenFadeElapsedSeconds = 0.0f;
	EntireScreenFadeDurationSeconds = FMath::Max(0.0f, DurationSeconds);
	bEntireScreenFadeAnimating = EntireScreenFadeDurationSeconds > UE_KINDA_SMALL_NUMBER;
	if (!bEntireScreenFadeAnimating)
	{
		EntireScreenFadeOpacity = 0.0f;
	}
	ApplyEntireScreenFadeOpacity();
}

void UProjectProject01GameInstance::ClearEntireScreenFade()
{
	bEntireScreenFadeAnimating = false;
	EntireScreenFadeOpacity = 0.0f;
	EntireScreenFadeStartOpacity = 0.0f;
	EntireScreenFadeTargetOpacity = 0.0f;
	ApplyEntireScreenFadeOpacity();
}

void UProjectProject01GameInstance::HandleNetworkFailure(
	UWorld* World,
	UNetDriver* NetDriver,
	const ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	if (IsRunningDedicatedServer() || !IsValid(World) || World->GetGameInstance()!=this || bHandlingFailedGameConnection)
	{
		return;
	}
	if (const UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>();
		IsValid(Auth) && !Auth->IsSignedIn())
	{
		// 강제 로그아웃 경로가 이미 로그인 레벨 이동을 시작한 뒤 발생한 늦은 네트워크 오류는
		// 두 번째 로그아웃/레벨 이동으로 이어지지 않게 무시한다.
		ClearPendingGameConnection();
		return;
	}
	if (!IsMultiplayerFailureWorld(World))
	{
		return;
	}
	bHandlingFailedGameConnection = true;
	UE_LOG(LogProjectProject01NetworkSecurity, Warning,
		TEXT("Game server connection failed (%d): %s. Clearing the multiplayer session and returning to login."),
		static_cast<int32>(FailureType), *ErrorString);
	ClearPendingGameConnection();
	if (APlayerController* PlayerController = World->GetFirstPlayerController(); IsValid(PlayerController))
	{
		if (IsValid(PlayerController->PlayerCameraManager))
		{
			PlayerController->PlayerCameraManager->StartCameraFade(
				0.0f, 1.0f, 0.5f, FLinearColor::Black, false, true);
		}
		const TWeakObjectPtr<UProjectProject01GameInstance> WeakThis(this);
		FTimerHandle ReturnTimer;
		World->GetTimerManager().SetTimer(ReturnTimer, [WeakThis]()
		{
			if (UProjectProject01GameInstance* Instance = WeakThis.Get(); IsValid(Instance))
			{
				Instance->BeginSafeLogoutAfterGameServerFailure();
			}
		}, 0.5f, false);
	}
	else
	{
		BeginSafeLogoutAfterGameServerFailure();
	}
}

void UProjectProject01GameInstance::BeginSafeLogoutAfterGameServerFailure()
{
	UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>();
	if (!IsValid(Auth))
	{
		ReturnToLoginAndClearMultiplayerState();
		return;
	}

	Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01GameInstance::HandleSafeLogoutCompleted);
	Auth->OnLogoutCompleted.AddUniqueDynamic(this, &UProjectProject01GameInstance::HandleSafeLogoutCompleted);
	Auth->Logout();
}

void UProjectProject01GameInstance::HandleSafeLogoutCompleted(
	const bool bSuccess,
	const FString& Message,
	const FString& DisplayName)
{
	(void)Message;
	(void)DisplayName;
	UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>();
	if (IsValid(Auth))
	{
		Auth->OnLogoutCompleted.RemoveDynamic(this, &UProjectProject01GameInstance::HandleSafeLogoutCompleted);
		if (!bSuccess)
		{
			Auth->ForceClearLocalSession();
		}
	}
	bHandlingFailedGameConnection = false;
	ReturnToLoginAndClearMultiplayerState();
}

void UProjectProject01GameInstance::HandleAuthenticationSessionInvalidated(const FString& Message)
{
	if (IsRunningDedicatedServer() || bHandlingSessionInvalidation)
	{
		return;
	}
	bHandlingSessionInvalidation = true;
	UE_LOG(LogProjectProject01NetworkSecurity, Warning,
		TEXT("Authentication session invalidated: %s. Clearing room/game state and returning to login."),
		*Message);
	ReturnToLoginAndClearMultiplayerState();
	bHandlingSessionInvalidation = false;
}

void UProjectProject01GameInstance::ReturnToLoginAndClearMultiplayerState()
{
	SetSurveyLoginRequested(false);
	ClearPendingGameConnection();
	if (UProjectProject01VoiceChatSubsystem* Voice = GetSubsystem<UProjectProject01VoiceChatSubsystem>();
		IsValid(Voice))
	{
		Voice->LeaveMatch();
	}
	if (UProjectProject01LobbySubsystem* Lobby = GetSubsystem<UProjectProject01LobbySubsystem>();
		IsValid(Lobby))
	{
		Lobby->ForceClearLocalSession();
	}
	if (UProjectProject01AuthSubsystem* Auth = GetSubsystem<UProjectProject01AuthSubsystem>(); IsValid(Auth))
	{
		Auth->ForceClearLocalSession();
	}
	bHandlingFailedGameConnection = false;
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/MyProject/Level/LoginLevel")));
}

void UProjectProject01GameInstance::SendGameServerHeartbeat()
{
	const UWorld* World = GetWorld();
	FString ManagedStopFile;
	if (IsRunningDedicatedServer() &&
		FParse::Value(FCommandLine::Get(), TEXT("ProjectProject01ManagedStopFile="), ManagedStopFile) &&
		IFileManager::Get().FileExists(*ManagedStopFile))
	{
		// Normal session completion must run Shutdown, including removal of crash activity markers.
		FPlatformMisc::RequestExit(false);
		return;
	}
	if (!IsRunningDedicatedServer() || !IsValid(World) || !IsBackendUrlAllowed())
	{
		return;
	}
	const FString ServerSecret = LoadGameServerSharedSecret();
	if (ServerSecret.Len() < 32)
	{
		UE_LOG(LogProjectProject01NetworkSecurity, Warning,
			TEXT("Cannot send game server heartbeat because the server secret is unavailable."));
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SecurityApiBaseUrl + TEXT("/api/server/heartbeat"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Secret"), ServerSecret);
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Port"), FString::FromInt(World->URL.Port));
	FProjectProject01VersionContract::ApplyToRequest(Request);
	if (ServerBootId.IsEmpty()) ServerBootId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	AMultiplayTestGameMode* ServerMode = World->GetAuthGameMode<AMultiplayTestGameMode>();
	TSharedRef<FJsonObject> Heartbeat = MakeShared<FJsonObject>();
	Heartbeat->SetNumberField(TEXT("port"), World->URL.Port);
	Heartbeat->SetStringField(TEXT("bootId"), ServerBootId);
	FString ManagedId;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectProject01ManagedId="), ManagedId))
		Heartbeat->SetStringField(TEXT("managedId"), ManagedId);
	Heartbeat->SetBoolField(TEXT("ready"), IsValid(ServerMode) && ServerMode->IsReadyForServerAllocation());
	if (IsValid(ServerMode) && !ServerMode->GetServerMatchId().IsEmpty())
		Heartbeat->SetStringField(TEXT("matchId"), ServerMode->GetServerMatchId());
	FString HeartbeatBody;
	FJsonSerializer::Serialize(Heartbeat, TJsonWriterFactory<>::Create(&HeartbeatBody));
	Request->SetContentAsString(HeartbeatBody);
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
					TEXT("Game server heartbeat failed with status %d."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
				FString ManagedInstance;
				if (FParse::Value(FCommandLine::Get(), TEXT("ProjectProject01ManagedId="), ManagedInstance))
				{
					const double Now = FPlatformTime::Seconds();
					if (ManagedHeartbeatFailureSince <= 0.0) ManagedHeartbeatFailureSince = Now;
					// Do not leave orphaned match processes after the operating backend disappears.
					if (Now - ManagedHeartbeatFailureSince >= 30.0) FPlatformMisc::RequestExit(false);
				}
			}
			else ManagedHeartbeatFailureSince = 0.0;
		});
	PendingSecurityRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		RemovePendingRequest(Request);
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

	PendingClientTicket = Ticket;
	PendingClientEncryptionKey = MoveTemp(Key);
	PendingCrashMatchId = MatchId.Left(ProjectProject01CrashReports::MaxShortFieldLength);
	PendingCrashPlayerRole = Role.Left(ProjectProject01CrashReports::MaxShortFieldLength);
	RefreshCrashReportContext(GetWorld());
	return true;
}

void UProjectProject01GameInstance::ClearPendingGameConnection()
{
	PendingClientTicket.Reset();
	PendingClientEncryptionKey.Reset();
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
	Request->SetHeader(TEXT("X-ProjectProject01-Server-Port"), FString::FromInt(GetWorld()->URL.Port));
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
			for (auto It = ValidatedClaims.CreateIterator(); It; ++It)
			{
				if (It.Value().ValidUntilUtc <= NowUtc)
				{
					It.RemoveCurrent();
				}
			}
			Claim.ValidUntilUtc = NowUtc + FTimespan::FromSeconds(30.0);
			ValidatedClaims.Add(EncryptionToken, MoveTemp(Claim));
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
	if (PendingClientEncryptionKey.Num() !=
		ProjectProject01NetworkSecurity::Aes256KeyBytes)
	{
		ProjectProject01NetworkSecurity::CompleteEncryptionFailure(
			Delegate, EEncryptionResponse::NoKey, TEXT("No pending AES-GCM key is available."));
		return;
	}

	FEncryptionKeyResponse Response;
	Response.Response = EEncryptionResponse::Success;
	Response.EncryptionData.Key = PendingClientEncryptionKey;
	Response.EncryptionData.Identifier = TEXT("ProjectProject01Match");
	Delegate.ExecuteIfBound(Response);
	ClearPendingGameConnection();
}

bool UProjectProject01GameInstance::ConsumeValidatedJoinClaim(
	const FString& Ticket,
	FProjectProject01ValidatedJoinClaim& OutClaim)
{
	if (FProjectProject01ValidatedJoinClaim* Claim = ValidatedClaims.Find(Ticket))
	{
		if (Claim->ValidUntilUtc <= FDateTime::UtcNow())
		{
			ValidatedClaims.Remove(Ticket);
			return false;
		}
		OutClaim = MoveTemp(*Claim);
		ValidatedClaims.Remove(Ticket);
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

	FProjectResultOutbox::Submit(SecurityApiBaseUrl,TEXT("/api/server/matches/results"),Body,ServerSecret,MoveTemp(Completion));
}

void UProjectProject01GameInstance::NotifyAuthoritativeMatchEnded(const FString& MatchId)
{
    if(!GetWorld()||GetWorld()->GetNetMode()==NM_Client||!IsBackendUrlAllowed()||MatchId.IsEmpty())return;
    FString Secret=LoadGameServerSharedSecret();if(Secret.Len()<32)return;
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("matchId"),MatchId);FString Body;
    FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Body));
    FProjectResultOutbox::Submit(SecurityApiBaseUrl,TEXT("/api/server/matches/complete"),Body,Secret);
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
	FString& OutError)
{
	OutError.Reset();
	FString Address = ServerAddress;
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
