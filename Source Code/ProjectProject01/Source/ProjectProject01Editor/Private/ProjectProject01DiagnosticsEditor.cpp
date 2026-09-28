// File: Source/ProjectProject01Editor/Private/ProjectProject01DiagnosticsEditor.cpp
// Target: ProjectProject01Editor Win64 Development, Unreal Engine 5.8.2

#include "ProjectProject01DiagnosticsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ProjectProject01DiagnosticsEditor"

namespace ProjectProject01DiagnosticsEditor
{
	bool bTraceStartedByDiagnostics = false;
	FString ActiveTestRunId;

	void ShowNotification(const FString& Message, const bool bSuccess)
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = bSuccess ? 4.0f : 8.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	bool ForEachPieDiagnosticsSubsystem(TFunctionRef<void(UProjectProject01DiagnosticsSubsystem&)> Callback)
	{
		if (!IsValid(GEngine))
		{
			return false;
		}

		bool bFoundPieWorld = false;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!IsValid(World) || World->WorldType != EWorldType::PIE)
			{
				continue;
			}
			UProjectProject01DiagnosticsSubsystem* Subsystem = World->GetSubsystem<UProjectProject01DiagnosticsSubsystem>();
			if (!IsValid(Subsystem))
			{
				ensureMsgf(false, TEXT("Diagnostics subsystem was unavailable in PIE world %s."), *World->GetName());
				continue;
			}
			bFoundPieWorld = true;
			Callback(*Subsystem);
		}
		return bFoundPieWorld;
	}

	void StartCapture()
	{
		if (!IsValid(GEngine))
		{
			ShowNotification(TEXT("Engine instance를 찾지 못했습니다."), false);
			return;
		}
		if (!ActiveTestRunId.IsEmpty())
		{
			ShowNotification(TEXT("진단 기록이 이미 진행 중입니다."), false);
			return;
		}

		const FString RunId = FString::Printf(TEXT("%s_%s"),
			*FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
		const FString TraceDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics"), RunId);
		if (!IFileManager::Get().MakeDirectory(*TraceDirectory, true))
		{
			ShowNotification(TEXT("Diagnostics 저장 폴더를 만들지 못했습니다."), false);
			return;
		}

		const bool bFoundPieWorld = ForEachPieDiagnosticsSubsystem([&RunId](UProjectProject01DiagnosticsSubsystem& Subsystem)
		{
			// Empty override keeps the operator-authored Name from DefaultGame.ini.
			Subsystem.StartCapture(RunId, TEXT(""));
		});
		if (!bFoundPieWorld)
		{
			ShowNotification(TEXT("실행 중인 PIE 월드를 찾지 못했습니다. PIE를 시작한 뒤 다시 실행하세요."), false);
			return;
		}

		const FTraceAuxiliary::ETraceSystemStatus TraceStatus = FTraceAuxiliary::GetTraceSystemStatus();
		if (TraceStatus == FTraceAuxiliary::ETraceSystemStatus::Available)
		{
			const FString TracePath = FPaths::Combine(TraceDirectory, FString::Printf(TEXT("%s.utrace"), *RunId));
			bTraceStartedByDiagnostics = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TracePath, TEXT("default"), nullptr, LogProjectProject01Diagnostics);
			if (!bTraceStartedByDiagnostics)
			{
				UE_LOG(LogProjectProject01Diagnostics, Warning, TEXT("Diagnostics started without a .utrace because tracing could not start."));
			}
		}
		else
		{
			UE_LOG(LogProjectProject01Diagnostics, Warning, TEXT("Diagnostics did not start a .utrace because tracing is unavailable or already active."));
		}

		ActiveTestRunId = RunId;
		ShowNotification(FString::Printf(TEXT("진단 기록 시작: %s"), *RunId), true);
	}

	void StopCapture()
	{
		if (ActiveTestRunId.IsEmpty())
		{
			ShowNotification(TEXT("진행 중인 진단 기록이 없습니다."), false);
			return;
		}
		const FString CompletedRunId = ActiveTestRunId;
		ForEachPieDiagnosticsSubsystem([](UProjectProject01DiagnosticsSubsystem& Subsystem)
		{
			Subsystem.StopCapture(TEXT("Manual capture completed; pass/fail requires the configured test criteria."));
		});
		if (bTraceStartedByDiagnostics)
		{
			FTraceAuxiliary::Stop();
			bTraceStartedByDiagnostics = false;
		}
		ActiveTestRunId.Reset();
		ShowNotification(FString::Printf(TEXT("진단 기록 저장 완료: Saved/Diagnostics/%s"), *CompletedRunId), true);
	}

	void OpenDiagnosticsFolder()
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		FPlatformProcess::ExploreFolder(*Directory);
	}
}

void RegisterProjectProject01DiagnosticsMenus()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (Menu == nullptr)
	{
		ensureMsgf(false, TEXT("ProjectProject01 diagnostics menu could not extend the Tools menu."));
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ProjectProject01Diagnostics"));
	Section.Label = LOCTEXT("DiagnosticsSection", "ProjectProject01 Diagnostics");
	Section.AddMenuEntry(TEXT("ProjectProject01StartDiagnostics"), LOCTEXT("StartDiagnostics", "Start Lightweight Diagnostics Capture"),
		LOCTEXT("StartDiagnosticsTip", "Start rate-limited PIE diagnostics and a default Unreal Insights trace when no other trace is active."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&ProjectProject01DiagnosticsEditor::StartCapture)));
	Section.AddMenuEntry(TEXT("ProjectProject01StopDiagnostics"), LOCTEXT("StopDiagnostics", "Stop and Save Diagnostics Capture"),
		LOCTEXT("StopDiagnosticsTip", "Flush XML and CSV reports. It stops only a trace started by this utility."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&ProjectProject01DiagnosticsEditor::StopCapture)));
	Section.AddMenuEntry(TEXT("ProjectProject01OpenDiagnostics"), LOCTEXT("OpenDiagnostics", "Open Diagnostics Folder"),
		LOCTEXT("OpenDiagnosticsTip", "Open Saved/Diagnostics in Explorer."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&ProjectProject01DiagnosticsEditor::OpenDiagnosticsFolder)));
}

#undef LOCTEXT_NAMESPACE
